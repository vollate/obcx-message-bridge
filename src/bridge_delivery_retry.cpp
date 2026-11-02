#include "bridge_delivery_retry.hpp"
#include "bridge_state_repository.hpp"

#include <algorithm>
#include <boost/asio/this_coro.hpp>
#include <chrono>
#include <exception>
#include <limits>
#include <set>
#include <type_traits>

namespace bridge {
namespace {
namespace d = obcx::bot;
template <class T>
auto encode(std::string_view name, const T &value) -> ledger::OpaqueValue {
  const d::Json document = value;
  d::detail::validate_domain_wire(document);
  const auto bytes = d::Json::to_cbor(document);
  return {"obcx.bot." + std::string(name) + ".cbor",
          name == "BridgeDeliveryPolicy" ? 2 : 1,
          std::string(bytes.begin(), bytes.end())};
}
auto decode(const ledger::OpaqueValue &value, std::string_view name)
    -> d::Json {
  if (value.codec != "obcx.bot." + std::string(name) + ".cbor" ||
      value.version != (name == "BridgeDeliveryPolicy" ? 2 : 1) ||
      value.bytes.empty() ||
      value.bytes.size() > d::detail::domain_maximum_bytes)
    throw std::invalid_argument("unsupported typed bridge retry encoding");
  auto document = d::Json::from_cbor(value.bytes.begin(), value.bytes.end());
  d::detail::validate_domain_wire(document);
  if (encode(name, document) != value)
    throw std::invalid_argument("noncanonical typed bridge retry encoding");
  return document;
}
auto media_ownership(const d::SendMessageRequest &send) -> d::Json {
  auto entries = d::Json::array();
  for (std::size_t i = 0; i < send.body.size(); ++i) {
    std::visit(
        [&](const auto &block) {
          if constexpr (!std::same_as<std::decay_t<decltype(block)>,
                                      d::RichText>) {
            // File leases and expiring HTTP acquisition are not interchangeable
            // with durable bytes. Do not manufacture a lease or reacquire a
            // URL.
            if (const auto *bytes = std::get_if<d::BytesSource>(&block.source))
              entries.push_back({{"block", i},
                                 {"kind", "owned-bytes"},
                                 {"size", bytes->bytes.size()}});
            else if (const auto *ref = std::get_if<d::MediaRef>(&block.source))
              entries.push_back({{"block", i},
                                 {"kind", "scoped-reference"},
                                 {"media", *ref}});
            else
              throw std::invalid_argument("durable retry requires owned bytes "
                                          "or a scoped media reference");
          }
        },
        send.body[i]);
  }
  return {{"format", 1},
          {"media", std::move(entries)},
          {"external_leases", d::Json::array()}};
}
auto now_ms() -> std::int64_t {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}
} // namespace

void BridgeDeliveryRetryPolicy::validate() const {
  if (base_interval_sec <= 0 || max_interval_sec < base_interval_sec ||
      max_interval_sec > std::numeric_limits<std::int64_t>::max() / 1000)
    throw std::invalid_argument("invalid explicit delivery retry policy");
}

namespace detail {
auto delivery_retry_due_at_ms(const BridgeDeliveryRetryPolicy &policy,
                              std::int64_t completed_attempts,
                              std::int64_t finished_at_ms) -> std::int64_t {
  policy.validate();
  if (completed_attempts <= 0 || finished_at_ms < 0)
    throw std::invalid_argument("invalid delivery retry sequence or clock");
  auto seconds = policy.base_interval_sec;
  for (std::int64_t i = 1;
       i < completed_attempts && seconds < policy.max_interval_sec; ++i)
    seconds = seconds > policy.max_interval_sec / 2 ? policy.max_interval_sec
                                                    : seconds * 2;
  if (finished_at_ms >
      std::numeric_limits<std::int64_t>::max() - seconds * 1000)
    throw std::invalid_argument("delivery retry due time overflow");
  return finished_at_ms + seconds * 1000;
}
auto prepare_delivery_retry(const BridgeDeliveryRequest &request,
                            std::int64_t completed_attempts,
                            std::int64_t finished_at_ms)
    -> ledger::RetryPayload {
  request.send.validate();
  if (!request.retry_policy || request.max_attempts <= 1 ||
      completed_attempts < 0 || completed_attempts >= request.max_attempts)
    throw std::invalid_argument("invalid delivery retry preparation");
  request.retry_policy->validate();
  const auto due =
      completed_attempts
          ? delivery_retry_due_at_ms(*request.retry_policy, completed_attempts,
                                     finished_at_ms)
          : std::numeric_limits<std::int64_t>::max();
  return {ledger::RetryPayload::current_format_version,
          encode("BridgeRetrySend", request.send),
          encode("ConversationRef", request.send.target),
          encode("BridgeRetryMediaOwnership", media_ownership(request.send)),
          encode("SendLimits", request.send.limits),
          due,
          completed_attempts};
}

auto restore_delivery_retry(const ledger::DeliverySnapshot &snapshot,
                            const BridgeRetryRoute &route)
    -> BridgeDeliveryRequest {
  route.source.validate();
  route.target.validate();
  if (!snapshot.admission || !snapshot.retry || !snapshot.safe_to_retry ||
      snapshot.attempts.empty() || !snapshot.outputs.empty() ||
      snapshot.admission->delivery_id != snapshot.delivery_id ||
      snapshot.admission->pair_id != route.pair_id ||
      snapshot.admission->source_scope !=
          encode("ConversationRef", route.source) ||
      snapshot.admission->target_scope !=
          encode("ConversationRef", route.target) ||
      snapshot.members.size() != snapshot.admission->members.size())
    throw std::runtime_error(
        "typed retry scope or zero-effect evidence unavailable");
  const auto &payload = *snapshot.retry;
  const auto &admission = *snapshot.admission;
  if (admission.members.empty() ||
      admission.members.size() > d::detail::domain_maximum_items ||
      snapshot.attempts.size() > d::detail::domain_maximum_items)
    throw std::runtime_error("typed retry evidence exceeds bounds");
  if (payload.format_version != ledger::RetryPayload::current_format_version ||
      payload.target_scope != admission.target_scope ||
      payload.retry_count !=
          static_cast<std::int64_t>(snapshot.attempts.size()))
    throw std::runtime_error("typed retry payload scope or count mismatch");
  auto send =
      decode(payload.body, "BridgeRetrySend").get<d::SendMessageRequest>();
  send.validate();
  const auto policy =
      decode(admission.policy_and_limits, "BridgeDeliveryPolicy");
  if (send.target != route.target ||
      payload.total_budget != encode("SendLimits", send.limits) ||
      payload.media_and_leases !=
          encode("BridgeRetryMediaOwnership", media_ownership(send)) ||
      policy.at("limits") != d::Json(send.limits) ||
      policy.at("delivery").get<unsigned>() !=
          static_cast<unsigned>(send.delivery) ||
      policy.at("reply_to") !=
          (send.reply_to ? d::Json(*send.reply_to) : d::Json(nullptr)))
    throw std::runtime_error("typed retry content or budget binding mismatch");
  // Storage classifies opaque effect records conservatively; the forwarding
  // owner additionally verifies the complete SDK outcomes against this exact
  // immutable body. A result flag alone cannot authorize another submission.
  for (std::size_t i = 0; i < snapshot.attempts.size(); ++i) {
    const auto &attempt = snapshot.attempts[i];
    if (attempt.sequence != static_cast<std::int64_t>(i) + 1 ||
        !attempt.result || attempt.result->complete ||
        attempt.result->effect != ledger::Effect::DefinitelyNotSubmitted ||
        !attempt.result->outputs.empty())
      throw std::runtime_error(
          "typed retry attempt evidence is not zero-effect");
    const auto outcome =
        decode(attempt.result->outcome, "SendOutcome").get<d::SendOutcome>();
    d::validate_send_outcome(send, outcome);
    const auto *failure = std::get_if<d::SendFailure>(&outcome);
    if (!failure || !failure->confirmed_messages.empty() ||
        failure->may_have_unconfirmed_messages ||
        failure->error.submission_safety !=
            d::SubmissionSafety::DefinitelyNotSubmitted ||
        failure->error.code == d::BotOperationErrorCode::Cancelled ||
        !failure->error.retryable || !attempt.result->retryable)
      throw std::runtime_error("typed retry outcome forbids resubmission");
  }
  BridgeDeliveryRetryPolicy retry_policy{
      policy.at("retry_policy").at("base_interval_sec").get<std::int64_t>(),
      policy.at("retry_policy").at("max_interval_sec").get<std::int64_t>()};
  retry_policy.validate();
  if (payload.next_retry_at_ms !=
      delivery_retry_due_at_ms(retry_policy, payload.retry_count,
                               snapshot.attempts.back().result->finished_at_ms))
    throw std::runtime_error("retry due time differs from immutable policy");
  std::set<std::pair<std::string, std::string>> inputs;
  for (const auto &member : snapshot.members) {
    const auto original =
        std::ranges::find_if(admission.members, [&](const auto &value) {
          return value.member_id == member.member_id;
        });
    if (original == admission.members.end() || *original != member ||
        decode(member.message_ref, "MessageRef")
                .get<d::MessageRef>()
                .conversation != route.source)
      throw std::runtime_error("typed retry source membership changed");
    d::OperationCorrelation{member.member_id}.validate();
    if (member.input_blocks.empty() ||
        member.input_blocks.size() > d::detail::domain_maximum_items)
      throw std::runtime_error("typed retry invalid source contribution set");
    for (const auto &input : member.input_blocks) {
      d::OperationCorrelation{input}.validate();
      if (!inputs.emplace(member.member_id, input).second)
        throw std::runtime_error("typed retry duplicate source contribution");
    }
  }
  std::vector<std::optional<std::vector<ledger::Provenance>>> origins;
  for (const auto &block : policy.at("block_origins")) {
    if (block.is_null()) {
      origins.push_back(std::nullopt);
      continue;
    }
    std::vector<ledger::Provenance> values;
    std::set<std::pair<std::string, std::string>> seen;
    for (const auto &entry : block) {
      auto member = entry.at("member_id").get<std::string>();
      auto input = entry.at("input_block").get<std::string>();
      if (!inputs.contains({member, input}) ||
          !seen.emplace(member, input).second)
        throw std::runtime_error("typed retry unbound source contribution");
      values.push_back({std::move(member), std::move(input)});
    }
    if (values.empty())
      throw std::runtime_error("typed retry empty known provenance");
    origins.emplace_back(std::move(values));
  }
  if (origins.size() != send.body.size())
    throw std::runtime_error("typed retry body provenance mismatch");
  const auto anchor = policy.at("quote_anchor").get<std::string>();
  if (anchor != "none" && anchor != "first-confirmed")
    throw std::runtime_error("typed retry unknown quote policy");
  const auto &publication = policy.at("publication");
  BridgeDeliveryPublication trace{
      publication.at("event_id").get<std::string>(),
      publication.at("correlation_id").get<std::string>(),
      publication.at("source_bot").get<std::string>()};
  if (trace.event_id.empty() ||
      trace.source_bot != route.source.installation.installation_id)
    throw std::runtime_error("typed retry publication identity mismatch");
  const bool album = admission.members.front().media_group_ref.has_value();
  for (const auto &member : admission.members)
    if (member.media_group_ref != admission.members.front().media_group_ref)
      throw std::runtime_error("retry album membership changed");
  if (album) {
    const auto group =
        decode(*admission.members.front().media_group_ref, "MediaGroupRef")
            .get<d::MediaGroupRef>();
    const auto &traces = policy.at("album_observations");
    if (group.conversation != route.source ||
        policy.at("album_batch_policy") != "preserve-observed-batch" ||
        !traces.is_array() || traces.size() != admission.members.size())
      throw std::runtime_error("retry album trace or scope mismatch");
    for (std::size_t i = 0; i < traces.size(); ++i) {
      const auto &entry = traces.at(i);
      if (entry.at("member_id") != admission.members[i].member_id ||
          entry.at("event_id").get<std::string>().empty() ||
          !entry.at("correlation_id").is_string() ||
          entry.at("source_bot") != trace.source_bot)
        throw std::runtime_error("retry album publication member mismatch");
      if (i == 0 && (entry.at("event_id") != trace.event_id ||
                     entry.at("correlation_id") != trace.correlation_id))
        throw std::runtime_error("retry album primary trace mismatch");
    }
  } else if (policy.contains("album_batch_policy") ||
             policy.contains("album_observations")) {
    throw std::runtime_error(
        "ordinary retry unexpectedly carries album policy");
  }
  // This is an already-admitted send plan, not a new source observation.
  // No source archive is reconstructed and admission_for is never called.
  return {snapshot.delivery_id,
          snapshot.delivery_id + ":" +
              std::to_string(snapshot.attempts.size() + 1),
          route.pair_id,
          {},
          std::move(send),
          std::move(origins),
          anchor == "none" ? BridgeQuoteAnchor::None
                           : BridgeQuoteAnchor::FirstConfirmed,
          admission.admitted_at_ms,
          admission.max_attempts,
          std::nullopt,
          retry_policy,
          std::move(trace)};
}
} // namespace detail

auto retry_bridge_delivery(
    std::shared_ptr<d::BotOperationGateway> gateway,
    std::shared_ptr<BridgeStateRepository> repository,
    std::shared_ptr<obcx::core::BlockingExecutor> blocking,
    std::string delivery_id, BridgeRetryRoute route,
    obcx::core::ActorGenerationLifecycle::TokenPtr token)
    -> boost::asio::awaitable<BridgeDeliveryResult> {
  if (!gateway || !repository || !blocking || !token)
    throw std::invalid_argument("retry requires generation-owned services");
  auto snapshot = co_await blocking->run([repository, delivery_id] {
    return repository->load_delivery(delivery_id);
  });
  if (!snapshot)
    throw std::runtime_error("retry delivery missing");
  auto request = detail::restore_delivery_retry(*snapshot, route);
  if (!token->valid())
    co_return BridgeDeliveryResult{std::move(*snapshot), std::nullopt, false};
  auto failure_notification = detail::prepare_delivery_failure_notification(
      *snapshot->admission, request.attempt_id);
  const auto admitted = co_await blocking->run(
      [repository, admission = *snapshot->admission, payload = *snapshot->retry,
       attempt = request.attempt_id, started = now_ms()] {
        return repository->admit_retry_attempt(admission, payload, attempt,
                                               started);
      });
  if (admitted != ledger::AdmissionResult::Created) {
    auto current = co_await blocking->run([repository, delivery_id] {
      return repository->load_delivery(delivery_id);
    });
    if (!current)
      throw std::runtime_error("retry delivery disappeared");
    co_return BridgeDeliveryResult{std::move(*current), std::nullopt, false};
  }
  // Admission is already durable. Cancellation must reach the submit owner's
  // explicit no-send/settlement branch, not throw while entering its awaitable.
  const auto previous = co_await boost::asio::this_coro::throw_if_cancelled();
  co_await boost::asio::this_coro::throw_if_cancelled(false);
  std::optional<BridgeDeliveryResult> result;
  std::exception_ptr error;
  try {
    result = co_await detail::submit_delivery_attempt(
        std::move(gateway), std::move(repository), std::move(blocking),
        std::move(request),
        static_cast<std::int64_t>(snapshot->attempts.size()) + 1,
        std::move(token), std::move(failure_notification));
  } catch (...) {
    error = std::current_exception();
  }
  co_await boost::asio::this_coro::throw_if_cancelled(previous);
  if (error)
    std::rethrow_exception(error);
  co_return std::move(*result);
}
} // namespace bridge
