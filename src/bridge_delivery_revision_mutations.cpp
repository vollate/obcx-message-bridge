#include "bridge_delivery_revision.hpp"
#include <algorithm>
#include <onebot11/bot/operations.hpp>
#include <set>

namespace bridge {
namespace {
namespace d = obcx::bot;
auto document(const ledger::OpaqueValue &value) -> d::Json {
  if (value.version != 1 || value.bytes.empty() ||
      value.bytes.size() > d::detail::domain_maximum_bytes)
    throw std::runtime_error("unsupported edit predecessor operation evidence");
  auto json = d::Json::from_cbor(value.bytes.begin(), value.bytes.end());
  d::detail::validate_domain_wire(json);
  const auto canonical = d::Json::to_cbor(json);
  if (std::string(canonical.begin(), canonical.end()) != value.bytes)
    throw std::runtime_error(
        "noncanonical edit predecessor operation evidence");
  return json;
}
template <class T>
void settled(const d::BotOperationResult<T> &result, bool matched) {
  result.validate();
  if (result.ok()) {
    if (!matched)
      throw std::runtime_error("edit predecessor receipt target mismatch");
  } else if (matched || result.error->submission_safety !=
                            d::SubmissionSafety::DefinitelyNotSubmitted) {
    throw std::runtime_error("edit predecessor mutation has uncertain effects");
  }
}
} // namespace

auto reconcile_revision_mutations(
    const std::vector<ledger::OutputOperation> &operations,
    const std::vector<ledger::OpaqueValue> &predecessors)
    -> std::vector<RevisionPriorDeletion> {
  std::vector<RevisionPriorDeletion> deleted;
  std::set<std::string> identities;
  for (const auto &operation : operations) {
    if (operation.operation_id.empty() ||
        !identities.insert(operation.operation_id).second ||
        std::ranges::find(predecessors, operation.message_ref) ==
            predecessors.end() ||
        operation.message_ref.codec != "obcx.bot.MessageRef.cbor")
      throw std::runtime_error("edit predecessor operation identity mismatch");
    const auto target = document(operation.message_ref).get<d::MessageRef>();
    const auto state = document(operation.result);
    if (state.at("phase") != "finished")
      throw std::runtime_error("edit predecessor mutation is still admitted");
    const auto matched = state.at("matched_receipt").get<bool>();
    if (operation.result.codec == "obcx.bot.BridgeRecallOutputState.cbor") {
      if (state.at("target").get<d::MessageRef>() != target)
        throw std::runtime_error("edit predecessor delete target changed");
      auto outcome =
          state.at("outcome").get<d::BotOperationResult<d::DeleteReceipt>>();
      settled(outcome, matched);
      if (!outcome.ok())
        continue; // A NEW edit may act after an old definite failure.
      if (outcome.value->message != target)
        throw std::runtime_error("edit predecessor delete receipt mismatch");
      if (std::ranges::none_of(deleted, [&](const auto &entry) {
            return entry.message_ref == operation.message_ref;
          }))
        deleted.push_back({operation.message_ref, operation.operation_id,
                           std::move(outcome)});
    } else if (operation.result.codec ==
               "obcx.bot.BridgeReactionOutputState.cbor") {
      const auto request =
          state.at("request")
              .get<obcx::onebot11::bot::SetOneBotMessageEmojiLikeRequest>();
      request.validate();
      if (request.message != target)
        throw std::runtime_error("edit predecessor reaction target changed");
      const auto outcome =
          state.at("outcome")
              .get<d::BotOperationResult<
                  obcx::onebot11::bot::OneBotMessageEmojiLikeResult>>();
      settled(outcome, matched);
      if (outcome.ok() && (outcome.value->message != target ||
                           outcome.value->emoji_id != request.emoji_id ||
                           outcome.value->set != request.set))
        throw std::runtime_error("edit predecessor reaction receipt mismatch");
      // A reaction never proves deletion and is never reapplied to a
      // replacement.
    } else {
      throw std::runtime_error(
          "edit predecessor operation kind is unsupported");
    }
  }
  return deleted;
}
} // namespace bridge
