#include "bridge_delivery_revision.hpp"
#include "bridge_delivery_sender.hpp"
#include <algorithm>

#include <chrono>
#include <stdexcept>

namespace bridge {
namespace {
namespace d = obcx::bot;
auto document(const ledger::OpaqueValue &value, std::string_view codec)
    -> d::Json {
  if (value.codec != codec ||
      value.version != ((codec == "bridge.EditIntent.cbor" ||
                         codec == "obcx.bot.BridgeDeliveryPolicy.cbor")
                            ? 2
                            : 1) ||
      value.bytes.empty() || value.bytes.size() > d::detail::bot_maximum_bytes)
    throw std::runtime_error("unsupported bridge publication evidence");
  auto result = d::Json::from_cbor(value.bytes.begin(), value.bytes.end());
  d::detail::validate_bot_wire(result);
  const auto canonical = d::Json::to_cbor(result);
  if (std::string(canonical.begin(), canonical.end()) != value.bytes)
    throw std::runtime_error("noncanonical bridge publication evidence");
  return result;
}
auto platform(const d::ConversationRef &scope) -> std::string {
  scope.validate();
  if (scope.installation.surface == d::SurfaceId{"onebot11.qq"})
    return "qq";
  if (scope.installation.surface == d::SurfaceId{"telegram.bot_api"})
    return "telegram";
  throw std::invalid_argument("unsupported Bridge publication surface");
}
auto failure_context(const BridgeDeliveryPublication &publication,
                     const d::MessageRef &source,
                     const d::ConversationRef &target, std::string identity,
                     std::string code, std::string message)
    -> obcx::core::MessageEnvelope {
  source.validate();
  target.validate();
  const auto &scope = source.conversation;
  if (publication.event_id.empty() ||
      publication.source_bot != scope.installation.installation_id)
    throw std::invalid_argument("failure publication source mismatch");
  obcx::core::MessageEnvelope notification;
  notification.id = "forward-failed-" + publication.event_id + ":" + identity;
  notification.type = "bridge::events::MessageForwardFailed";
  notification.source_platform = platform(scope);
  notification.source_bot = scope.installation.installation_id;
  notification.conversation_id = scope.id.serialized();
  notification.correlation_id = publication.correlation_id;
  notification.causation_id = publication.event_id;
  notification.timestamp = std::chrono::system_clock::now();
  notification.payload = {{"code", std::move(code)},
                          {"message", std::move(message)},
                          {"retryable", false},
                          {"schema", 2},
                          {"source_message", source},
                          {"source_conversation", scope},
                          {"target_conversation", target},
                          {"outcome_persistence", "unconfirmed"}};
  return notification;
}
} // namespace

auto detail::prepare_delivery_failure_notification(
    const ledger::Admission &admission, const std::string &attempt_id)
    -> obcx::core::MessageEnvelope {
  if (admission.members.empty())
    throw std::invalid_argument("failure publication lacks source members");
  const auto policy = document(admission.policy_and_limits,
                               "obcx.bot.BridgeDeliveryPolicy.cbor");
  const auto &publication = policy.at("publication");
  const auto target =
      document(admission.target_scope, "obcx.bot.ConversationRef.cbor")
          .get<d::ConversationRef>();
  const auto scope =
      document(admission.source_scope, "obcx.bot.ConversationRef.cbor")
          .get<d::ConversationRef>();
  auto members = d::Json::array();
  for (const auto &member : admission.members) {
    const auto message =
        document(member.message_ref, "obcx.bot.MessageRef.cbor")
            .get<d::MessageRef>();
    if (message.conversation != scope)
      throw std::runtime_error("failure publication member scope mismatch");
    members.push_back(
        {{"member_id", member.member_id}, {"message_ref", message}});
  }
  const auto &source = members.front();
  auto notification =
      failure_context({publication.at("event_id").get<std::string>(),
                       publication.at("correlation_id").get<std::string>(),
                       publication.at("source_bot").get<std::string>()},
                      source.at("message_ref").get<d::MessageRef>(), target,
                      attempt_id, "bridge_delivery_persistence_failed",
                      "delivery outcome persistence failed; do not resubmit");
  notification.payload["delivery_id"] = admission.delivery_id;
  notification.payload["attempt_id"] = attempt_id;
  if (policy.contains("album_observations")) {
    const auto &trace = policy.at("album_observations");
    if (!trace.is_array() || trace.size() != admission.members.size())
      throw std::runtime_error("failure publication album trace mismatch");
    auto publications = d::Json::array();
    for (std::size_t i = 0; i < trace.size(); ++i) {
      const auto &entry = trace.at(i);
      if (entry.at("member_id") != admission.members[i].member_id)
        throw std::runtime_error("failure publication album member mismatch");
      publications.push_back({{"event_id", entry.at("event_id")},
                              {"correlation_id", entry.at("correlation_id")},
                              {"source_bot", entry.at("source_bot")},
                              {"message", members.at(i).at("message_ref")}});
    }
    notification.payload["source_publications"] = std::move(publications);
  }
  notification.payload["source_members"] = std::move(members);
  return notification;
}

auto bridge_delivery_failure_notification(
    const BridgeDeliveryPersistenceFailure &failure)
    -> obcx::core::MessageEnvelope {
  auto notification = failure.notification_context();
  notification.timestamp = std::chrono::system_clock::now();
  notification.payload["send_outcome"] = failure.outcome();
  return notification;
}

auto bridge_revision_failure_notification(const BridgeRevisionResult &result)
    -> obcx::core::MessageEnvelope {
  const auto &admission = result.persisted.admission;
  const auto intent = document(admission.intent, "bridge.EditIntent.cbor");
  const auto &publication = intent.at("publication");
  const auto source = document(admission.source_ref, "obcx.bot.MessageRef.cbor")
                          .get<d::MessageRef>();
  const auto target =
      document(admission.target_scope, "obcx.bot.ConversationRef.cbor")
          .get<d::ConversationRef>();
  auto notification = failure_context(
      {publication.at("event_id").get<std::string>(),
       publication.at("correlation_id").get<std::string>(),
       publication.at("source_bot").get<std::string>()},
      source, target, admission.revision_id, "bridge_edit_persistence_failed",
      "edit outcome persistence failed; do not resubmit");
  notification.payload["delivery_id"] = admission.delivery_id;
  notification.payload["revision_id"] = admission.revision_id;
  notification.payload["delete_outcomes"] = result.deletions;
  notification.payload["prior_deletion_operations"] =
      result.prior_deletion_operations;
  notification.payload["send_outcome"] =
      result.outcome ? d::Json(*result.outcome) : d::Json(nullptr);
  notification.payload["interrupted"] = result.interrupted;
  if (result.persisted.checkpoint &&
      !result.persisted.checkpoint->send_attempt_ids.empty())
    notification.payload["attempt_id"] =
        result.persisted.checkpoint->send_attempt_ids.back();
  return notification;
}

auto bridge_delivery_notification(const BridgeDeliveryResult &result)
    -> obcx::core::MessageEnvelope {
  const auto &snapshot = result.persisted;
  if (!result.newly_completed ||
      snapshot.state != ledger::DeliveryState::Complete ||
      !snapshot.admission || snapshot.admission->members.empty() ||
      !result.outcome ||
      !std::holds_alternative<d::SendReceipt>(*result.outcome))
    throw std::invalid_argument(
        "publication requires newly persisted complete delivery");
  const auto &receipt = std::get<d::SendReceipt>(*result.outcome);
  receipt.validate();
  const auto outputs = bridge_delivery_output_messages(snapshot);
  if (outputs != receipt.messages)
    throw std::runtime_error(
        "publication output set disagrees with complete receipt");
  const auto policy = document(snapshot.admission->policy_and_limits,
                               "obcx.bot.BridgeDeliveryPolicy.cbor");
  const auto source_scope = document(snapshot.admission->source_scope,
                                     "obcx.bot.ConversationRef.cbor")
                                .get<d::ConversationRef>();
  const auto target_scope = document(snapshot.admission->target_scope,
                                     "obcx.bot.ConversationRef.cbor")
                                .get<d::ConversationRef>();
  const auto &publication = policy.at("publication");
  const auto event_id = publication.at("event_id").get<std::string>();
  const auto source_bot = publication.at("source_bot").get<std::string>();
  if (event_id.empty() ||
      source_bot != source_scope.installation.installation_id)
    throw std::runtime_error("publication original identity mismatch");
  if (std::ranges::any_of(outputs, [&](const auto &message) {
        return message.conversation != target_scope;
      }))
    throw std::runtime_error("publication target evidence mismatch");

  auto sources = d::Json::array();
  for (const auto &member : snapshot.admission->members) {
    const auto message =
        document(member.message_ref, "obcx.bot.MessageRef.cbor")
            .get<d::MessageRef>();
    if (message.conversation != source_scope)
      throw std::runtime_error("publication source member mismatch");
    sources.push_back({{"member_id", member.member_id}, {"message", message}});
  }
  const auto &primary = sources.front();
  const auto anchor = bridge_delivery_quote_anchor(snapshot);
  obcx::core::MessageEnvelope notification;
  notification.id = "forwarded-" + snapshot.delivery_id;
  notification.type = "bridge::events::MessageForwarded";
  notification.source_platform = platform(source_scope);
  notification.conversation_id = source_scope.id.serialized();
  notification.source_bot = source_bot;
  notification.correlation_id =
      publication.at("correlation_id").get<std::string>();
  notification.causation_id = event_id;
  notification.timestamp = std::chrono::system_clock::now();
  notification.payload = {
      {"schema", 2},
      {"source_message", primary.at("message")},
      {"source_conversation", source_scope},
      {"target_conversation", target_scope},
      {"quote_anchor", anchor ? d::Json(*anchor) : d::Json(nullptr)},
      {"delivery_id", snapshot.delivery_id},
      {"target_messages", outputs},
      {"output_details", receipt.output_details}};
  if (policy.contains("album_batch_policy")) {
    const auto &trace = policy.at("album_observations");
    if (policy.at("album_batch_policy") != "preserve-observed-batch" ||
        !trace.is_array() || trace.size() != snapshot.admission->members.size())
      throw std::runtime_error(
          "album publication lacks its original member traces");
    auto publications = d::Json::array();
    for (std::size_t i = 0; i < trace.size(); ++i) {
      const auto &member = snapshot.admission->members[i];
      const auto &entry = trace.at(i);
      if (!member.media_group_ref ||
          entry.at("member_id") != member.member_id ||
          entry.at("event_id").get<std::string>().empty() ||
          entry.at("source_bot") != source_bot ||
          !entry.at("correlation_id").is_string())
        throw std::runtime_error("album publication member trace mismatch");
      if (i == 0 &&
          (entry.at("event_id") != event_id ||
           entry.at("correlation_id") != publication.at("correlation_id")))
        throw std::runtime_error("album primary publication trace mismatch");
      auto bound = entry;
      bound["message"] =
          document(member.message_ref, "obcx.bot.MessageRef.cbor");
      publications.push_back(std::move(bound));
    }
    // Suppressed observations are deliberately absent: this notification
    // describes only the members actually submitted in the immutable body.
    notification.payload["source_publications"] = std::move(publications);
  }
  notification.payload["source_members"] = std::move(sources);
  return notification;
}

auto bridge_revision_output_messages(const ledger::RevisionSnapshot &snapshot)
    -> std::vector<d::MessageRef> {
  const auto target =
      document(snapshot.admission.target_scope, "obcx.bot.ConversationRef.cbor")
          .get<d::ConversationRef>();
  std::vector<d::MessageRef> messages;
  for (const auto &output : snapshot.observed_outputs) {
    auto message = document(output.message_ref, "obcx.bot.MessageRef.cbor")
                       .get<d::MessageRef>();
    if (message.conversation != target ||
        std::ranges::find(messages, message) != messages.end())
      throw std::runtime_error("edit observation scope or identity conflict");
    messages.push_back(std::move(message));
  }
  return messages;
}
auto bridge_revision_notification(const BridgeRevisionResult &result)
    -> obcx::core::MessageEnvelope {
  const auto &revision = result.persisted;
  if (!result.newly_completed || !revision.checkpoint ||
      !revision.checkpoint->complete ||
      !revision.observations_match_checkpoint || !result.outcome ||
      !std::holds_alternative<d::SendReceipt>(*result.outcome))
    throw std::invalid_argument(
        "edit publication requires newly persisted complete content");
  const auto intent =
      document(revision.admission.intent, "bridge.EditIntent.cbor");
  const auto send = intent.at("send").get<d::SendMessageRequest>();
  d::validate_send_outcome(send, *result.outcome);
  const auto &receipt = std::get<d::SendReceipt>(*result.outcome);
  const auto evidence =
      document(revision.checkpoint->evidence, "bridge.EditOutcomes.cbor");
  if (evidence.at("replacement") != d::Json(*result.outcome) ||
      evidence.at("deletions") != d::Json(result.deletions) ||
      evidence.at("prior_deletion_operations") !=
          d::Json(result.prior_deletion_operations) ||
      result.prior_deletion_operations.size() != result.deletions.size())
    throw std::runtime_error("edit publication outcome binding mismatch");
  const auto observed = bridge_revision_output_messages(revision);
  if (receipt.messages.size() != observed.size() ||
      revision.checkpoint->outputs.size() != receipt.messages.size())
    throw std::runtime_error("edit publication output set mismatch");
  for (std::size_t i = 0; i < receipt.messages.size(); ++i) {
    const auto &output = revision.checkpoint->outputs[i];
    if (output.ordinal !=
            std::optional<std::int64_t>{static_cast<std::int64_t>(i)} ||
        document(output.message_ref, "obcx.bot.MessageRef.cbor")
                .get<d::MessageRef>() != receipt.messages[i] ||
        std::ranges::find(observed, receipt.messages[i]) == observed.end())
      throw std::runtime_error("edit publication receipt order mismatch");
  }
  const auto source =
      document(revision.admission.source_ref, "obcx.bot.MessageRef.cbor")
          .get<d::MessageRef>();
  const auto source_scope =
      document(revision.admission.source_scope, "obcx.bot.ConversationRef.cbor")
          .get<d::ConversationRef>();
  const auto target_scope =
      document(revision.admission.target_scope, "obcx.bot.ConversationRef.cbor")
          .get<d::ConversationRef>();
  const auto original = intent.at("original_message").get<d::MessageReceived>();
  const auto &publication = intent.at("publication");
  const auto source_bot = publication.at("source_bot").get<std::string>();
  if (original.message != source || source.conversation != source_scope ||
      send.target != target_scope ||
      intent.at("pair_id") != revision.admission.pair_id ||
      intent.at("revision_id") != revision.admission.revision_id ||
      intent.at("delivery_id") != revision.admission.delivery_id ||
      source_bot != source_scope.installation.installation_id ||
      publication.at("event_id").get<std::string>().empty())
    throw std::runtime_error("edit publication original identity mismatch");
  original.validate();
  if (original.context.kind != "group")
    throw std::runtime_error(
        "edit publication source or target evidence mismatch");
  const auto decoded_refs = [&](const auto &values) {
    std::vector<d::MessageRef> refs;
    for (const auto &value : values) {
      auto ref = document(value, "obcx.bot.MessageRef.cbor")
                     .template get<d::MessageRef>();
      if (ref.conversation != target_scope)
        throw std::runtime_error("edit deletion publication scope mismatch");
      refs.push_back(std::move(ref));
    }
    return refs;
  };
  obcx::core::MessageEnvelope notification;
  notification.id = "forwarded-" + revision.admission.delivery_id +
                    ":edit:" + revision.admission.revision_id;
  notification.type = "bridge::events::MessageForwarded";
  notification.source_platform = platform(source_scope);
  notification.conversation_id = source_scope.id.serialized();
  notification.source_bot = source_bot;
  notification.correlation_id =
      publication.at("correlation_id").get<std::string>();
  notification.causation_id = publication.at("event_id").get<std::string>();
  notification.timestamp = std::chrono::system_clock::now();
  notification.payload = {
      {"schema", 2},
      {"source_message", source},
      {"source_conversation", source_scope},
      {"target_conversation", target_scope},
      {"quote_anchor", receipt.messages.front()},
      {"delivery_id", revision.admission.delivery_id},
      {"revision_id", revision.admission.revision_id},
      {"target_messages", receipt.messages},
      {"output_details", receipt.output_details},
      {"source_members",
       d::Json::array({{{"member_id", revision.admission.member_id},
                        {"message", source}}})},
      {"delete_outcomes", result.deletions},
      {"prior_deletion_operations", result.prior_deletion_operations},
      {"deleted_target_messages",
       decoded_refs(revision.checkpoint->deleted_outputs)},
      {"retained_target_messages",
       decoded_refs(revision.checkpoint->retained_outputs)}};
  return notification;
}

auto publish_bridge_delivery(
    std::shared_ptr<obcx::core::ActorEventPublisher> publisher,
    obcx::core::ActorGenerationLifecycle::TokenPtr token,
    BridgeDeliveryResult result) -> boost::asio::awaitable<bool> {
  if (!publisher)
    throw std::invalid_argument(
        "delivery publication requires generation services");
  auto notification = bridge_delivery_notification(result);
  // Failure here does not change Complete and must not trigger provider replay.
  co_return co_await publisher->publish(std::move(token),
                                        std::move(notification));
}
} // namespace bridge
