#include "bridge_delivery_sender.hpp"
#include "bridge_bot_failures.hpp"
#include "bridge_command_operations.hpp"
#include "bridge_delivery_retry.hpp"
#include "bridge_hash.hpp"
#include "bridge_state_repository.hpp"
#include <boost/asio/this_coro.hpp>
#include <type_traits>
#include <variant>

#include <algorithm>
#include <chrono>
#include <exception>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace bridge {
namespace {
namespace domain = obcx::bot;
constexpr std::string_view correlation_prefix = "bridge-delivery-v1:";
constexpr std::string_view revision_correlation_prefix = "bridge-edit-v1:";

template <class T>
auto encode(std::string_view type, const T &value) -> ledger::OpaqueValue {
  const domain::Json document = value;
  domain::detail::validate_domain_wire(document);
  const auto bytes = domain::Json::to_cbor(document);
  return {"obcx.bot." + std::string(type) + ".cbor",
          type == "BridgeDeliveryPolicy" ? 2 : 1,
          std::string(bytes.begin(), bytes.end())};
}
auto decode_message(const ledger::OpaqueValue &value) -> domain::MessageRef {
  if (value.codec != "obcx.bot.MessageRef.cbor" || value.version != 1 ||
      value.bytes.empty() ||
      value.bytes.size() > domain::detail::domain_maximum_bytes)
    throw std::runtime_error("unsupported bridge message reference evidence");
  const auto document =
      domain::Json::from_cbor(value.bytes.begin(), value.bytes.end());
  domain::detail::validate_domain_wire(document);
  return document.get<domain::MessageRef>();
}
auto now_ms() -> std::int64_t {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}
auto correlation(const BridgeDeliveryRequest &request)
    -> domain::OperationCorrelation {
  domain::OperationCorrelation{request.delivery_id}.validate();
  domain::OperationCorrelation{request.attempt_id}.validate();
  domain::OperationCorrelation result{
      std::string(correlation_prefix) +
      domain::Json::array({request.delivery_id, request.attempt_id}).dump()};
  result.validate();
  return result;
}
auto admission_for(const BridgeDeliveryRequest &request,
                   const domain::ConversationResolver &references)
    -> ledger::Admission {
  request.send.validate();
  (void)correlation(request);
  domain::OperationCorrelation{request.pair_id}.validate();
  if (request.members.empty() ||
      request.members.size() > domain::detail::domain_maximum_items ||
      request.block_origins.size() != request.send.body.size() ||
      request.max_attempts <= 0 ||
      (request.quote_anchor != BridgeQuoteAnchor::None &&
       request.quote_anchor != BridgeQuoteAnchor::FirstConfirmed))
    throw std::invalid_argument("invalid bridge delivery plan");

  const auto &group = request.members.front().media_group;
  const bool any_group =
      std::ranges::any_of(request.members, [](const auto &member) {
        return member.media_group.has_value();
      });
  if (any_group != request.album_policy.has_value() ||
      (request.album_policy &&
       *request.album_policy != BridgeAlbumPolicy::PreserveObservedBatch) ||
      (any_group &&
       (!group || std::ranges::any_of(request.members, [&](const auto &member) {
         return member.media_group != group;
       }))))
    throw std::invalid_argument("delivery album evidence or policy mismatch");
  const auto &source_scope = request.members.front().message.conversation;
  ledger::Admission result{request.delivery_id,
                           request.pair_id,
                           encode("ConversationRef", source_scope),
                           encode("ConversationRef", request.send.target),
                           {},
                           {},
                           request.admitted_at_ms,
                           request.max_attempts};
  std::set<std::string> members;
  std::set<std::pair<std::string, std::string>> inputs;
  for (const auto &member : request.members) {
    member.message.validate();
    domain::OperationCorrelation{member.member_id}.validate();
    member.source.validate();
    if (member.message.conversation != source_scope ||
        !members.insert(member.member_id).second ||
        member.input_blocks.empty() ||
        member.input_blocks.size() > domain::detail::domain_maximum_items ||
        member.source.message != member.message ||
        member.source.media_group != member.media_group ||
        member.source.context.kind !=
            request.members.front().source.context.kind ||
        member.source.context.group_id !=
            request.members.front().source.context.group_id ||
        member.source.context.topic_id !=
            request.members.front().source.context.topic_id)
      throw std::invalid_argument("delivery typed source evidence mismatch");
    if (member.media_group) {
      member.media_group->validate();
      if (member.media_group->conversation != source_scope)
        throw std::invalid_argument("delivery album scope mismatch");
    }
    for (const auto &block : member.input_blocks) {
      domain::OperationCorrelation{block}.validate();
      if (!inputs.emplace(member.member_id, block).second)
        throw std::invalid_argument("duplicate delivery input identity");
    }
    result.members.push_back(
        {member.member_id, encode("MessageRef", member.message),
         member.media_group
             ? std::optional{encode("MediaGroupRef", *member.media_group)}
             : std::nullopt,
         member.input_blocks});
  }
  auto album_trace = domain::Json::array();
  if (request.album_observations.size() !=
      (any_group ? result.members.size() : 0))
    throw std::invalid_argument("delivery album observation count mismatch");
  for (std::size_t i = 0; i < request.album_observations.size(); ++i) {
    const auto &observation = request.album_observations[i];
    const auto &member = result.members[i];
    if (observation.message_ref != member.message_ref ||
        observation.media_group_ref != *member.media_group_ref ||
        observation.source_bot != source_scope.installation.installation_id ||
        observation.event_id.empty())
      throw std::invalid_argument(
          "delivery album publication binding mismatch");
    if (i == 0 &&
        (observation.event_id != request.publication.event_id ||
         observation.correlation_id != request.publication.correlation_id ||
         observation.source_bot != request.publication.source_bot))
      throw std::invalid_argument("album primary publication identity changed");
    album_trace.push_back({{"member_id", member.member_id},
                           {"event_id", observation.event_id},
                           {"correlation_id", observation.correlation_id},
                           {"source_bot", observation.source_bot}});
  }
  auto origins = domain::Json::array();
  for (const auto &block : request.block_origins) {
    if (!block) {
      origins.push_back(nullptr);
      continue;
    }
    if (block->empty() || block->size() > domain::detail::domain_maximum_items)
      throw std::invalid_argument("invalid known delivery provenance");
    auto values = domain::Json::array();
    std::set<std::pair<std::string, std::string>> seen;
    for (const auto &origin : *block) {
      const auto key = std::pair{origin.member_id, origin.input_block};
      if (!inputs.contains(key) || !seen.insert(key).second)
        throw std::invalid_argument("unbound or duplicate delivery provenance");
      values.push_back({{"member_id", origin.member_id},
                        {"input_block", origin.input_block}});
    }
    origins.push_back(std::move(values));
  }
  if (request.publication.event_id.empty() ||
      request.publication.source_bot !=
          source_scope.installation.installation_id)
    throw std::invalid_argument("delivery lacks original publication identity");
  const auto &facts = request.members.front().source.context;
  if (facts.kind != "group" || facts.group_id.empty())
    throw std::invalid_argument("delivery source lacks current group context");
  const domain::ConfiguredConversation source_route{
      domain::ConfiguredConversationKind::Group, facts.group_id,
      facts.topic_id};
  const auto resolved_source_route =
      references.resolve(source_scope.installation, source_route);
  if (!resolved_source_route.ok() ||
      *resolved_source_route.value != source_scope)
    throw std::invalid_argument("delivery source route evidence conflicts");
  domain::Json policy = {
      {"source_route",
       {{"group_id", facts.group_id},
        {"topic_id", facts.topic_id ? domain::Json(*facts.topic_id)
                                    : domain::Json(nullptr)}}},
      {"publication",
       {{"event_id", request.publication.event_id},
        {"correlation_id", request.publication.correlation_id},
        {"source_bot", request.publication.source_bot}}},
      {"delivery", static_cast<unsigned>(request.send.delivery)},
      {"limits", request.send.limits},
      {"reply_to", request.send.reply_to ? domain::Json(*request.send.reply_to)
                                         : domain::Json(nullptr)},
      {"quote_anchor", request.quote_anchor == BridgeQuoteAnchor::None
                           ? "none"
                           : "first-confirmed"},
      {"block_origins", std::move(origins)}};
  if (request.album_policy) {
    policy["album_batch_policy"] = "preserve-observed-batch";
    policy["album_observations"] = std::move(album_trace);
  }
  if (request.retry_policy) {
    request.retry_policy->validate();
    if (request.max_attempts <= 1)
      throw std::invalid_argument("retry policy requires an attempt budget");
    policy["retry_policy"] = {
        {"base_interval_sec", request.retry_policy->base_interval_sec},
        {"max_interval_sec", request.retry_policy->max_interval_sec}};
  } else if (request.max_attempts > 1) {
    throw std::invalid_argument(
        "delivery retry budget requires explicit policy");
  }
  result.policy_and_limits = encode("BridgeDeliveryPolicy", policy);
  return result;
}
auto physical(const domain::SendOutputDetails &detail,
              std::optional<std::int64_t> ordinal,
              const BridgeDeliveryRequest *request) -> ledger::PhysicalOutput {
  detail.validate();
  ledger::PhysicalOutput result{encode("MessageRef", detail.message),
                                ordinal,
                                std::nullopt,
                                {},
                                std::nullopt,
                                {encode("SendOutputDetails", detail)}};
  for (const auto &media : detail.reusable_media)
    result.reusable_media.push_back(encode("PublishedMedia", media));
  // Notifications are observations only; final request-aware reconciliation
  // supplies provenance. A partial text slice of a shared prepared block does
  // not prove that every source contributing to that block contributed here.
  if (request && detail.content) {
    std::vector<ledger::Provenance> origins;
    for (const auto &slice : *detail.content) {
      domain::validate_content_slice(slice, request->send);
      const auto &block = request->block_origins.at(slice.block_index);
      if (!block || (slice.text && block->size() != 1))
        return result;
      for (const auto &origin : *block)
        if (std::ranges::find(origins, origin) == origins.end())
          origins.push_back(origin);
    }
    if (!origins.empty()) {
      const auto owner = origins.front().member_id;
      if (std::ranges::all_of(
              origins, [&](const auto &v) { return v.member_id == owner; }))
        result.reverse_primary_member = owner;
      result.provenance = std::move(origins);
    }
  }
  return result;
}
auto terminal(const BridgeDeliveryRequest &request,
              const domain::SendOutcome &outcome) -> ledger::AttemptResult {
  domain::validate_send_outcome(request.send, outcome);
  ledger::AttemptResult result{
      false,        ledger::Effect::DefinitelyNotSubmitted, false,   {},
      std::nullopt, encode("SendOutcome", outcome),         now_ms()};
  std::visit(
      [&](const auto &value) {
        if constexpr (std::same_as<std::decay_t<decltype(value)>,
                                   domain::SendReceipt>) {
          result.complete = true;
          result.effect = ledger::Effect::ConfirmedOnly;
        } else {
          result.effect = value.may_have_unconfirmed_messages
                              ? ledger::Effect::PossiblySubmitted
                          : !value.confirmed_messages.empty()
                              ? ledger::Effect::ConfirmedOnly
                              : ledger::Effect::DefinitelyNotSubmitted;
          result.retryable =
              value.confirmed_messages.empty() &&
              !value.may_have_unconfirmed_messages && value.error.retryable &&
              value.error.code != domain::BotOperationErrorCode::Cancelled &&
              value.error.submission_safety ==
                  domain::SubmissionSafety::DefinitelyNotSubmitted;
        }
        for (std::size_t i = 0; i < value.output_details.size(); ++i)
          result.outputs.push_back(physical(
              value.output_details[i], static_cast<std::int64_t>(i), &request));
      },
      outcome);
  if (request.quote_anchor == BridgeQuoteAnchor::FirstConfirmed &&
      !result.outputs.empty())
    result.quote_anchor = result.outputs.front().message_ref;
  return result;
}
} // namespace

