#pragma once
#include "bridge_delivery_sender.hpp"

namespace bridge {
struct BridgeRevisionResult {
  ledger::RevisionSnapshot persisted;
  // In expected_outputs order: attempted deletions or confirmed prior deletes.
  // An installed unknown result also survives a throwing provider invocation.
  std::vector<obcx::bot::BotOperationResult<obcx::bot::DeleteReceipt>>
      deletions;
  std::optional<obcx::bot::SendOutcome> outcome;
  bool newly_completed;
  bool interrupted;
  // Parallel to deletions. Empty identifies this invocation; otherwise the
  // original completed operation supplied the receipt without another call.
  std::vector<std::string> prior_deletion_operations;
};
struct RevisionPriorDeletion {
  ledger::OpaqueValue message_ref;
  std::string operation_id;
  obcx::bot::BotOperationResult<obcx::bot::DeleteReceipt> outcome;
};
// Pure actor-owned classification. Pending/unknown mutations stop preparation;
// finished reactions and definitely-not-submitted failures do not delete refs.
auto reconcile_revision_mutations(
    const std::vector<ledger::OutputOperation> &operations,
    const std::vector<ledger::OpaqueValue> &predecessors)
    -> std::vector<RevisionPriorDeletion>;
auto retry_bridge_revision(
    std::shared_ptr<obcx::bot::BotOperationGateway> gateway,
    std::shared_ptr<BridgeStateRepository> repository,
    std::shared_ptr<obcx::core::BlockingExecutor> blocking,
    ledger::RevisionSnapshot snapshot, BridgeRetryRoute route,
    obcx::core::ActorGenerationLifecycle::TokenPtr token)
    -> boost::asio::awaitable<BridgeRevisionResult>;
namespace detail {
// Called only with a newly admitted Sending checkpoint, never from ingress
// replay.
auto submit_revision_replacement(
    std::shared_ptr<obcx::bot::BotOperationGateway> gateway,
    std::shared_ptr<BridgeStateRepository> repository,
    std::shared_ptr<obcx::core::BlockingExecutor> blocking,
    BridgeDeliveryRequest request, BridgeRevisionResult result,
    obcx::core::ActorGenerationLifecycle::TokenPtr token)
    -> boost::asio::awaitable<BridgeRevisionResult>;
} // namespace detail

auto bridge_revision_output_messages(const ledger::RevisionSnapshot &snapshot)
    -> std::vector<obcx::bot::MessageRef>;
auto bridge_revision_notification(const BridgeRevisionResult &result)
    -> obcx::core::MessageEnvelope;
auto bridge_revision_failure_notification(const BridgeRevisionResult &result)
    -> obcx::core::MessageEnvelope;

class BridgeRevisionPersistenceFailure final : public std::runtime_error {
public:
  explicit BridgeRevisionPersistenceFailure(BridgeRevisionResult result)
      : std::runtime_error("edit outcome persistence failed; do not resubmit"),
        result_(std::move(result)) {}
  [[nodiscard]] auto result() const noexcept -> const BridgeRevisionResult & {
    return result_;
  }

private:
  BridgeRevisionResult result_;
};
// One fresh edit invocation only, not a retry/resume entry. All preparation and
// complete intent serialization precede this function. Created admits the
// workflow; each effect also has a durable checkpoint before invocation.
// Definite deletion rejections retain old outputs and allow the new version;
// cancellation/uncertain deletion stops the workflow. No rollback is claimed.
auto send_bridge_revision(
    std::shared_ptr<obcx::bot::BotOperationGateway> gateway,
    std::shared_ptr<BridgeStateRepository> repository,
    std::shared_ptr<obcx::core::BlockingExecutor> blocking,
    BridgeDeliveryRequest request, ledger::RevisionAdmission admission)
    -> boost::asio::awaitable<BridgeRevisionResult>;
} // namespace bridge
