#include "bridge_bot_failures.hpp"

#include <boost/asio/error.hpp>
#include <boost/system/system_error.hpp>
#include <system_error>
#include <utility>

namespace bridge {

auto is_bridge_operation_cancelled(const std::exception &error) noexcept
    -> bool {
  if (const auto *typed =
          dynamic_cast<const BridgeBotOperationFailure *>(&error))
    return typed->error().code == obcx::bot::BotOperationErrorCode::Cancelled;
  if (const auto *send = dynamic_cast<const BridgeBotSendFailure *>(&error))
    return send->failure().error.code ==
           obcx::bot::BotOperationErrorCode::Cancelled;
  if (const auto *system =
          dynamic_cast<const boost::system::system_error *>(&error))
    return system->code() == boost::asio::error::operation_aborted;
  if (const auto *system = dynamic_cast<const std::system_error *>(&error))
    return system->code() == std::errc::operation_canceled;
  return false;
}

auto classify_bridge_operation_failure(const std::exception &error)
    -> BridgeOperationFailureDisposition {
  const auto *typed = dynamic_cast<const BridgeBotOperationFailure *>(&error);
  const auto *send = dynamic_cast<const BridgeBotSendFailure *>(&error);
  if (typed == nullptr && send == nullptr) {
    return {.outcome_unknown = true, .diagnostic = "unclassified_exception"};
  }
  const auto &failure = send ? send->failure().error : typed->error();
  const bool send_effects =
      send && (!send->failure().confirmed_messages.empty() ||
               send->failure().may_have_unconfirmed_messages);
  auto diagnostic = std::string{obcx::bot::error_code_id(failure.code)};
  if (failure.provider_code && !failure.provider_code->empty()) {
    diagnostic += " (provider_code=" +
                  obcx::bot::redact_bot_diagnostic(*failure.provider_code) +
                  ")";
  }
  if (!failure.message.empty()) {
    diagnostic += ": " + obcx::bot::redact_bot_diagnostic(failure.message);
  }
  return {
      .retryable = !is_bridge_operation_cancelled(error) && !send_effects &&
                   failure.retryable &&
                   failure.submission_safety ==
                       obcx::bot::SubmissionSafety::DefinitelyNotSubmitted,
      .outcome_unknown =
          send_effects || failure.submission_safety ==
                              obcx::bot::SubmissionSafety::PossiblySubmitted,
      .diagnostic = std::move(diagnostic),
  };
}

} // namespace bridge