auto detail::describe_delivery_outcome(const BridgeDeliveryRequest &request,
                                       const domain::SendOutcome &outcome)
    -> ledger::AttemptResult {
  return terminal(request, outcome);
}

auto send_bridge_delivery(
    std::shared_ptr<obcx::bot::BotOperationGateway> gateway,
    std::shared_ptr<BridgeStateRepository> repository,
    std::shared_ptr<obcx::core::BlockingExecutor> blocking,
    std::shared_ptr<const obcx::bot::ConversationResolver> references,
    BridgeDeliveryRequest request)
    -> boost::asio::awaitable<BridgeDeliveryResult> {
  if (!gateway || !repository || !blocking || !references)
    throw std::invalid_argument(
        "delivery sender requires generation-owned services");
  auto admission = admission_for(request, *references);
  auto failure_notification = detail::prepare_delivery_failure_notification(
      admission, request.attempt_id);
  // Complete serialization and ownership validation before ledger admission.
  // A retry never reacquires an old URL or revives a temporary cache path.
  const auto retry =
      request.retry_policy
          ? std::optional{detail::prepare_delivery_retry(request, 0, 0)}
          : std::nullopt;
  const auto admitted = co_await blocking->run([repository, admission,
                                                album_policy =
                                                    request.album_policy,
                                                album_observations =
                                                    request
                                                        .album_observations] {
    if (album_policy)
      return repository->admit_album_delivery(admission, album_observations);
    for (const auto &member : admission.members) {
      const auto existing = repository->resolve_delivery(
          admission.pair_id, admission.source_scope, admission.target_scope,
          member.message_ref);
      if (existing.status == ledger::ResolutionStatus::Missing)
        continue;
      if (existing.status == ledger::ResolutionStatus::Unique &&
          existing.delivery_ids.size() == 1 &&
          existing.delivery_ids.front() == admission.delivery_id)
        continue; // Only admit_delivery's identical replay may accept this.
      if (existing.status == ledger::ResolutionStatus::Unique)
        throw std::runtime_error(
            "source already belongs to a different delivery; do not resubmit");
      throw std::runtime_error(existing.status ==
                                       ledger::ResolutionStatus::Ambiguous
                                   ? "ambiguous delivery source binding"
                                   : "corrupt delivery source binding");
    }
    return repository->admit_delivery(admission);
  });
  if (admitted == ledger::AdmissionResult::AlbumBatchSuppressed)
    throw BridgeAlbumBatchSuppressed{};
  if (admitted != ledger::AdmissionResult::Created) {
    auto snapshot =
        co_await blocking->run([repository, id = request.delivery_id] {
          return repository->load_delivery(id);
        });
    if (!snapshot)
      throw std::runtime_error("admitted delivery snapshot missing");
    co_return BridgeDeliveryResult{std::move(*snapshot), std::nullopt, false};
  }
  if (retry)
    co_await blocking->run([repository, id = request.delivery_id, retry] {
      repository->store_retry_payload(id, *retry);
    });
  const auto attempt = co_await blocking->run(
      [repository, id = request.delivery_id, attempt = request.attempt_id,
       started = now_ms()] {
        return repository->admit_attempt(id, attempt, started);
      });
  if (attempt != ledger::AdmissionResult::Created)
    throw std::runtime_error("existing attempt does not authorize submission");

  // An admitted attempt must reach the submit owner's cancellation-aware
  // settlement even if cancellation arrived during the joined DB admission.
  const auto previous = co_await boost::asio::this_coro::throw_if_cancelled();
  co_await boost::asio::this_coro::throw_if_cancelled(false);
  std::optional<BridgeDeliveryResult> result;
  std::exception_ptr error;
  try {
    result = co_await detail::submit_delivery_attempt(
        std::move(gateway), std::move(repository), std::move(blocking),
        std::move(request), 1, {}, std::move(failure_notification));
  } catch (...) {
    error = std::current_exception();
  }
  co_await boost::asio::this_coro::throw_if_cancelled(previous);
  if (error)
    std::rethrow_exception(error);
  co_return std::move(*result);
}

