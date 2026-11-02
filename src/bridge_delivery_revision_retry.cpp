#include "bridge_delivery_retry.hpp"
#include "bridge_delivery_revision.hpp"
#include "bridge_state_repository.hpp"
#include <algorithm>
#include <boost/asio/this_coro.hpp>
#include <chrono>
#include <exception>
#include <set>
#include <telegram/bot/domain_views.hpp>

namespace bridge {
namespace {
namespace d = obcx::bot;
auto document(const ledger::OpaqueValue &value, std::string_view codec)
    -> d::Json {
  if (value.codec != codec ||
      value.version != (codec == "bridge.EditIntent.cbor" ? 2 : 1) ||
      value.bytes.empty() ||
      value.bytes.size() > d::detail::domain_maximum_bytes)
    throw std::runtime_error("unsupported edit retry evidence");
  auto json = d::Json::from_cbor(value.bytes.begin(), value.bytes.end());
  d::detail::validate_domain_wire(json);
  const auto bytes = d::Json::to_cbor(json);
  if (std::string(bytes.begin(), bytes.end()) != value.bytes)
    throw std::runtime_error("noncanonical edit retry evidence");
  return json;
}
auto restore(const ledger::RevisionSnapshot &snapshot,
             const BridgeRetryRoute &route) -> BridgeDeliveryRequest {
  route.source.validate();
  route.target.validate();
  const auto &admission = snapshot.admission;
  if (!snapshot.checkpoint || !snapshot.observations_match_checkpoint ||
      !snapshot.observed_outputs.empty() ||
      !snapshot.confirmed_send_attempt_ids.empty() ||
      admission.pair_id != route.pair_id ||
      document(admission.source_scope, "obcx.bot.ConversationRef.cbor")
              .get<d::ConversationRef>() != route.source ||
      document(admission.target_scope, "obcx.bot.ConversationRef.cbor")
              .get<d::ConversationRef>() != route.target)
    throw std::runtime_error(
        "edit retry route or observation evidence unavailable");
  const auto &checkpoint = *snapshot.checkpoint;
  if (!checkpoint.retry_due_at_ms || !checkpoint.settled ||
      checkpoint.complete ||
      checkpoint.phase != ledger::RevisionPhase::Finished ||
      !checkpoint.outputs.empty() || checkpoint.send_attempt_ids.empty() ||
      checkpoint.send_attempt_ids.size() > d::detail::domain_maximum_items ||
      checkpoint.send_results.size() != checkpoint.send_attempt_ids.size())
    throw std::runtime_error(
        "edit retry lacks complete zero-effect send history");
  const auto intent = document(admission.intent, "bridge.EditIntent.cbor");
  const auto source = intent.at("original_message").get<d::MessageReceived>();
  if (source.message !=
          document(admission.source_ref, "obcx.bot.MessageRef.cbor")
              .get<d::MessageRef>() ||
      source.message.conversation != route.source ||
      !obcx::telegram::bot::message_update_view(source) ||
      intent.at("delivery_id") != admission.delivery_id ||
      intent.at("revision_id") != admission.revision_id ||
      intent.at("pair_id") != admission.pair_id ||
      intent.at("member_id") != admission.member_id ||
      intent.at("input_blocks") != d::Json(admission.input_blocks) ||
      intent.at("attempt_id") != admission.revision_id + ":send:1" ||
      intent.at("quote_anchor").get<unsigned>() !=
          static_cast<unsigned>(BridgeQuoteAnchor::FirstConfirmed))
    throw std::runtime_error("edit retry immutable source binding mismatch");
  const auto &publication = intent.at("publication");
  const auto &policy = intent.at("retry_policy");
  BridgeDeliveryRetryPolicy retry_policy{
      policy.at("base_interval_sec").get<std::int64_t>(),
      policy.at("max_interval_sec").get<std::int64_t>()};
  retry_policy.validate();
  auto send = intent.at("send").get<d::SendMessageRequest>();
  send.validate();
  if (send.target != route.target ||
      publication.at("source_bot") !=
          route.source.installation.installation_id ||
      publication.at("event_id").get<std::string>().empty())
    throw std::runtime_error(
        "edit retry target or publication identity mismatch");
  for (const auto &block : send.body)
    std::visit(
        [](const auto &value) {
          if constexpr (requires { value.source; })
            if (!std::holds_alternative<d::BytesSource>(value.source) &&
                !std::holds_alternative<d::MediaRef>(value.source))
              throw std::runtime_error(
                  "edit retry lacks durable media ownership");
        },
        block);
  const std::set<std::string> inputs(admission.input_blocks.begin(),
                                     admission.input_blocks.end());
  if (inputs.empty() || inputs.size() != admission.input_blocks.size())
    throw std::runtime_error("invalid edit retry source contributions");
  std::vector<std::optional<std::vector<ledger::Provenance>>> origins;
  for (const auto &block : intent.at("block_origins")) {
    if (block.is_null()) {
      origins.push_back(std::nullopt);
      continue;
    }
    std::vector<ledger::Provenance> values;
    std::set<std::string> seen;
    for (const auto &entry : block) {
      const auto member = entry.at("member_id").get<std::string>();
      const auto input = entry.at("input_block").get<std::string>();
      if (member != admission.member_id || !inputs.contains(input) ||
          !seen.insert(input).second)
        throw std::runtime_error("unbound edit retry contribution");
      values.push_back({member, input});
    }
    if (values.empty())
      throw std::runtime_error("empty known edit retry provenance");
    origins.emplace_back(std::move(values));
  }
  if (origins.size() != send.body.size())
    throw std::runtime_error("edit retry body provenance mismatch");
  const auto maximum = intent.at("max_attempts").get<std::int64_t>();
  if (maximum <= 1 ||
      checkpoint.send_results.size() >= static_cast<std::uint64_t>(maximum))
    throw std::runtime_error("edit retry budget exhausted");
  for (std::size_t i = 0; i < checkpoint.send_results.size(); ++i) {
    const auto &result = checkpoint.send_results[i];
    if (checkpoint.send_attempt_ids[i] !=
            admission.revision_id + ":send:" + std::to_string(i + 1) ||
        result.complete ||
        result.effect != ledger::Effect::DefinitelyNotSubmitted ||
        !result.retryable || !result.outputs.empty() || result.quote_anchor)
      throw std::runtime_error("edit retry send history is not zero-effect");
    const auto outcome = document(result.outcome, "obcx.bot.SendOutcome.cbor")
                             .get<d::SendOutcome>();
    d::validate_send_outcome(send, outcome);
    const auto *failure = std::get_if<d::SendFailure>(&outcome);
    if (!failure || !failure->confirmed_messages.empty() ||
        failure->may_have_unconfirmed_messages ||
        failure->error.submission_safety !=
            d::SubmissionSafety::DefinitelyNotSubmitted ||
        failure->error.code == d::BotOperationErrorCode::Cancelled ||
        !failure->error.retryable)
      throw std::runtime_error(
          "edit retry SDK outcome forbids another submission");
  }
  if (*checkpoint.retry_due_at_ms !=
      detail::delivery_retry_due_at_ms(
          retry_policy,
          static_cast<std::int64_t>(checkpoint.send_results.size()),
          checkpoint.send_results.back().finished_at_ms))
    throw std::runtime_error(
        "edit retry due time differs from immutable policy");
  return {admission.delivery_id,
          admission.revision_id +
              ":send:" + std::to_string(checkpoint.send_results.size() + 1),
          admission.pair_id,
          {{admission.member_id, source.message, source, source.media_group,
            admission.input_blocks}},
          std::move(send),
          std::move(origins),
          BridgeQuoteAnchor::FirstConfirmed,
          intent.at("received_at_ms").get<std::int64_t>(),
          maximum,
          std::nullopt,
          retry_policy,
          {publication.at("event_id").get<std::string>(),
           publication.at("correlation_id").get<std::string>(),
           publication.at("source_bot").get<std::string>()}};
}
} // namespace

auto retry_bridge_revision(
    std::shared_ptr<d::BotOperationGateway> gateway,
    std::shared_ptr<BridgeStateRepository> repository,
    std::shared_ptr<obcx::core::BlockingExecutor> blocking,
    ledger::RevisionSnapshot snapshot, BridgeRetryRoute route,
    obcx::core::ActorGenerationLifecycle::TokenPtr token)
    -> boost::asio::awaitable<BridgeRevisionResult> {
  if (!gateway || !repository || !blocking || !token)
    throw std::invalid_argument(
        "edit retry requires generation-owned services");
  auto request = restore(snapshot, route);
  const auto evidence =
      document(snapshot.checkpoint->evidence, "bridge.EditOutcomes.cbor");
  if (evidence.at("interrupted").get<bool>() ||
      evidence.at("replacement") !=
          document(snapshot.checkpoint->send_results.back().outcome,
                   "obcx.bot.SendOutcome.cbor"))
    throw std::runtime_error("edit retry completion evidence mismatch");
  auto deletions =
      evidence.at("deletions")
          .get<std::vector<d::BotOperationResult<d::DeleteReceipt>>>();
  auto prior_operations =
      evidence.at("prior_deletion_operations").get<std::vector<std::string>>();
  const auto prior_deletes = reconcile_revision_mutations(
      snapshot.admission.expected_output_operations,
      snapshot.admission.expected_outputs);
  if (deletions.size() != snapshot.admission.expected_outputs.size() ||
      prior_operations.size() != deletions.size())
    throw std::runtime_error(
        "edit retry would require unfinished deletion work");
  for (std::size_t i = 0; i < deletions.size(); ++i) {
    const auto &result = deletions[i];
    const auto &ref = snapshot.admission.expected_outputs[i];
    const auto prior =
        std::ranges::find_if(prior_deletes, [&](const auto &entry) {
          return entry.message_ref == ref;
        });
    if ((prior == prior_deletes.end() && !prior_operations[i].empty()) ||
        (prior != prior_deletes.end() &&
         (prior_operations[i] != prior->operation_id ||
          d::Json(result) != d::Json(prior->outcome))))
      throw std::runtime_error(
          "edit retry prior deletion attribution mismatch");
    result.validate();
    if (result.ok()) {
      if (result.value->message !=
              document(ref, "obcx.bot.MessageRef.cbor").get<d::MessageRef>() ||
          std::ranges::find(snapshot.checkpoint->deleted_outputs, ref) ==
              snapshot.checkpoint->deleted_outputs.end())
        throw std::runtime_error("edit retry deletion receipt mismatch");
    } else if (result.error->submission_safety !=
                   d::SubmissionSafety::DefinitelyNotSubmitted ||
               result.error->code == d::BotOperationErrorCode::Cancelled ||
               std::ranges::find(snapshot.checkpoint->retained_outputs, ref) ==
                   snapshot.checkpoint->retained_outputs.end()) {
      throw std::runtime_error(
          "edit retry has uncertain or cancelled deletion");
    }
  }
  if (!token->valid())
    co_return BridgeRevisionResult{std::move(snapshot),
                                   std::move(deletions),
                                   std::nullopt,
                                   false,
                                   false,
                                   std::move(prior_operations)};
  const auto admitted =
      co_await blocking->run([repository, snapshot, id = request.attempt_id] {
        const auto now =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch())
                .count();
        return repository->admit_revision_retry(snapshot, id, now);
      });
  if (!admitted)
    co_return BridgeRevisionResult{std::move(snapshot),
                                   std::move(deletions),
                                   std::nullopt,
                                   false,
                                   false,
                                   std::move(prior_operations)};
  // The new send identity is durable. Preserve cancellation for the submit
  // owner's explicit check while allowing its no-send checkpoint to complete.
  const auto previous = co_await boost::asio::this_coro::throw_if_cancelled();
  co_await boost::asio::this_coro::throw_if_cancelled(false);
  std::optional<BridgeRevisionResult> result;
  std::exception_ptr error;
  try {
    result = co_await detail::submit_revision_replacement(
        std::move(gateway), std::move(repository), std::move(blocking),
        std::move(request),
        BridgeRevisionResult{*admitted, std::move(deletions), std::nullopt,
                             false, false, std::move(prior_operations)},
        std::move(token));
  } catch (...) {
    error = std::current_exception();
  }
  co_await boost::asio::this_coro::throw_if_cancelled(previous);
  if (error)
    std::rethrow_exception(error);
  co_return std::move(*result);
}
} // namespace bridge
