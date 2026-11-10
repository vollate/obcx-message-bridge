#include "bridge_bot_failures.hpp"

#include <boost/asio/error.hpp>
#include <boost/system/system_error.hpp>
#include <string_view>
#include <system_error>
#include <utility>

namespace bridge {
namespace {

auto describe_error_code(const obcx::bot::BotOperationErrorCode code)
    -> std::string_view {
  using Code = obcx::bot::BotOperationErrorCode;
  switch (code) {
  case Code::InvalidRequest:
    return "request was rejected before sending";
  case Code::RouteNotFound:
    return "no bot is configured for the target";
  case Code::SurfaceMismatch:
    return "target belongs to a different platform";
  case Code::UnsupportedAction:
    return "platform does not support this action";
  case Code::ProviderRejected:
    return "platform rejected the request";
  case Code::MalformedResponse:
    return "platform response could not be understood";
  case Code::TransportFailure:
    return "network request failed";
  case Code::MediaTooLarge:
    return "media is too large";
  case Code::Cancelled:
    return "operation was cancelled";
  case Code::OutcomeUnknown:
    return "result is unknown";
  case Code::CacheUnavailable:
    return "required cached data is unavailable";
  }
  return "operation failed";
}

} // namespace

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
  auto diagnostic = std::string{describe_error_code(failure.code)} + " (" +
                    std::string{obcx::bot::error_code_id(failure.code)};
  if (failure.provider_code && !failure.provider_code->empty()) {
    diagnostic += ", platform error " +
                  obcx::bot::redact_bot_diagnostic(*failure.provider_code);
  }
  diagnostic += ")";
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