auto detail::submit_delivery_attempt(
    std::shared_ptr<domain::BotOperationGateway> gateway,
    std::shared_ptr<BridgeStateRepository> repository,
    std::shared_ptr<obcx::core::BlockingExecutor> blocking,
    BridgeDeliveryRequest request, std::int64_t completed_attempts,
    obcx::core::ActorGenerationLifecycle::TokenPtr token,
    obcx::core::MessageEnvelope failure_notification)
    -> boost::asio::awaitable<BridgeDeliveryResult> {
  auto association = correlation(request);
  // Sole provider call, only after durable Created admission. An exception or
  // persistence failure leaves the admitted attempt uncertain, never missing.
  domain::SendOutcome outcome = domain::SendFailure{
      {domain::BotOperationErrorCode::Cancelled,
       "delivery cancelled before invocation", std::nullopt, std::nullopt,
       false, domain::SubmissionSafety::DefinitelyNotSubmitted},
      {},
      {},
      false};
  // Keep cancellation observable, but do not let an awaitable entry check
  // discard an admitted no-send result or a returned physical outcome.
  const auto cancellation = co_await boost::asio::this_coro::cancellation_state;
  const auto previous = co_await boost::asio::this_coro::throw_if_cancelled();
  co_await boost::asio::this_coro::throw_if_cancelled(false);
  std::exception_ptr invocation_error;
  try {
    if (cancellation.cancelled() == boost::asio::cancellation_type::none &&
        (!token || token->valid()))
      outcome = co_await domain::BotClient{*gateway}.send(
          request.send, std::move(association));
  } catch (...) {
    // No returned outcome means the durable attempt stays unresolved. Never
    // persist the initial no-send placeholder as evidence for a thrown call.
    invocation_error = std::current_exception();
  }
  std::pair<ledger::AdmissionResult, std::optional<ledger::DeliverySnapshot>>
      persisted;
  bool persistence_failed = false;
  if (!invocation_error) {
    try {
      auto result = terminal(request, outcome);
      if (cancellation.cancelled() != boost::asio::cancellation_type::none ||
          (token && !token->valid()))
        result.retryable = false;
      const auto retry =
          result.retryable && request.retry_policy &&
                  completed_attempts < request.max_attempts
              ? std::optional{detail::prepare_delivery_retry(
                    request, completed_attempts, result.finished_at_ms)}
              : std::nullopt;
      persisted = co_await blocking->run([repository, id = request.delivery_id,
                                          attempt = request.attempt_id, retry,
                                          result = std::move(result)] {
        const auto written =
            repository->finish_attempt(id, attempt, result, retry);
        return std::pair{written, repository->load_delivery(id)};
      });
    } catch (...) {
      persistence_failed = true;
    }
  }
  co_await boost::asio::this_coro::throw_if_cancelled(previous);
  if (invocation_error)
    std::rethrow_exception(invocation_error);
  if (persistence_failed || !persisted.second)
    throw BridgeDeliveryPersistenceFailure(std::move(request.delivery_id),
                                           std::move(outcome),
                                           std::move(failure_notification));
  const bool publish =
      std::holds_alternative<domain::SendReceipt>(outcome) &&
      persisted.first == ledger::AdmissionResult::Created &&
      persisted.second->state == ledger::DeliveryState::Complete;
  co_return BridgeDeliveryResult{std::move(*persisted.second),
                                 std::move(outcome), publish};
}

auto bridge_delivery_output_messages(const ledger::DeliverySnapshot &snapshot)
    -> std::vector<domain::MessageRef> {
  if (!snapshot.admission)
    throw std::runtime_error("delivery has no bound scope");
  if (snapshot.state == ledger::DeliveryState::Complete &&
      (!snapshot.completeness_known || snapshot.outputs.empty()))
    throw std::runtime_error(
        "complete delivery lacks complete output evidence");
  std::vector<domain::MessageRef> messages;
  for (const auto &output : snapshot.outputs) {
    if (snapshot.state == ledger::DeliveryState::Complete &&
        output.ordinal != std::optional<std::int64_t>{
                              static_cast<std::int64_t>(messages.size())})
      throw std::runtime_error("complete delivery output order mismatch");
    auto message = decode_message(output.message_ref);
    if (encode("ConversationRef", message.conversation) !=
            snapshot.admission->target_scope ||
        std::ranges::find(messages, message) != messages.end())
      throw std::runtime_error("delivery output scope or identity conflict");
    messages.push_back(std::move(message));
  }
  return messages;
}
auto bridge_delivery_find_output(const ledger::DeliverySnapshot &snapshot,
                                 const domain::MessageRef &message)
    -> const ledger::PhysicalOutput & {
  message.validate();
  if (!snapshot.admission ||
      snapshot.admission->target_scope !=
          encode("ConversationRef", message.conversation))
    throw std::runtime_error("physical output lookup scope mismatch");
  const auto ref = encode("MessageRef", message);
  const ledger::PhysicalOutput *found = nullptr;
  const auto observe = [&](const auto &output) {
    if (output.message_ref != ref)
      return;
    if (found)
      throw std::runtime_error("ambiguous physical output history");
    found = &output;
  };
  for (const auto &output : snapshot.outputs)
    observe(output);
  for (const auto &revision : snapshot.revisions)
    for (const auto &output : revision.observed_outputs)
      observe(output);
  if (!found)
    throw std::runtime_error("physical output disappeared");
  return *found;
}
namespace {
auto latest_member_revision(const ledger::DeliverySnapshot &snapshot,
                            const ledger::OpaqueValue &source)
    -> const ledger::RevisionSnapshot * {
  const ledger::RevisionSnapshot *latest = nullptr;
  if (!snapshot.admission ||
      snapshot.revisions.size() > domain::detail::domain_maximum_items)
    throw std::runtime_error("invalid edit history scope or bound");
  for (const auto &revision : snapshot.revisions) {
    if (revision.admission.delivery_id != snapshot.delivery_id ||
        revision.admission.pair_id != snapshot.admission->pair_id ||
        revision.admission.source_scope != snapshot.admission->source_scope ||
        revision.admission.target_scope != snapshot.admission->target_scope)
      throw std::runtime_error("edit history scope mismatch");
    if (revision.admission.source_ref != source)
      continue;
    if (latest && latest->admission.native_edited_at_ms ==
                      revision.admission.native_edited_at_ms)
      throw std::runtime_error("ambiguous edit revision order");
    if (!latest || latest->admission.native_edited_at_ms <
                       revision.admission.native_edited_at_ms)
      latest = &revision;
  }
  return latest;
}
} // namespace

