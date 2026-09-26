#ifndef OBCX_TESTS_FAKE_GATEWAY_DISPATCH_HPP_
#define OBCX_TESTS_FAKE_GATEWAY_DISPATCH_HPP_

#include "core/bot/typed_operation.hpp"

namespace obcx::tests {

// SDK-only fake dispatch for independent actor tests. No process registry,
// provider class, transport, credential, or root implementation is included.
template <typename Request, typename... Rest, typename Fake>
auto dispatch_fake_gateway(Fake &fake, bot::OperationEnvelope envelope)
    -> boost::asio::awaitable<bot::OperationReply> {
  if (envelope.action == Request::action) {
    using Traits = bot::OperationTraits<Request>;
    using Result = typename Traits::result_type;
    std::optional<Request> request;
    try {
      envelope.validate();
      request.emplace(
          bot::GatewayCodec<Request>::decode(std::move(envelope.payload)));
      request->validate();
      if (Traits::installation(*request) != envelope.installation) {
        throw std::invalid_argument("fake gateway installation mismatch");
      }
    } catch (...) {
      co_return bot::failed_operation<bot::Json>(
          bot::BotOperationErrorCode::InvalidRequest,
          "invalid fake gateway request");
    }
    try {
      auto reply = co_await fake.execute(*request);
      reply.validate();
      if (!reply.ok()) {
        co_return bot::OperationReply::failure(std::move(*reply.error));
      }
      Traits::validate_result(*request, *reply.value);
      co_return bot::OperationReply::success(
          bot::GatewayCodec<Result>::encode(*reply.value));
    } catch (...) {
      co_return bot::failed_operation<bot::Json>(
          Traits::side_effecting
              ? bot::BotOperationErrorCode::OutcomeUnknown
              : bot::BotOperationErrorCode::MalformedResponse,
          "invalid fake gateway result", false,
          Traits::side_effecting
              ? bot::SubmissionSafety::PossiblySubmitted
              : bot::SubmissionSafety::DefinitelyNotSubmitted);
    }
  }
  if constexpr (sizeof...(Rest) > 0) {
    co_return co_await dispatch_fake_gateway<Rest...>(fake,
                                                      std::move(envelope));
  } else {
    co_return bot::failed_operation<bot::Json>(
        bot::BotOperationErrorCode::UnsupportedAction,
        "fake gateway action is unsupported");
  }
}

} // namespace obcx::tests

#endif // OBCX_TESTS_FAKE_GATEWAY_DISPATCH_HPP_
