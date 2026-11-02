#include "bridge_delivery_revision.hpp"
#include "bridge_delivery_retry.hpp"
#include "bridge_state_repository.hpp"
#include <algorithm>
#include <boost/asio/this_coro.hpp>
#include <exception>
#include <limits>

namespace bridge {
namespace {
namespace d = obcx::bot;
auto decode(const ledger::OpaqueValue &value, std::string_view codec)
    -> d::Json {
  if (value.codec != codec ||
      value.version != (codec == "bridge.EditIntent.cbor" ? 2 : 1) ||
      value.bytes.empty() ||
      value.bytes.size() > d::detail::domain_maximum_bytes)
    throw std::runtime_error("invalid edit evidence codec");
  auto json = d::Json::from_cbor(value.bytes.begin(), value.bytes.end());
  d::detail::validate_domain_wire(json);
  const auto canonical = d::Json::to_cbor(json);
  if (std::string(canonical.begin(), canonical.end()) != value.bytes)
    throw std::runtime_error("noncanonical edit evidence");
  return json;
}
auto evidence(const BridgeRevisionResult &result) -> ledger::OpaqueValue {
  if (result.prior_deletion_operations.size() != result.deletions.size())
    throw std::runtime_error("edit deletion origin evidence mismatch");
  d::Json json{{"deletions", result.deletions},
               {"prior_deletion_operations", result.prior_deletion_operations},
               {"replacement",
                result.outcome ? d::Json(*result.outcome) : d::Json(nullptr)},
               {"interrupted", result.interrupted}};
  d::detail::validate_domain_wire(json);
  const auto bytes = d::Json::to_cbor(json);
  if (bytes.size() > d::detail::domain_maximum_bytes)
    throw d::detail::DomainBudgetExceeded{};
  return {"bridge.EditOutcomes.cbor", 1, {bytes.begin(), bytes.end()}};
}
} // namespace

namespace detail {
auto submit_revision_replacement(
    std::shared_ptr<d::BotOperationGateway> gateway,
    std::shared_ptr<BridgeStateRepository> repository,
    std::shared_ptr<obcx::core::BlockingExecutor> blocking,
    BridgeDeliveryRequest request, BridgeRevisionResult result,
    obcx::core::ActorGenerationLifecycle::TokenPtr token)
    -> boost::asio::awaitable<BridgeRevisionResult> {
  if (!gateway || !repository || !blocking || !result.persisted.checkpoint)
    throw std::invalid_argument(
        "replacement requires newly admitted owned services");
  auto checkpoint = *result.persisted.checkpoint;
  if (checkpoint.phase != ledger::RevisionPhase::Sending ||
      checkpoint.send_attempt_ids.empty() ||
      checkpoint.send_attempt_ids.back() != request.attempt_id ||
      checkpoint.send_results.size() + 1 != checkpoint.send_attempt_ids.size())
    throw std::runtime_error("replacement has no matching send admission");
  const d::OperationCorrelation association{
      "bridge-edit-v1:" +
      d::Json::array({request.delivery_id,
                      result.persisted.admission.revision_id,
                      request.attempt_id})
          .dump()};
  association.validate();
  const auto cancellation = co_await boost::asio::this_coro::cancellation_state;
  const auto previous = co_await boost::asio::this_coro::throw_if_cancelled();
  co_await boost::asio::this_coro::throw_if_cancelled(false);
  result.outcome.reset();
  result.interrupted = false;
  bool invoking = false;
  bool persistence_failed = false;
  bool uncertain = false;
  try {
    if (cancellation.cancelled() == boost::asio::cancellation_type::none &&
        (!token || token->valid())) {
      invoking = true;
      result.outcome =
          co_await d::BotClient{*gateway}.send(request.send, association);
      invoking = false;
      const auto terminal = describe_delivery_outcome(request, *result.outcome);
      checkpoint.send_results.push_back(terminal);
      checkpoint.outputs = terminal.outputs;
      checkpoint.complete = terminal.complete;
      uncertain = terminal.effect == ledger::Effect::PossiblySubmitted;
      if (const auto *failure = std::get_if<d::SendFailure>(&*result.outcome)) {
        result.interrupted =
            failure->error.code == d::BotOperationErrorCode::Cancelled;
        if (!result.interrupted && terminal.retryable &&
            cancellation.cancelled() == boost::asio::cancellation_type::none &&
            (!token || token->valid()) && failure->error.retryable &&
            terminal.effect == ledger::Effect::DefinitelyNotSubmitted &&
            terminal.outputs.empty() && failure->confirmed_messages.empty() &&
            !failure->may_have_unconfirmed_messages &&
            failure->error.submission_safety ==
                d::SubmissionSafety::DefinitelyNotSubmitted &&
            request.retry_policy &&
            checkpoint.send_attempt_ids.size() <
                static_cast<std::uint64_t>(request.max_attempts))
          checkpoint.retry_due_at_ms = delivery_retry_due_at_ms(
              *request.retry_policy,
              static_cast<std::int64_t>(checkpoint.send_attempt_ids.size()),
              terminal.finished_at_ms);
      }
    } else {
      result.interrupted = true;
      result.outcome = d::SendFailure{
          {d::BotOperationErrorCode::Cancelled,
           "edit replacement cancelled before invocation", std::nullopt,
           std::nullopt, false, d::SubmissionSafety::DefinitelyNotSubmitted},
          {},
          {},
          false};
      checkpoint.send_results.push_back(
          describe_delivery_outcome(request, *result.outcome));
    }
  } catch (...) {
    result.interrupted = true;
    uncertain = true;
    persistence_failed = !invoking;
  }
  if (!persistence_failed) {
    try {
      if (checkpoint.sequence == std::numeric_limits<std::int64_t>::max())
        throw std::runtime_error("edit checkpoint sequence exhausted");
      ++checkpoint.sequence;
      checkpoint.phase = ledger::RevisionPhase::Finished;
      checkpoint.settled = !uncertain;
      checkpoint.evidence = evidence(result);
      result.persisted = co_await blocking->run(
          [repository, expected = result.persisted, checkpoint] {
            return repository->checkpoint_revision(expected, checkpoint);
          });
      result.newly_completed =
          checkpoint.complete && result.persisted.observations_match_checkpoint;
    } catch (...) {
      persistence_failed = true;
    }
  }
  co_await boost::asio::this_coro::throw_if_cancelled(previous);
  if (persistence_failed)
    throw BridgeRevisionPersistenceFailure(std::move(result));
  co_return result;
}
} // namespace detail

auto send_bridge_revision(
    std::shared_ptr<d::BotOperationGateway> gateway,
    std::shared_ptr<BridgeStateRepository> repository,
    std::shared_ptr<obcx::core::BlockingExecutor> blocking,
    BridgeDeliveryRequest request, ledger::RevisionAdmission admission)
    -> boost::asio::awaitable<BridgeRevisionResult> {
  if (!gateway || !repository || !blocking)
    throw std::invalid_argument("edit requires generation-owned services");
  request.send.validate();
  if (request.max_attempts <= 0 ||
      (request.max_attempts > 1 && !request.retry_policy) ||
      request.quote_anchor != BridgeQuoteAnchor::FirstConfirmed ||
      admission.expected_outputs.size() > d::detail::domain_maximum_items)
    throw std::invalid_argument(
        "invalid explicit edit policy or output budget");
  if (request.retry_policy)
    request.retry_policy->validate();
  for (const auto &block : request.send.body)
    std::visit(
        [](const auto &value) {
          if constexpr (requires { value.source; })
            if (!std::holds_alternative<d::BytesSource>(value.source) &&
                !std::holds_alternative<d::MediaRef>(value.source))
              throw std::invalid_argument(
                  "edit requires durable owned media before deletion");
        },
        block);
  const auto intent = decode(admission.intent, "bridge.EditIntent.cbor");
  if (request.members.size() != 1 ||
      request.delivery_id != admission.delivery_id ||
      request.pair_id != admission.pair_id ||
      decode(admission.source_scope, "obcx.bot.ConversationRef.cbor")
              .get<d::ConversationRef>() !=
          request.members.front().message.conversation ||
      decode(admission.target_scope, "obcx.bot.ConversationRef.cbor")
              .get<d::ConversationRef>() != request.send.target ||
      request.members.front().member_id != admission.member_id ||
      request.members.front().input_blocks != admission.input_blocks ||
      decode(admission.source_ref, "obcx.bot.MessageRef.cbor")
              .get<d::MessageRef>() != request.members.front().message ||
      intent.at("send") != d::Json(request.send) ||
      intent.at("pair_id") != request.pair_id ||
      intent.at("attempt_id") != request.attempt_id ||
      intent.at("revision_id") != admission.revision_id ||
      intent.at("member_id") != admission.member_id ||
      request.block_origins.size() != request.send.body.size())
    throw std::invalid_argument(
        "edit send plan disagrees with admitted evidence");
  if (intent.at("max_attempts") != request.max_attempts ||
      intent.at("input_blocks") != d::Json(admission.input_blocks) ||
      intent.at("quote_anchor") != static_cast<unsigned>(request.quote_anchor))
    throw std::invalid_argument("edit budget or quote policy changed");
  const auto retry_policy =
      request.retry_policy ? d::Json{{"base_interval_sec",
                                      request.retry_policy->base_interval_sec},
                                     {"max_interval_sec",
                                      request.retry_policy->max_interval_sec}}
                           : d::Json(nullptr);
  if (intent.at("retry_policy") != retry_policy ||
      intent.at("publication").at("event_id") != request.publication.event_id ||
      intent.at("publication").at("correlation_id") !=
          request.publication.correlation_id ||
      intent.at("publication").at("source_bot") !=
          request.publication.source_bot)
    throw std::invalid_argument("edit retry or publication policy changed");
  auto origins = d::Json::array();
  for (const auto &block : request.block_origins) {
    if (!block) {
      origins.push_back(nullptr);
      continue;
    }
    if (block->empty())
      throw std::invalid_argument("edit has empty known source contribution");
    auto values = d::Json::array();
    for (const auto &origin : *block) {
      if (origin.member_id != admission.member_id ||
          std::ranges::find(admission.input_blocks, origin.input_block) ==
              admission.input_blocks.end())
        throw std::invalid_argument("edit source contribution mismatch");
      values.push_back({{"member_id", origin.member_id},
                        {"input_block", origin.input_block}});
    }
    origins.push_back(std::move(values));
  }
  if (intent.at("block_origins") != origins)
    throw std::invalid_argument("edit source contribution binding changed");
  const auto prior_deletes = reconcile_revision_mutations(
      admission.expected_output_operations, admission.expected_outputs);
  std::vector<d::MessageRef> targets;
  targets.reserve(admission.expected_outputs.size());
  for (const auto &ref : admission.expected_outputs) {
    auto target = decode(ref, "obcx.bot.MessageRef.cbor").get<d::MessageRef>();
    target.validate();
    if (target.conversation != request.send.target)
      throw std::invalid_argument("edit deletion target scope mismatch");
    targets.push_back(std::move(target));
  }
  d::OperationCorrelation association{
      "bridge-edit-v1:" +
      d::Json::array(
          {admission.delivery_id, admission.revision_id, request.attempt_id})
          .dump()};
  association.validate();
  const auto admitted = co_await blocking->run([repository, admission] {
    return repository->admit_revision(admission);
  });
  BridgeRevisionResult result{
      {admission, std::nullopt}, {}, std::nullopt, false, false};
  if (admitted != ledger::AdmissionResult::Created) {
    const auto snapshot =
        co_await blocking->run([repository, id = admission.delivery_id] {
          return repository->load_delivery(id);
        });
    if (!snapshot)
      throw std::runtime_error("replayed edit disappeared");
    const auto replay =
        std::ranges::find_if(snapshot->revisions, [&](const auto &entry) {
          return entry.admission.revision_id == admission.revision_id;
        });
    if (replay == snapshot->revisions.end())
      throw std::runtime_error("replayed edit checkpoint disappeared");
    result.persisted = *replay;
    co_return result; // No provider call, publication, suffix or lease refresh.
  }
  result.deletions.reserve(targets.size());
  auto checkpoint = ledger::RevisionCheckpoint{0,
                                               ledger::RevisionPhase::Admitted,
                                               evidence(result),
                                               {},
                                               {},
                                               {},
                                               false,
                                               false};
  for (const auto &prior : prior_deletes)
    checkpoint.deleted_outputs.push_back(prior.message_ref);
  const auto save =
      [&](ledger::RevisionPhase phase) -> boost::asio::awaitable<void> {
    const auto sequence =
        result.persisted.checkpoint ? result.persisted.checkpoint->sequence : 0;
    if (sequence == std::numeric_limits<std::int64_t>::max())
      throw std::runtime_error("edit checkpoint sequence exhausted");
    checkpoint.sequence = sequence + 1;
    checkpoint.phase = phase;
    checkpoint.evidence = evidence(result);
    result.persisted = co_await blocking->run(
        [repository, expected = result.persisted, next = checkpoint] {
          return repository->checkpoint_revision(expected, next);
        });
  };
  const auto cancellation = co_await boost::asio::this_coro::cancellation_state;
  const auto previous = co_await boost::asio::this_coro::throw_if_cancelled();
  co_await boost::asio::this_coro::throw_if_cancelled(false);
  bool uncertain = false;
  bool persistence_failed = false;
  bool invoking = false;
  try {
    for (std::size_t i = 0; i < targets.size(); ++i) {
      if (cancellation.cancelled() != boost::asio::cancellation_type::none) {
        result.interrupted = true;
        break;
      }
      const auto prior_delete =
          std::ranges::find_if(prior_deletes, [&](const auto &entry) {
            return entry.message_ref == admission.expected_outputs[i];
          });
      if (prior_delete != prior_deletes.end()) {
        result.deletions.push_back(prior_delete->outcome);
        result.prior_deletion_operations.push_back(prior_delete->operation_id);
        co_await save(ledger::RevisionPhase::Deleting);
        continue; // The original command already deleted this exact output.
      }
      result.prior_deletion_operations.emplace_back();
      result.deletions.push_back(d::failed_operation<d::DeleteReceipt>(
          d::BotOperationErrorCode::OutcomeUnknown,
          "edit deletion outcome unavailable", false,
          d::SubmissionSafety::PossiblySubmitted));
      co_await save(ledger::RevisionPhase::Deleting);
      if (cancellation.cancelled() != boost::asio::cancellation_type::none) {
        result.deletions.back() = d::failed_operation<d::DeleteReceipt>(
            d::BotOperationErrorCode::Cancelled,
            "edit deletion cancelled before invocation", false,
            d::SubmissionSafety::DefinitelyNotSubmitted);
        checkpoint.retained_outputs.push_back(admission.expected_outputs[i]);
        result.interrupted = true;
        co_await save(ledger::RevisionPhase::Deleting);
        break;
      }
      invoking = true;
      result.deletions.back() =
          co_await d::BotClient{*gateway}.delete_message({targets[i]});
      invoking = false;
      const auto &deleted = result.deletions.back();
      deleted.validate();
      if (deleted.ok() && deleted.value->message == targets[i]) {
        checkpoint.deleted_outputs.push_back(admission.expected_outputs[i]);
      } else if (deleted.error &&
                 deleted.error->submission_safety ==
                     d::SubmissionSafety::DefinitelyNotSubmitted) {
        checkpoint.retained_outputs.push_back(admission.expected_outputs[i]);
        if (deleted.error->code == d::BotOperationErrorCode::Cancelled)
          result.interrupted = true;
      } else {
        uncertain = true;
        result.interrupted = true;
      }
      co_await save(ledger::RevisionPhase::Deleting);
      if (result.interrupted)
        break;
    }
    if (cancellation.cancelled() != boost::asio::cancellation_type::none)
      result.interrupted = true;
    if (!result.interrupted) {
      checkpoint.send_attempt_ids.push_back(request.attempt_id);
      co_await save(ledger::RevisionPhase::Sending);
      // The replacement owner rechecks cancellation after durable admission.
      // It never re-enters the predecessor deletion loop.
    }
  } catch (...) {
    // Never enter the provider loop again. Raw owning results remain in result
    // even if serialization/finalization failed after a physical response.
    result.interrupted = true;
    uncertain = true;
    persistence_failed = !invoking;
  }
  if (!persistence_failed &&
      checkpoint.phase == ledger::RevisionPhase::Sending) {
    std::exception_ptr error;
    try {
      result = co_await detail::submit_revision_replacement(
          gateway, repository, blocking, std::move(request), std::move(result),
          {});
    } catch (...) {
      error = std::current_exception();
    }
    co_await boost::asio::this_coro::throw_if_cancelled(previous);
    if (error)
      std::rethrow_exception(error);
    co_return result;
  }
  if (!persistence_failed) {
    try {
      checkpoint.settled = !uncertain;
      co_await save(ledger::RevisionPhase::Finished);
      result.newly_completed =
          checkpoint.complete && result.persisted.observations_match_checkpoint;
    } catch (...) {
      persistence_failed = true;
    }
  }
  co_await boost::asio::this_coro::throw_if_cancelled(previous);
  if (persistence_failed)
    throw BridgeRevisionPersistenceFailure(std::move(result));
  co_return result;
}
} // namespace bridge