auto bridge_delivery_member_outputs(const ledger::DeliverySnapshot &snapshot,
                                    const domain::MessageRef &source)
    -> std::vector<domain::MessageRef> {
  source.validate();
  if (!snapshot.admission ||
      snapshot.admission->delivery_id != snapshot.delivery_id ||
      snapshot.admission->source_scope !=
          encode("ConversationRef", source.conversation) ||
      snapshot.members.empty() ||
      snapshot.members.size() > domain::detail::domain_maximum_items ||
      snapshot.outputs.size() > domain::detail::domain_maximum_items)
    throw std::runtime_error("invalid bridge member output selection scope");
  // Admission is immutable; late proven members live in the member rows.
  // Original members must remain identical, but additional rows are allowed
  // only as candidates: exact output provenance below establishes contribution.
  if (snapshot.admission->members.empty() ||
      snapshot.members.size() < snapshot.admission->members.size())
    throw std::runtime_error("bridge member set lost admitted members");
  std::map<std::string, const ledger::SourceMember *> admitted;
  for (const auto &member : snapshot.admission->members)
    if (!admitted.emplace(member.member_id, &member).second)
      throw std::runtime_error("duplicate admitted bridge member");
  std::map<std::string, std::set<std::string>> inputs;
  std::set<std::tuple<std::string, std::int64_t, std::string>> refs;
  std::optional<std::string> selected;
  const auto wanted = encode("MessageRef", source);
  for (const auto &member : snapshot.members) {
    const auto expected = admitted.find(member.member_id);
    const auto message = decode_message(member.message_ref);
    const auto canonical = encode("MessageRef", message);
    if (member.member_id.empty() || inputs.contains(member.member_id) ||
        !refs.emplace(canonical.codec, canonical.version, canonical.bytes)
             .second ||
        (expected != admitted.end() && *expected->second != member) ||
        message.conversation != source.conversation ||
        member.input_blocks.empty() ||
        member.input_blocks.size() > domain::detail::domain_maximum_items)
      throw std::runtime_error("conflicting bridge member evidence");
    auto &blocks = inputs[member.member_id];
    for (const auto &block : member.input_blocks)
      if (block.empty() || !blocks.insert(block).second)
        throw std::runtime_error("conflicting bridge member input identity");
    if (canonical == wanted)
      selected = member.member_id;
  }
  for (const auto &original : admitted)
    if (!inputs.contains(original.first))
      throw std::runtime_error("admitted bridge member disappeared");
  if (!selected) {
    if (std::ranges::any_of(snapshot.suppressed_album_members,
                            [&](const auto &entry) {
                              return entry.observation.message_ref == wanted;
                            }))
      throw std::runtime_error(
          "album association does not authorize output mutation");
    throw std::runtime_error("message is not an admitted delivery member");
  }
  if (const auto *revision = latest_member_revision(snapshot, wanted)) {
    if (revision->admission.member_id != *selected || !revision->checkpoint ||
        !revision->checkpoint->settled ||
        !revision->observations_match_checkpoint)
      throw std::runtime_error("member edit has unresolved effects");
    auto active = revision->admission.expected_outputs;
    std::erase_if(active, [&](const auto &ref) {
      return std::ranges::find(revision->checkpoint->deleted_outputs, ref) !=
             revision->checkpoint->deleted_outputs.end();
    });
    for (const auto &output : revision->checkpoint->outputs)
      active.push_back(output.message_ref);
    std::vector<domain::MessageRef> result;
    for (const auto &ref : active) {
      auto message = decode_message(ref);
      const auto &output = bridge_delivery_find_output(snapshot, message);
      if (!output.provenance || output.provenance->empty())
        throw std::runtime_error("edited member has unknown output provenance");
      const auto *allowed = &inputs.at(*selected);
      std::set<std::string> revision_inputs;
      for (const auto &owner : snapshot.revisions)
        if (std::ranges::any_of(owner.observed_outputs,
                                [&](const auto &candidate) {
                                  return candidate.message_ref == ref;
                                })) {
          if (owner.admission.source_ref != wanted ||
              owner.admission.member_id != *selected)
            throw std::runtime_error(
                "edited output has a different source owner");
          revision_inputs.insert(owner.admission.input_blocks.begin(),
                                 owner.admission.input_blocks.end());
          allowed = &revision_inputs;
        }
      for (const auto &origin : *output.provenance)
        if (origin.member_id != *selected ||
            !allowed->contains(origin.input_block) ||
            (output.reverse_primary_member &&
             *output.reverse_primary_member != *selected))
          throw std::runtime_error("edited output provenance conflicts");
      if (std::ranges::find(result, message) != result.end())
        throw std::runtime_error("duplicate edited member output");
      result.push_back(std::move(message));
    }
    return result;
  }
  const auto messages = bridge_delivery_output_messages(snapshot);
  std::vector<domain::MessageRef> result;
  for (std::size_t i = 0; i < snapshot.outputs.size(); ++i) {
    const auto &output = snapshot.outputs[i];
    if (!output.provenance || output.provenance->empty() ||
        output.provenance->size() > domain::detail::domain_maximum_items)
      throw std::runtime_error(
          "unknown output provenance forbids member mutation");
    bool includes_member = false;
    bool includes_other = false;
    for (const auto &origin : *output.provenance) {
      const auto member = inputs.find(origin.member_id);
      if (member == inputs.end() ||
          !member->second.contains(origin.input_block) ||
          (output.reverse_primary_member &&
           *output.reverse_primary_member != origin.member_id))
        throw std::runtime_error("conflicting output provenance");
      includes_member |= origin.member_id == *selected;
      includes_other |= origin.member_id != *selected;
    }
    if (includes_member && includes_other)
      throw std::runtime_error(
          "shared output requires reconstruction before member mutation");
    if (includes_member)
      result.push_back(messages[i]);
  }
  return result;
}

auto bridge_delivery_quote_anchor(const ledger::DeliverySnapshot &snapshot)
    -> std::optional<domain::MessageRef> {
  if (!snapshot.quote_anchor)
    return std::nullopt;
  auto anchor = decode_message(*snapshot.quote_anchor);
  const auto messages = bridge_delivery_output_messages(snapshot);
  if (std::ranges::find(messages, anchor) == messages.end())
    throw std::runtime_error("delivery quote anchor lacks confirmed output");
  return anchor;
}
auto bridge_delivery_member_quote_anchor(
    const ledger::DeliverySnapshot &snapshot, const domain::MessageRef &source)
    -> std::optional<domain::MessageRef> {
  source.validate();
  auto selected = source;
  auto wanted = encode("MessageRef", source);
  if (!snapshot.admission || snapshot.admission->source_scope !=
                                 encode("ConversationRef", source.conversation))
    throw std::runtime_error("quote source scope mismatch");
  const auto actual =
      std::ranges::count_if(snapshot.members, [&](const auto &member) {
        return member.message_ref == wanted;
      });
  const ledger::SuppressedAlbumMember *association = nullptr;
  for (const auto &member : snapshot.suppressed_album_members)
    if (member.observation.message_ref == wanted) {
      if (association || actual != 0)
        throw std::runtime_error("ambiguous album quote association");
      association = &member;
    }
  if (association) {
    const auto anchor =
        std::ranges::find_if(snapshot.members, [&](const auto &member) {
          return member.message_ref == association->anchor_member_ref;
        });
    if (anchor == snapshot.members.end() ||
        anchor->media_group_ref !=
            std::optional{association->observation.media_group_ref})
      throw std::runtime_error("album quote lacks admitted group anchor");
    selected = decode_message(association->anchor_member_ref);
    if (selected.conversation != source.conversation)
      throw std::runtime_error("album quote anchor scope mismatch");
    wanted = association->anchor_member_ref;
  }
  if (std::ranges::count_if(snapshot.members, [&](const auto &member) {
        return member.message_ref == wanted;
      }) != 1)
    throw std::runtime_error("quote source lacks unique delivery membership");
  // Group association selects a reply anchor only. Mutation selectors never
  // perform this substitution, and association chains are not followed.
  const auto *revision = latest_member_revision(snapshot, wanted);
  if (!revision)
    return bridge_delivery_quote_anchor(snapshot);
  const auto active = bridge_delivery_member_outputs(snapshot, selected);
  if (!revision->checkpoint || revision->checkpoint->outputs.empty())
    return std::nullopt; // Do not substitute a retained old-version output.
  auto anchor =
      decode_message(revision->checkpoint->outputs.front().message_ref);
  if (std::ranges::find(active, anchor) == active.end())
    throw std::runtime_error(
        "edit quote anchor lacks current confirmed output");
  return anchor;
}
auto is_bridge_delivery_output(BridgeStateRepository &repository,
                               const std::string &pair_id,
                               const domain::MessageRef &message) -> bool {
  message.validate();
  domain::OperationCorrelation{pair_id}.validate();
  const auto result = repository.resolve_observed_output(
      pair_id, encode("ConversationRef", message.conversation),
      encode("MessageRef", message));
  if (result.status == ledger::ResolutionStatus::Missing &&
      result.delivery_ids.empty())
    return false;
  if (result.status == ledger::ResolutionStatus::Unique &&
      result.delivery_ids.size() == 1)
    return true;
  throw std::runtime_error("ambiguous or corrupt bridge output observation");
}

auto resolve_bridge_delivery_source(
    BridgeStateRepository &repository, const std::string &pair_id,
    const domain::ConversationRef &original_scope,
    const domain::ReplyInfo &reply) -> std::optional<domain::MessageRef> {
  reply.validate();
  original_scope.validate();
  domain::OperationCorrelation{pair_id}.validate();
  if (reply.relation() != domain::ReplyRelation::SameConversation ||
      !reply.reference())
    throw std::invalid_argument(
        "bridge source attribution requires a same-conversation reply");
  const auto &output = reply.reference()->target();
  const auto owner = repository.resolve_reverse_primary(
      pair_id, encode("ConversationRef", original_scope),
      encode("ConversationRef", output.conversation),
      encode("MessageRef", output));
  if (owner.status == ledger::ResolutionStatus::Missing &&
      !owner.observed_output)
    return std::nullopt;
  if (owner.status != ledger::ResolutionStatus::Unique || !owner.primary_member)
    throw std::runtime_error("bridge output lacks unique source ownership");
  auto message = decode_message(owner.primary_member->message_ref);
  if (message.conversation != original_scope)
    throw std::runtime_error("bridge original source scope mismatch");
  return message;
}

