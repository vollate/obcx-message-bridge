#pragma once

#include <core/bot/domain_messaging.hpp>

#include <stdexcept>
#include <string>
#include <utility>

namespace bridge {

class BridgeBotOperationFailure final : public std::runtime_error {
public:
  explicit BridgeBotOperationFailure(obcx::bot::BotOperationError error)
      : std::runtime_error(error.message), error_(std::move(error)) {}

  [[nodiscard]] auto error() const noexcept
      -> const obcx::bot::BotOperationError & {
    return error_;
  }

private:
  obcx::bot::BotOperationError error_;
};

// Keep every confirmed output and uncertainty flag until the actor boundary;
// a send failure must never be flattened to the predecessor scalar receipt.
class BridgeBotSendFailure final : public std::runtime_error {
public:
  explicit BridgeBotSendFailure(obcx::bot::SendFailure failure)
      : std::runtime_error(failure.error.message),
        failure_(std::move(failure)) {
    failure_.validate();
  }

  [[nodiscard]] auto failure() const noexcept
      -> const obcx::bot::SendFailure & {
    return failure_;
  }

private:
  obcx::bot::SendFailure failure_;
};

struct BridgeOperationFailureDisposition {
  bool retryable = false;
  bool outcome_unknown = false;
  std::string diagnostic;
};

// Cancellation is control flow, not permission for a replacement send. This
// predicate does not discard a BridgeBotSendFailure's confirmed effects.
[[nodiscard]] auto is_bridge_operation_cancelled(
    const std::exception &error) noexcept -> bool;

[[nodiscard]] auto classify_bridge_operation_failure(
    const std::exception &error) -> BridgeOperationFailureDisposition;

} // namespace bridge
