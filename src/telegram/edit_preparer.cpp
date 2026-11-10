#include "bridge_bot_failures.hpp"
#include "bridge_hash.hpp"
#include "bridge_state_repository.hpp"
#include "telegram/ordinary_forwarder.hpp"
#include <telegram/bot/domain_views.hpp>

#include <algorithm>
#include <stdexcept>

namespace bridge::telegram {
namespace {
namespace domain = obcx::bot;
template <class T> auto require(domain::BotOperationResult<T> value) -> T {
  value.validate();
  if (!value.ok())
    throw BridgeBotOperationFailure(std::move(*value.error));
  return std::move(*value.value);
}
template <class T>
auto encode(std::string codec, const T &value) -> ledger::OpaqueValue {
  const domain::Json json = value;
  domain::detail::validate_domain_wire(json);
  auto bytes = domain::Json::to_cbor(json);
  if (bytes.size() > domain::detail::domain_maximum_bytes)
    throw domain::detail::DomainBudgetExceeded{};
  const auto version = codec == "bridge.EditIntent.cbor" ? 2 : 1;
  return {std::move(codec), version, {bytes.begin(), bytes.end()}};
}
auto confirmed_output(const ledger::DeliverySnapshot &snapshot,
                      const ledger::OpaqueValue &ref)
    -> const ledger::PhysicalOutput & {
  const ledger::PhysicalOutput *found = nullptr;
  const auto observe = [&](const ledger::PhysicalOutput &output) {
    if (output.message_ref == ref) {
      if (found)
        throw std::runtime_error("ambiguous edit predecessor output");
      found = &output;
    }
  };
  for (const auto &output : snapshot.outputs)
    observe(output);
  for (const auto &revision : snapshot.revisions)
    for (const auto &output : revision.observed_outputs)
      observe(output);
  if (!found)
    throw std::runtime_error("edit predecessor output disappeared");
  return *found;
}
} // namespace

auto OrdinaryForwarder::prepare_edit(
    obcx::message_store::events::MessageStored stored,
    BridgeDeliveryPublication publication)
    -> boost::asio::awaitable<EditPreparation> {
  if (!obcx::telegram::bot::message_update_view(stored.message))
    throw std::invalid_argument(
        "edit preparation requires original typed update");
  auto resolved = resolve_source(stored);
  if (!resolved)
    co_return EditPreparation{std::monostate{}};
  if (const auto reason = body_rejection_reason(stored.message, true))
    throw std::invalid_argument("edit contains unsupported content or a "
                                "non-unique control observation: " +
                                *reason);
  if (publication.event_id.empty() ||
      publication.source_bot != stored.source.source_bot)
    throw std::invalid_argument("edit lacks original publication identity");
  const auto observation =
      obcx::telegram::bot::message_update_observation(stored.message);
  if (!observation || observation->source != stored.message.message ||
      !observation->edited_at)
    throw std::runtime_error("edit lacks original revision time evidence");
  const auto source_ref =
      encode("obcx.bot.MessageRef.cbor", stored.message.message);
  const auto source_scope = encode("obcx.bot.ConversationRef.cbor",
                                   stored.message.message.conversation);
  const auto target_scope =
      encode("obcx.bot.ConversationRef.cbor", resolved->route.target);
  auto original = co_await blocking_->run(
      [repository = repository_, pair = pair_id_, source_ref, source_scope,
       target_scope,
       observed =
           stored.message.message] -> std::optional<ledger::DeliverySnapshot> {
        if (is_bridge_delivery_output(*repository, pair, observed))
          return std::nullopt;
        const auto binding = repository->resolve_delivery(
            pair, source_scope, target_scope, source_ref);
        if (binding.status == ledger::ResolutionStatus::Missing &&
            binding.delivery_ids.empty()) {
          return std::nullopt; // Missing mapping never authorizes a first send.
        }
        if (binding.status != ledger::ResolutionStatus::Unique ||
            binding.delivery_ids.size() != 1)
          throw std::runtime_error("ambiguous or corrupt edit source binding");
        auto snapshot = repository->load_delivery(binding.delivery_ids.front());
        if (!snapshot || !snapshot->admission ||
            snapshot->admission->pair_id != pair ||
            snapshot->admission->source_scope != source_scope ||
            snapshot->admission->target_scope != target_scope)
          throw std::runtime_error("edit delivery scope changed");
        return snapshot;
      });
  if (!original)
    co_return EditPreparation{std::monostate{}};
  if (!original->completeness_known ||
      original->state != ledger::DeliveryState::Complete)
    throw std::runtime_error(
        "edit original delivery is incomplete or uncertain");
  const ledger::SourceMember *member = nullptr;
  for (const auto &candidate : original->members)
    if (candidate.message_ref == source_ref) {
      if (member)
        throw std::runtime_error("edit source membership is ambiguous");
      member = &candidate;
    }
  if (!member) {
    if (std::ranges::any_of(
            original->suppressed_album_members, [&](const auto &entry) {
              return entry.observation.message_ref == source_ref;
            }))
      throw std::runtime_error(
          "observed album member has no confirmed content contribution");
    throw std::runtime_error("edit source member missing");
  }
  const auto group = stored.message.media_group
                         ? std::optional{encode("obcx.bot.MediaGroupRef.cbor",
                                                *stored.message.media_group)}
                         : std::nullopt;
  if (group != member->media_group_ref)
    throw std::runtime_error("edit original album membership changed");
  const auto digest = calculate_hash(
      domain::Json::array({pair_id_, original->delivery_id,
                           domain::Json(stored.message.message),
                           observation->edited_at->unix_milliseconds,
                           observation->content_fingerprint})
          .dump());
  if (digest.empty())
    throw std::runtime_error("edit revision identity hashing failed");
  const auto revision_id = "tg-edit-v1:" + digest;
  const ledger::RevisionSnapshot *head = nullptr;
  for (const auto &revision : original->revisions) {
    if (revision.admission.member_id != member->member_id)
      continue;
    if (revision.admission.source_ref != source_ref)
      throw std::runtime_error("edit revision source evidence changed");
    if (revision.admission.revision_id == revision_id) {
      if (revision.admission.content_fingerprint !=
              observation->content_fingerprint ||
          revision.admission.native_edited_at_ms !=
              observation->edited_at->unix_milliseconds)
        throw std::runtime_error("edit revision identity collision");
      co_return EditPreparation{
          revision}; // Never prepare or replay its effects.
    }
    if (!head || head->admission.native_edited_at_ms <
                     revision.admission.native_edited_at_ms)
      head = &revision;
  }
  std::vector<ledger::OpaqueValue> expected_outputs;
  if (head) {
    if (observation->edited_at->unix_milliseconds <
        head->admission.native_edited_at_ms)
      co_return EditPreparation{std::monostate{}};
    if (observation->edited_at->unix_milliseconds ==
        head->admission.native_edited_at_ms)
      throw std::runtime_error("distinct edits lack provable native ordering");
    if (!head->checkpoint ||
        head->checkpoint->phase != ledger::RevisionPhase::Finished ||
        !head->checkpoint->settled || !head->observations_match_checkpoint)
      throw std::runtime_error(
          "previous edit has uncertain or incomplete effects");
    for (const auto &ref : head->admission.expected_outputs)
      if (std::ranges::find(head->checkpoint->deleted_outputs, ref) ==
          head->checkpoint->deleted_outputs.end())
        expected_outputs.push_back(ref);
    for (const auto &output : head->checkpoint->outputs)
      expected_outputs.push_back(output.message_ref);
  } else {
    // This existing selector rejects unknown/shared outputs before acquisition.
    // Replacing a single album member must never delete the other members.
    for (const auto &ref :
         bridge_delivery_member_outputs(*original, stored.message.message))
      expected_outputs.push_back(encode("obcx.bot.MessageRef.cbor", ref));
    if (expected_outputs.empty())
      throw std::runtime_error(
          "edit has no exclusively attributed original output");
  }
  // Validate every persisted physical scope before any media acquisition,
  // including predecessors originating in earlier revisions.
  (void)bridge_delivery_output_messages(*original);
  for (const auto &revision : original->revisions)
    (void)bridge_revision_output_messages(revision);
  for (const auto &ref : expected_outputs) {
    const auto &output = confirmed_output(*original, ref);
    if (!output.provenance || output.provenance->empty() ||
        std::ranges::any_of(*output.provenance, [&](const auto &origin) {
          return origin.member_id != member->member_id;
        }))
      throw std::runtime_error(
          "edit predecessor provenance is unknown or shared");
  }
  std::vector<ledger::OutputOperation> operations;
  for (const auto &operation : original->output_operations)
    if (std::ranges::find(expected_outputs, operation.message_ref) !=
        expected_outputs.end())
      operations.push_back(operation);
  // Validate every existing mutation before acquiring any media. Successful
  // prior deletes are accounted separately by the executor, never invoked
  // again.
  (void)reconcile_revision_mutations(operations, expected_outputs);

  const auto route = resolved->route; // Own configuration before suspension.
  const auto capabilities = require(
      co_await domain::BotClient{*gateway_}.capabilities({route.target}));
  if (capabilities.conversation != route.target)
    throw std::runtime_error("edit capability scope mismatch");
  check_media_capabilities(stored.message, capabilities);
  const auto media_count = static_cast<std::size_t>(
      std::ranges::count_if(stored.message.body, [](const auto &block) {
        return std::holds_alternative<domain::ReceivedMedia>(block);
      }));
  domain::SendLimits limits{1, 0, 0};
  if (media_count) {
    const auto item = config_->qq_media_download_max_bytes;
    if (!item || item > domain::detail::domain_maximum_bytes)
      throw std::invalid_argument("invalid configured edit media byte budget");
    limits = {1, item,
              media_count > domain::detail::domain_maximum_bytes / item
                  ? domain::detail::domain_maximum_bytes
                  : media_count * item};
  }
  auto prepared = co_await prepare_member(
      stored, std::move(*resolved), member->member_id, true, false, true,
      limits, AttachmentFailurePolicy::FailClosed);
  if (prepared.body.empty())
    throw std::runtime_error(
        "edited source prepared no outbound content: no forwardable content");
  BridgeDeliveryRequest request{
      original->delivery_id,
      revision_id + ":send:1",
      pair_id_,
      {std::move(prepared.member)},
      {route.target, std::move(prepared.body), std::move(prepared.reply),
       domain::DeliveryPolicy::SingleMessageOnly, limits},
      std::move(prepared.origins),
      BridgeQuoteAnchor::FirstConfirmed,
      stored.message.received_at.unix_milliseconds,
      config_->enable_retry_queue ? config_->message_retry_max_attempts : 1,
      std::nullopt,
      config_->enable_retry_queue && config_->message_retry_max_attempts > 1
          ? std::optional{BridgeDeliveryRetryPolicy{
                config_->message_retry_base_interval_sec,
                config_->max_retry_interval_sec}}
          : std::nullopt,
      std::move(publication)};
  request.send.validate();
  // This is a revision intent, never a second first-submission admission.
  // The immutable journal retains only the current typed
  // observation/publication.
  auto origins = domain::Json::array();
  for (const auto &block : request.block_origins) {
    if (!block) {
      origins.push_back(nullptr);
      continue;
    }
    auto contributions = domain::Json::array();
    for (const auto &origin : *block)
      contributions.push_back({{"member_id", origin.member_id},
                               {"input_block", origin.input_block}});
    origins.push_back(std::move(contributions));
  }
  const domain::Json policy =
      request.retry_policy
          ? domain::Json{{"base_interval_sec",
                          request.retry_policy->base_interval_sec},
                         {"max_interval_sec",
                          request.retry_policy->max_interval_sec}}
          : domain::Json(nullptr);
  auto intent =
      encode("bridge.EditIntent.cbor",
             domain::Json{
                 {"delivery_id", request.delivery_id},
                 {"revision_id", revision_id},
                 {"attempt_id", request.attempt_id},
                 {"pair_id", request.pair_id},
                 {"member_id", member->member_id},
                 {"original_message", stored.message},
                 {"send", request.send},
                 {"block_origins", std::move(origins)},
                 {"input_blocks", request.members.front().input_blocks},
                 {"quote_anchor", static_cast<unsigned>(request.quote_anchor)},
                 {"received_at_ms", request.admitted_at_ms},
                 {"max_attempts", request.max_attempts},
                 {"retry_policy", policy},
                 {"publication",
                  {{"event_id", request.publication.event_id},
                   {"correlation_id", request.publication.correlation_id},
                   {"source_bot", request.publication.source_bot}}}});
  ledger::RevisionAdmission admission{
      original->delivery_id,
      pair_id_,
      source_scope,
      target_scope,
      revision_id,
      member->member_id,
      source_ref,
      observation->edited_at->unix_milliseconds,
      observation->content_fingerprint,
      head ? std::optional{head->admission.revision_id} : std::nullopt,
      std::move(expected_outputs),
      request.members.front().input_blocks,
      std::move(operations),
      std::move(intent)};
  co_return EditPreparation{
      PreparedEdit{std::move(request), std::move(admission)}};
}
auto OrdinaryForwarder::forward_edit(
    obcx::message_store::events::MessageStored stored,
    BridgeDeliveryPublication publication)
    -> boost::asio::awaitable<BridgeForwardResult> {
  auto preparation = co_await prepare_edit(stored, std::move(publication));
  BridgeForwardResult result;
  if (auto *replay = std::get_if<ledger::RevisionSnapshot>(&preparation)) {
    result.domain_revision = BridgeRevisionResult{
        std::move(*replay), {}, std::nullopt, false, false};
  } else if (auto *prepared = std::get_if<PreparedEdit>(&preparation)) {
    result.domain_revision = co_await send_bridge_revision(
        gateway_, repository_, blocking_, std::move(prepared->request),
        std::move(prepared->admission));
  }
  co_return result;
}
} // namespace bridge::telegram