auto resolve_bridge_delivery_reply(BridgeStateRepository &repository,
                                   const std::string &pair_id,
                                   const domain::ConversationRef &target,
                                   const domain::ReplyInfo &reply)
    -> std::optional<domain::MessageRef> {
  reply.validate();
  target.validate();
  if (reply.relation() != domain::ReplyRelation::SameConversation ||
      !reply.reference())
    return std::nullopt;
  const auto source_scope =
      encode("ConversationRef", reply.source().conversation);
  const auto target_scope = encode("ConversationRef", target);
  const auto quoted = encode("MessageRef", reply.reference()->target());
  const auto direct =
      repository.resolve_delivery(pair_id, source_scope, target_scope, quoted);
  const auto reverse = repository.resolve_reverse_primary(pair_id, target_scope,
                                                          source_scope, quoted);
  const auto valid = [](ledger::ResolutionStatus status) {
    return status == ledger::ResolutionStatus::Missing ||
           status == ledger::ResolutionStatus::Unique;
  };
  if (!valid(direct.status) || !valid(reverse.status) ||
      (direct.status == ledger::ResolutionStatus::Unique &&
       reverse.status == ledger::ResolutionStatus::Unique))
    throw std::runtime_error("ambiguous or corrupt bridge reply mapping");
  std::optional<domain::MessageRef> result;
  if (direct.status == ledger::ResolutionStatus::Unique) {
    if (direct.delivery_ids.size() != 1)
      throw std::runtime_error("invalid unique bridge reply mapping");
    const auto delivery = repository.load_delivery(direct.delivery_ids.front());
    if (!delivery)
      throw std::runtime_error("bridge reply delivery disappeared");
    result = bridge_delivery_member_quote_anchor(*delivery,
                                                 reply.reference()->target());
  } else if (reverse.status == ledger::ResolutionStatus::Unique) {
    if (!reverse.primary_member)
      throw std::runtime_error("bridge reply primary owner missing");
    result = decode_message(reverse.primary_member->message_ref);
  }
  if (result && result->conversation != target)
    throw std::runtime_error("bridge reply target scope mismatch");
  return result;
}

namespace {
struct MessageMutationPlan {
  std::string delivery_id;
  std::vector<domain::MessageRef> outputs;
  std::vector<std::vector<ledger::Provenance>> provenance;
  std::string source_member_id;
  bool original_source;
};
// Pure selection on the storage worker. The later operation admission still
// rechecks authority transactionally; this plan itself grants no mutation.
auto select_message_mutation_plan(
    const std::shared_ptr<BridgeStateRepository> &repository,
    const std::string &pair, const domain::ConversationRef &target,
    const domain::ReplyInfo &reply) -> std::optional<MessageMutationPlan> {
  using Plan = MessageMutationPlan;
  const auto source_scope =
      encode("ConversationRef", reply.source().conversation);
  const auto quoted = encode("MessageRef", reply.reference()->target());
  const auto direct = repository->resolve_delivery(
      pair, source_scope, encode("ConversationRef", target), quoted);
  const auto observed =
      repository->resolve_observed_output(pair, source_scope, quoted);
  const auto valid = [](ledger::ResolutionStatus status) {
    return status == ledger::ResolutionStatus::Missing ||
           status == ledger::ResolutionStatus::Unique;
  };
  if (!valid(direct.status) || !valid(observed.status) ||
      (direct.status == ledger::ResolutionStatus::Unique &&
       observed.status == ledger::ResolutionStatus::Unique))
    throw std::runtime_error("ambiguous or corrupt recall binding");
  if (observed.status == ledger::ResolutionStatus::Unique) {
    if (observed.delivery_ids.size() != 1)
      throw std::runtime_error("invalid observed recall binding");
    const auto original =
        resolve_bridge_delivery_source(*repository, pair, target, reply);
    const auto snapshot =
        repository->load_delivery(observed.delivery_ids.front());
    if (!original || !snapshot || !snapshot->admission ||
        snapshot->admission->pair_id != pair ||
        snapshot->admission->source_scope !=
            encode("ConversationRef", target) ||
        snapshot->admission->target_scope != source_scope)
      throw std::runtime_error("recall original source scope mismatch");
    const auto encoded = encode("MessageRef", *original);
    const auto matches = [&](const auto &member) {
      return member.message_ref == encoded;
    };
    if (std::ranges::count_if(snapshot->members, matches) != 1)
      throw std::runtime_error("recall original source member not unique");
    const auto member = std::ranges::find_if(snapshot->members, matches);
    return Plan{
        snapshot->delivery_id, {*original}, {}, member->member_id, true};
  }
  if (direct.status == ledger::ResolutionStatus::Missing) {
    if (!direct.delivery_ids.empty() || !observed.delivery_ids.empty())
      throw std::runtime_error("inconsistent missing recall binding");
    return std::nullopt;
  }
  if (direct.delivery_ids.size() != 1)
    throw std::runtime_error("invalid unique recall binding");
  const auto snapshot = repository->load_delivery(direct.delivery_ids.front());
  if (!snapshot || !snapshot->admission ||
      snapshot->admission->pair_id != pair ||
      snapshot->admission->source_scope != source_scope ||
      snapshot->admission->target_scope != encode("ConversationRef", target))
    throw std::runtime_error("recall admission scope mismatch");
  Plan result{
      snapshot->delivery_id,
      bridge_delivery_member_outputs(*snapshot, reply.reference()->target()),
      {},
      {},
      false};
  for (const auto &message : result.outputs) {
    const auto &output = bridge_delivery_find_output(*snapshot, message);
    if (!output.provenance)
      throw std::runtime_error("recall output provenance disappeared");
    result.provenance.push_back(*output.provenance);
  }
  return result;
}
} // namespace

