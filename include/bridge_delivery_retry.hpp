#pragma once
#include "bridge_delivery_sender.hpp"

namespace bridge::detail {
auto delivery_retry_due_at_ms(const BridgeDeliveryRetryPolicy &policy,
                              std::int64_t completed_attempts,
                              std::int64_t finished_at_ms) -> std::int64_t;
// Internal owner codec: no SDK/native DTOs, cache paths, or acquired URLs.
auto prepare_delivery_retry(const BridgeDeliveryRequest &request,
                            std::int64_t completed_attempts,
                            std::int64_t finished_at_ms)
    -> ledger::RetryPayload;
auto restore_delivery_retry(const ledger::DeliverySnapshot &snapshot,
                            const BridgeRetryRoute &route)
    -> BridgeDeliveryRequest;
// Pure shared receipt/provenance projection; does not admit any provider work.
auto describe_delivery_outcome(const BridgeDeliveryRequest &request,
                               const obcx::bot::SendOutcome &outcome)
    -> ledger::AttemptResult;
// Called only after the caller obtained Created for this exact attempt.
auto submit_delivery_attempt(
    std::shared_ptr<obcx::bot::BotOperationGateway> gateway,
    std::shared_ptr<BridgeStateRepository> repository,
    std::shared_ptr<obcx::core::BlockingExecutor> blocking,
    BridgeDeliveryRequest request, std::int64_t completed_attempts,
    obcx::core::ActorGenerationLifecycle::TokenPtr token,
    obcx::core::MessageEnvelope failure_notification)
    -> boost::asio::awaitable<BridgeDeliveryResult>;
} // namespace bridge::detail