auto recall_bridge_delivery(
    std::shared_ptr<BridgeCommandOperations> operations,
    std::shared_ptr<BridgeStateRepository> repository,
    std::shared_ptr<obcx::core::BlockingExecutor> blocking,
    domain::ReplyInfo reply, std::string qq_group_id)
    -> boost::asio::awaitable<std::optional<BridgeRecallResult>> {
  if (!operations || !repository || !blocking)
    throw std::invalid_argument("recall requires owned operations and storage");
  reply.validate();
  if (reply.relation() != domain::ReplyRelation::SameConversation ||
      !reply.reference() ||
      reply.source().conversation.installation !=
          operations->telegram_installation())
    throw std::invalid_argument("recall requires the original Telegram reply");
  const auto pair = operations->pair_id();
  const auto target = operations->onebot11_conversation(qq_group_id);
  const auto plan = co_await blocking->run([repository, pair, target, reply] {
    return select_message_mutation_plan(repository, pair, target, reply);
  });
  if (!plan)
    co_return std::nullopt;
  BridgeRecallResult result{{}, false, false, plan->original_source};
  if (plan->outputs.empty())
    co_return result;

  // Stable across output-set/order changes. A changed first target conflicts
  // with the same fence rather than turning replay into suffix continuation.
  const auto digest =
      calculate_hash(domain::Json::array({pair, plan->delivery_id,
                                          domain::Json(reply.source())})
                         .dump());
  if (digest.empty())
    throw std::runtime_error("recall identity hashing failed");
  const auto batch = "bridge-recall-v1:" + digest;
  using Operation =
      std::variant<ledger::OutputOperation, ledger::SourceOperation>;
  std::vector<Operation> pending;
  const auto codec = plan->original_source ? "BridgeRecallSourceState"
                                           : "BridgeRecallOutputState";
  std::vector<domain::BotOperationResult<domain::DeleteReceipt>> checkpoints;
  pending.reserve(plan->outputs.size());
  checkpoints.reserve(plan->outputs.size());
  result.targets.reserve(plan->outputs.size());
  for (std::size_t i = 0; i < plan->outputs.size(); ++i) {
    const auto &message = plan->outputs[i];
    if (message.conversation != target)
      throw std::runtime_error("recall physical target scope mismatch");
    const auto output_hash = calculate_hash(domain::Json(message).dump());
    if (output_hash.empty())
      throw std::runtime_error("recall output hashing failed");
    const auto id = i == 0 ? batch : batch + ":output:" + output_hash;
    domain::Json intent = {{"phase", "admitted"},
                           {"batch", batch},
                           {"command", reply.source()},
                           {"source", reply.reference()->target()},
                           {"target", message}};
    if (plan->original_source) {
      intent["source"] = message;
      intent["anchor"] = reply.reference()->target();
      intent["member_id"] = plan->source_member_id;
      pending.emplace_back(ledger::SourceOperation{
          id, plan->source_member_id, encode("MessageRef", message),
          encode("MessageRef", reply.reference()->target()),
          encode(codec, intent)});
    } else {
      if (i == 0)
        intent["planned_outputs"] = plan->outputs;
      pending.emplace_back(ledger::OutputOperation{
          id, encode("MessageRef", message), encode(codec, intent)});
    }
    checkpoints.push_back(domain::failed_operation<domain::DeleteReceipt>(
        domain::BotOperationErrorCode::OutcomeUnknown,
        "recall invocation outcome unavailable", false,
        domain::SubmissionSafety::PossiblySubmitted));
    result.targets.push_back({message, std::nullopt, false});
  }

  const auto cancellation = co_await boost::asio::this_coro::cancellation_state;
  const auto previous = co_await boost::asio::this_coro::throw_if_cancelled();
  co_await boost::asio::this_coro::throw_if_cancelled(false);
  try {
    for (std::size_t i = 0; i < pending.size(); ++i) {
      if (cancellation.cancelled() != boost::asio::cancellation_type::none) {
        result.interrupted = true;
        break;
      }
      const auto admitted = co_await blocking->run(
          [repository, id = plan->delivery_id, checkpoint = pending[i],
           provenance = plan->original_source
                            ? std::vector<ledger::Provenance>{}
                            : plan->provenance[i]] {
            return std::visit(
                [&](const auto &entry) {
                  if constexpr (std::is_same_v<std::decay_t<decltype(entry)>,
                                               ledger::SourceOperation>)
                    return repository->admit_source_operation(id, entry);
                  else
                    return repository->admit_output_operation(id, entry,
                                                              provenance);
                },
                checkpoint);
          });
      if (admitted != ledger::AdmissionResult::Created) {
        result.replayed = true;
        break;
      }
      auto &output = result.targets[i];
      output.outcome = std::move(checkpoints[i]);
      // The owning unknown checkpoint is installed before invocation. A throw
      // or failed finalization cannot discard an earlier physical receipt.
      if (cancellation.cancelled() != boost::asio::cancellation_type::none)
        *output.outcome = domain::failed_operation<domain::DeleteReceipt>(
            domain::BotOperationErrorCode::Cancelled,
            "recall cancelled before invocation", false,
            domain::SubmissionSafety::DefinitelyNotSubmitted);
      else
        *output.outcome =
            co_await operations->delete_onebot11_message(output.message);
      output.outcome->validate();
      const bool matched = output.outcome->ok() &&
                           output.outcome->value->message == output.message;
      domain::Json terminal = {{"phase", "finished"},
                               {"batch", batch},
                               {"command", reply.source()},
                               {"target", output.message},
                               {"source", reply.reference()->target()},
                               {"matched_receipt", matched},
                               {"outcome", *output.outcome}};
      if (plan->original_source) {
        terminal["source"] = output.message;
        terminal["anchor"] = reply.reference()->target();
        terminal["member_id"] = plan->source_member_id;
      } else if (i == 0) {
        terminal["planned_outputs"] = plan->outputs;
      }
      const auto completed = std::visit(
          [&](auto entry) -> Operation {
            entry.result = encode(codec, terminal);
            return entry;
          },
          pending[i]);
      (void)co_await blocking->run([repository, id = plan->delivery_id,
                                    checkpoint = pending[i], completed] {
        return std::visit(
            [&](const auto &entry) {
              using Entry = std::decay_t<decltype(entry)>;
              if constexpr (std::is_same_v<Entry, ledger::SourceOperation>)
                return repository->finish_source_operation(
                    id, entry, std::get<Entry>(completed));
              else
                return repository->finish_output_operation(
                    id, entry, std::get<Entry>(completed));
            },
            checkpoint);
      });
      output.persisted = true;
      if (!matched) {
        result.interrupted = true;
        // A definite rejection of one independent target does not prevent
        // the remaining targets being attempted. Unknown effects or a wrong
        // receipt stop this invocation; no earlier target is retried.
        if (output.outcome->value || !output.outcome->error ||
            output.outcome->error->code ==
                domain::BotOperationErrorCode::Cancelled ||
            output.outcome->error->submission_safety !=
                domain::SubmissionSafety::DefinitelyNotSubmitted)
          break;
      }
    }
  } catch (const std::exception &error) {
    result.cancelled = is_bridge_operation_cancelled(error);
    result.interrupted = true;
  } catch (...) {
    // Durable admission remains possible/unknown if no terminal write won.
    // Return all already-owned receipts; never re-enter the provider loop.
    result.interrupted = true;
  }
  result.cancelled |=
      cancellation.cancelled() != boost::asio::cancellation_type::none;
  co_await boost::asio::this_coro::throw_if_cancelled(previous);
  co_return result;
}

auto propagate_bridge_recall(
    std::shared_ptr<BridgeCommandOperations> operations,
    std::shared_ptr<BridgeStateRepository> repository,
    std::shared_ptr<obcx::core::BlockingExecutor> blocking,
    domain::MessageDeleted deleted, const bool delete_outputs,
    obcx::telegram::bot::FormattedText replacement)
    -> boost::asio::awaitable<std::optional<BridgeRecallPropagationResult>> {
  if (!operations || !repository || !blocking)
    throw std::invalid_argument("recall propagation requires owned storage");
  deleted.validate();
  if (deleted.message.conversation.installation !=
      operations->onebot11_installation())
    throw std::invalid_argument("recall observation installation mismatch");
  const auto pair = operations->pair_id();
  const auto source_scope =
      encode("ConversationRef", deleted.message.conversation);
  const auto source_ref = encode("MessageRef", deleted.message);
  const auto snapshot = co_await blocking->run(
      [repository, pair, source_scope,
       source_ref]() -> std::optional<ledger::DeliverySnapshot> {
        const auto binding =
            repository->resolve_source_delivery(pair, source_scope, source_ref);
        if (binding.status == ledger::ResolutionStatus::Missing &&
            binding.delivery_ids.empty())
          return std::nullopt;
        if (binding.status != ledger::ResolutionStatus::Unique ||
            binding.delivery_ids.size() != 1)
          throw std::runtime_error(
              "ambiguous or corrupt recall source binding");
        auto result = repository->load_delivery(binding.delivery_ids.front());
        if (!result || !result->admission ||
            result->admission->pair_id != pair ||
            result->admission->source_scope != source_scope)
          throw std::runtime_error("recall source admission mismatch");
        return result;
      });
  if (!snapshot)
    co_return std::nullopt;
  const auto outputs =
      bridge_delivery_member_outputs(*snapshot, deleted.message);
  BridgeRecallPropagationResult result{
      {}, false, false, snapshot->completeness_known};
  if (outputs.empty())
    co_return result;
  const auto digest = calculate_hash(
      domain::Json::array({pair, snapshot->delivery_id, deleted.message})
          .dump());
  if (digest.empty())
    throw std::runtime_error("recall notification hashing failed");
  const auto batch = "bridge-recall-notice-v1:" + digest;
  const std::string codec = "BridgeRecallNoticeOutputState";
  std::vector<ledger::OutputOperation> pending;
  std::vector<std::vector<ledger::Provenance>> provenance;
  std::vector<BridgeRecallPropagationOutcome> checkpoints;
  pending.reserve(outputs.size());
  provenance.reserve(outputs.size());
  checkpoints.reserve(outputs.size());
  result.targets.reserve(outputs.size());
  // Preflight every output and intent before the first durable admission.
  for (std::size_t i = 0; i < outputs.size(); ++i) {
    const auto &message = outputs[i];
    operations->validate_telegram_target(message.conversation);
    message.validate(); // Dispatch performs owning native-codec validation.
    if (!delete_outputs)
      obcx::telegram::bot::EditFormattedTextRequest{message, replacement}
          .validate();
    const auto &output = bridge_delivery_find_output(*snapshot, message);
    if (!output.provenance)
      throw std::runtime_error("recall output provenance disappeared");
    // editMessageText cannot edit an attachment/caption. Refuse a known media
    // output before editing any text prefix; do not silently switch methods.
    if (!delete_outputs && !output.reusable_media.empty())
      throw std::runtime_error(
          "Telegram media output cannot be edited as text");
    provenance.push_back(*output.provenance);
    const auto hash = calculate_hash(domain::Json(message).dump());
    if (hash.empty())
      throw std::runtime_error("recall output hashing failed");
    domain::Json intent = {
        {"phase", "admitted"},       {"batch", batch},
        {"source", deleted.message}, {"observed_at", deleted.received_at},
        {"target", message},         {"delete", delete_outputs}};
    if (!delete_outputs)
      intent["request"] =
          obcx::telegram::bot::EditFormattedTextRequest{message, replacement};
    if (i == 0)
      intent["planned_outputs"] = outputs;
    pending.push_back({i == 0 ? batch : batch + ":output:" + hash,
                       encode("MessageRef", message), encode(codec, intent)});
    const auto unknown = []<typename T>() {
      return domain::failed_operation<T>(
          domain::BotOperationErrorCode::OutcomeUnknown,
          "recall propagation outcome unavailable", false,
          domain::SubmissionSafety::PossiblySubmitted);
    };
    if (delete_outputs)
      checkpoints.emplace_back(
          unknown.template operator()<domain::DeleteReceipt>());
    else
      checkpoints.emplace_back(
          unknown.template operator()<obcx::telegram::bot::EditTextReceipt>());
    result.targets.push_back({message, std::nullopt, false, false});
  }
  const auto cancellation = co_await boost::asio::this_coro::cancellation_state;
  const auto previous = co_await boost::asio::this_coro::throw_if_cancelled();
  co_await boost::asio::this_coro::throw_if_cancelled(false);
  try {
    for (std::size_t i = 0; i < outputs.size(); ++i) {
      if (cancellation.cancelled() != boost::asio::cancellation_type::none) {
        result.interrupted = true;
        break;
      }
      const auto admitted = co_await blocking->run(
          [repository, id = snapshot->delivery_id, entry = pending[i],
           expected = provenance[i]] {
            return repository->admit_output_operation(id, entry, expected);
          });
      if (admitted != ledger::AdmissionResult::Created) {
        result.replayed = true;
        break;
      }
      auto &target = result.targets[i];
      target.outcome = std::move(checkpoints[i]);
      if (cancellation.cancelled() != boost::asio::cancellation_type::none) {
        if (delete_outputs)
          *target.outcome = domain::failed_operation<domain::DeleteReceipt>(
              domain::BotOperationErrorCode::Cancelled,
              "recall propagation cancelled before invocation", false,
              domain::SubmissionSafety::DefinitelyNotSubmitted);
        else
          *target.outcome =
              domain::failed_operation<obcx::telegram::bot::EditTextReceipt>(
                  domain::BotOperationErrorCode::Cancelled,
                  "recall propagation cancelled before invocation", false,
                  domain::SubmissionSafety::DefinitelyNotSubmitted);
      } else if (delete_outputs)
        *target.outcome =
            co_await operations->delete_telegram_output(target.message);
      else
        *target.outcome = co_await operations->edit_telegram_text(
            {target.message, replacement});
      target.matched_receipt = std::visit(
          [&](const auto &outcome) {
            outcome.validate();
            return outcome.ok() && outcome.value->message == target.message;
          },
          *target.outcome);
      domain::Json terminal = {
          {"phase", "finished"},
          {"batch", batch},
          {"source", deleted.message},
          {"observed_at", deleted.received_at},
          {"target", target.message},
          {"delete", delete_outputs},
          {"matched_receipt", target.matched_receipt},
          {"outcome",
           std::visit([](const auto &outcome) { return domain::Json(outcome); },
                      *target.outcome)}};
      if (!delete_outputs)
        terminal["request"] = obcx::telegram::bot::EditFormattedTextRequest{
            target.message, replacement};
      if (i == 0)
        terminal["planned_outputs"] = outputs;
      auto completed = pending[i];
      completed.result = encode(codec, terminal);
      (void)co_await blocking->run([repository, id = snapshot->delivery_id,
                                    initial = pending[i], completed] {
        return repository->finish_output_operation(id, initial, completed);
      });
      target.persisted = true;
      if (!target.matched_receipt) {
        result.interrupted = true;
        break;
      }
    }
  } catch (...) {
    // Keep owned receipts, the uncertain admission and the batch fence. No
    // automatic re-entry, suffix continuation or fallback to legacy mappings.
    result.interrupted = true;
  }
  co_await boost::asio::this_coro::throw_if_cancelled(previous);
  co_return result;
}

auto react_bridge_delivery(
    std::shared_ptr<BridgeCommandOperations> operations,
    std::shared_ptr<BridgeStateRepository> repository,
    std::shared_ptr<obcx::core::BlockingExecutor> blocking,
    domain::ReplyInfo reply, std::string qq_group_id, std::string emoji_id,
    const bool set)
    -> boost::asio::awaitable<std::optional<BridgeReactionResult>> {
  using Request = obcx::onebot11::bot::SetOneBotMessageEmojiLikeRequest;
  using Receipt = obcx::onebot11::bot::OneBotMessageEmojiLikeResult;
  if (!operations || !repository || !blocking)
    throw std::invalid_argument(
        "reaction requires owned operations and storage");
  reply.validate();
  if (reply.relation() != domain::ReplyRelation::SameConversation ||
      !reply.reference() ||
      reply.source().conversation.installation !=
          operations->telegram_installation())
    throw std::invalid_argument(
        "reaction requires the original Telegram reply");
  const auto pair = operations->pair_id();
  const auto target = operations->onebot11_conversation(qq_group_id);
  const auto command_ref = encode("MessageRef", reply.source());
  const auto digest = calculate_hash(
      domain::Json::array({pair, "qq-message-reaction", reply.source()})
          .dump());
  if (digest.empty())
    throw std::runtime_error("reaction command hashing failed");
  const auto command_id = "bridge-command-v1:" + digest;
  struct Selection {
    std::optional<MessageMutationPlan> plan;
    std::vector<Request> requests;
    domain::Json evidence;
    bool replayed = false;
  };
  const auto selection = co_await blocking->run([operations, repository, pair,
                                                 target, reply, command_ref,
                                                 command_id, emoji_id, set] {
    Selection result;
    if (const auto existing =
            repository->find_command_operation(pair, command_id)) {
      if (existing->command_ref != command_ref ||
          existing->kind != "qq-message-reaction")
        throw std::runtime_error("reaction command fence identity conflict");
      result.replayed = true;
      return result;
    }
    result.plan = select_message_mutation_plan(repository, pair, target, reply);
    result.evidence = {{"reply", reply},
                       {"v4_binding", result.plan.has_value()}};
    if (result.plan) {
      result.evidence["delivery_id"] = result.plan->delivery_id;
      result.evidence["original_source"] = result.plan->original_source;
      for (const auto &message : result.plan->outputs) {
        if (message.conversation != target)
          throw std::runtime_error("reaction target scope mismatch");
        Request request{message, emoji_id, set};
        request.validate();
        result.requests.push_back(std::move(request));
      }
      return result;
    }
    return result;
  });
  if (selection.replayed)
    co_return BridgeReactionResult{{}, true, false, false, false};
  if (!selection.plan)
    co_return std::nullopt;
  BridgeReactionResult result{
      {}, false, false, selection.plan->original_source, false};
  if (selection.requests.empty()) {
    result.interrupted = true;
    co_return result;
  }
  std::optional<ledger::SourceBinding> binding;
  if (selection.plan && selection.plan->original_source)
    binding = ledger::SourceBinding{
        selection.plan->delivery_id, selection.plan->source_member_id,
        encode("MessageRef", selection.requests.front().message),
        encode("MessageRef", reply.reference()->target())};
  const ledger::CommandOperation command{
      .pair_id = pair,
      .operation_id = command_id,
      .kind = "qq-message-reaction",
      .command_ref = command_ref,
      .source_scope = encode("ConversationRef", reply.source().conversation),
      .target_scope = encode("ConversationRef", target),
      .request = encode("BridgeReactionPlan", selection.requests),
      .evidence = encode("BridgeReactionEvidence", selection.evidence),
      .source_binding = binding,
      .outcome = std::nullopt};
  using Operation =
      std::variant<ledger::OutputOperation, ledger::SourceOperation>;
  std::vector<Operation> pending;
  std::vector<domain::BotOperationResult<Receipt>> checkpoints;
  const auto codec = result.original_source ? "BridgeReactionSourceState"
                                            : "BridgeReactionOutputState";
  for (const auto &request : selection.requests) {
    checkpoints.push_back(domain::failed_operation<Receipt>(
        domain::BotOperationErrorCode::OutcomeUnknown,
        "reaction invocation outcome unavailable", false,
        domain::SubmissionSafety::PossiblySubmitted));
    result.targets.push_back({request.message, std::nullopt, false, false});
    if (!selection.plan)
      continue;
    const auto hash = calculate_hash(domain::Json(request.message).dump());
    if (hash.empty())
      throw std::runtime_error("reaction target hashing failed");
    const auto id = command_id + ":target:" + hash;
    const auto intent = encode(codec, domain::Json{{"phase", "admitted"},
                                                   {"command", command_id},
                                                   {"request", request},
                                                   {"reply", reply}});
    if (result.original_source)
      pending.emplace_back(ledger::SourceOperation{
          id, selection.plan->source_member_id,
          encode("MessageRef", request.message),
          encode("MessageRef", reply.reference()->target()), intent});
    else
      pending.emplace_back(ledger::OutputOperation{
          id, encode("MessageRef", request.message), intent});
  }
  const auto cancellation = co_await boost::asio::this_coro::cancellation_state;
  const auto previous = co_await boost::asio::this_coro::throw_if_cancelled();
  co_await boost::asio::this_coro::throw_if_cancelled(false);
  bool admitted = false;
  try {
    if (cancellation.cancelled() == boost::asio::cancellation_type::none) {
      admitted = ledger::AdmissionResult::Created ==
                 co_await blocking->run([repository, command] {
                   return repository->admit_command_operation(command);
                 });
      result.replayed = !admitted;
    } else
      result.interrupted = true;
    if (admitted) {
      for (std::size_t i = 0; i < selection.requests.size(); ++i) {
        if (cancellation.cancelled() != boost::asio::cancellation_type::none) {
          result.interrupted = true;
          break;
        }
        if (selection.plan) {
          const auto ready = co_await blocking->run(
              [repository, id = selection.plan->delivery_id, entry = pending[i],
               provenance = result.original_source
                                ? std::vector<ledger::Provenance>{}
                                : selection.plan->provenance[i]] {
                return std::visit(
                    [&](const auto &operation) {
                      if constexpr (std::is_same_v<
                                        std::decay_t<decltype(operation)>,
                                        ledger::SourceOperation>)
                        return repository->admit_source_operation(id,
                                                                  operation);
                      else
                        return repository->admit_output_operation(id, operation,
                                                                  provenance);
                    },
                    entry);
              });
          if (ready != ledger::AdmissionResult::Created) {
            result.replayed = true;
            result.interrupted = true;
            break;
          }
        }
        auto &item = result.targets[i];
        item.outcome = std::move(checkpoints[i]);
        if (cancellation.cancelled() != boost::asio::cancellation_type::none)
          *item.outcome = domain::failed_operation<Receipt>(
              domain::BotOperationErrorCode::Cancelled,
              "reaction cancelled before invocation", false,
              domain::SubmissionSafety::DefinitelyNotSubmitted);
        else
          *item.outcome = co_await operations->set_onebot11_message_emoji_like(
              selection.requests[i]);
        item.outcome->validate();
        const bool matched = item.outcome->ok() &&
                             item.outcome->value->message == item.message &&
                             item.outcome->value->emoji_id == emoji_id &&
                             item.outcome->value->set == set;
        item.matched_receipt = matched;
        if (selection.plan) {
          const auto terminal =
              encode(codec, domain::Json{{"phase", "finished"},
                                         {"command", command_id},
                                         {"request", selection.requests[i]},
                                         {"reply", reply},
                                         {"matched_receipt", matched},
                                         {"outcome", *item.outcome}});
          const auto completed = std::visit(
              [&](auto operation) -> Operation {
                operation.result = terminal;
                return operation;
              },
              pending[i]);
          (void)co_await blocking->run([repository,
                                        id = selection.plan->delivery_id,
                                        entry = pending[i], completed] {
            return std::visit(
                [&](const auto &operation) {
                  using Entry = std::decay_t<decltype(operation)>;
                  if constexpr (std::is_same_v<Entry, ledger::SourceOperation>)
                    return repository->finish_source_operation(
                        id, operation, std::get<Entry>(completed));
                  else
                    return repository->finish_output_operation(
                        id, operation, std::get<Entry>(completed));
                },
                entry);
          });
          item.persisted = true;
        }
        if (!matched) {
          result.interrupted = true;
          if (item.outcome->value || !item.outcome->error ||
              item.outcome->error->code ==
                  domain::BotOperationErrorCode::Cancelled ||
              item.outcome->error->submission_safety !=
                  domain::SubmissionSafety::DefinitelyNotSubmitted)
            break;
        }
      }
    }
  } catch (const std::exception &error) {
    result.cancelled = is_bridge_operation_cancelled(error);
    result.interrupted = true;
  } catch (...) {
    result.interrupted = true;
  }
  if (admitted) {
    try {
      domain::Json targets = domain::Json::array();
      for (const auto &item : result.targets)
        targets.push_back(
            {{"message", item.message},
             {"outcome", item.outcome ? domain::Json(*item.outcome)
                                      : domain::Json(nullptr)},
             {"child_journal_persisted", item.persisted},
             {"matched_receipt", item.matched_receipt}});
      auto completed = command;
      completed.outcome =
          encode("BridgeReactionOutcome",
                 domain::Json{{"targets", targets},
                              {"interrupted", result.interrupted},
                              {"replayed", result.replayed}});
      (void)co_await blocking->run([repository, command, completed] {
        return repository->finish_command_operation(command, completed);
      });
      result.command_persisted = true;
    } catch (const std::exception &error) {
      result.cancelled |= is_bridge_operation_cancelled(error);
      result.interrupted = true;
    } catch (...) {
      // Keep every owned receipt even if the child or aggregate write fails.
      result.interrupted = true;
    }
  }
  result.cancelled |=
      cancellation.cancelled() != boost::asio::cancellation_type::none;
  co_await boost::asio::this_coro::throw_if_cancelled(previous);
  co_return result;
}

auto is_bridge_delivery_confirmation(
    const obcx::bot::SendConfirmation &confirmation) -> bool {
  return confirmation.attempt_id.starts_with(correlation_prefix) ||
         confirmation.attempt_id.starts_with(revision_correlation_prefix);
}
auto record_bridge_delivery_confirmation(
    BridgeStateRepository &repository,
    const obcx::bot::SendConfirmation &confirmation) -> bool {
  if (!is_bridge_delivery_confirmation(confirmation))
    return false;
  confirmation.validate();
  domain::OperationCorrelation{confirmation.attempt_id}.validate();
  if (confirmation.attempt_id.starts_with(revision_correlation_prefix)) {
    const auto suffix =
        confirmation.attempt_id.substr(revision_correlation_prefix.size());
    const auto ids = domain::Json::parse(suffix);
    if (!ids.is_array() || ids.size() != 3 || !ids[0].is_string() ||
        !ids[1].is_string() || !ids[2].is_string() || ids.dump() != suffix)
      throw std::invalid_argument("noncanonical bridge edit correlation");
    const auto id = ids[0].get<std::string>();
    const auto revision_id = ids[1].get<std::string>();
    const auto attempt_id = ids[2].get<std::string>();
    for (const auto &value : {id, revision_id, attempt_id})
      domain::OperationCorrelation{value}.validate();
    const auto snapshot = repository.load_delivery(id);
    if (!snapshot || !snapshot->admission)
      return false;
    const auto revision =
        std::ranges::find_if(snapshot->revisions, [&](const auto &value) {
          return value.admission.revision_id == revision_id;
        });
    if (revision == snapshot->revisions.end() || !revision->checkpoint ||
        std::ranges::find(revision->checkpoint->send_attempt_ids, attempt_id) ==
            revision->checkpoint->send_attempt_ids.end())
      return false;
    const auto scope =
        encode("ConversationRef", confirmation.output.message.conversation);
    if (scope != revision->admission.target_scope)
      throw std::invalid_argument("edit confirmation target mismatch");
    repository.record_revision_confirmation(
        id, revision_id, attempt_id, scope,
        physical(confirmation.output, std::nullopt, nullptr));
    return true;
  }
  const auto suffix = confirmation.attempt_id.substr(correlation_prefix.size());
  const auto ids = domain::Json::parse(suffix);
  if (!ids.is_array() || ids.size() != 2 || !ids[0].is_string() ||
      !ids[1].is_string() || ids.dump() != suffix)
    throw std::invalid_argument("noncanonical bridge delivery correlation");
  const auto id = ids[0].get<std::string>(),
             attempt = ids[1].get<std::string>();
  domain::OperationCorrelation{id}.validate();
  domain::OperationCorrelation{attempt}.validate();
  const auto snapshot = repository.load_delivery(id);
  if (!snapshot || !snapshot->admission)
    return false;
  if (std::ranges::none_of(snapshot->attempts, [&](const auto &v) {
        return v.attempt_id == attempt;
      }))
    return false;
  if (snapshot->admission->target_scope !=
      encode("ConversationRef", confirmation.output.message.conversation))
    throw std::invalid_argument(
        "confirmation target differs from admitted target");
  repository.record_confirmation(
      id, attempt, physical(confirmation.output, std::nullopt, nullptr));
  return true;
}
} // namespace bridge
