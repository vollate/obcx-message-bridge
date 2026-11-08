#include "bridge_delivery_retry_worker.hpp"
#include "bridge_bot_failures.hpp"
#include "bridge_delivery_revision.hpp"
#include "bridge_state_repository.hpp"
#include <algorithm>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <common/logger.hpp>
#include <retry_queue/queue.hpp>

namespace bridge {
namespace {
namespace d = obcx::bot;
namespace asio = boost::asio;
using Lifecycle = obcx::core::ActorGenerationLifecycle;
void warn_retained(const char *message) noexcept {
  try {
    OBCX_WARN("{}", message);
  } catch (...) {
  }
}
auto publish_failure(std::shared_ptr<obcx::core::ActorEventPublisher> publisher,
                     Lifecycle::TokenPtr token,
                     obcx::core::MessageEnvelope notification)
    -> asio::awaitable<void> {
  try {
    if (!co_await publisher->publish(std::move(token), std::move(notification)))
      warn_retained(
          "Bridge failure notification was not accepted; no resubmit");
  } catch (const std::exception &error) {
    if (is_bridge_operation_cancelled(error))
      throw;
    warn_retained("Bridge failure notification unavailable; no resubmit");
  }
}
template <class T> auto require(d::BotOperationResult<T> result) -> T {
  result.validate();
  if (!result.ok())
    throw std::invalid_argument("configured retry reference is unavailable");
  return std::move(*result.value);
}
auto decode(const ledger::OpaqueValue &value, std::string_view name)
    -> d::Json {
  if (value.codec != "obcx.bot." + std::string(name) + ".cbor" ||
      value.version != (name == "BridgeDeliveryPolicy" ? 2 : 1) ||
      value.bytes.empty() ||
      value.bytes.size() > d::detail::domain_maximum_bytes)
    throw std::runtime_error("unsupported retry route evidence");
  auto document = d::Json::from_cbor(value.bytes.begin(), value.bytes.end());
  d::detail::validate_domain_wire(document);
  return document;
}
auto now_ms() -> std::int64_t {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}
// The durable ledger owns attempts and backoff. Each discovered ID is only
// one in-memory execution opportunity; exceptions never authorize a resubmit.
template <class Payload, class Callable>
auto run_retry_batch(retry_queue::Strand strand, std::vector<Payload> items,
                     Callable attempt) -> asio::awaitable<void> {
  if (items.empty())
    co_return;
  // Always enter the queue's cancellation/drain path after admitting work.
  co_await asio::this_coro::throw_if_cancelled(false);
  retry_queue::Queue queue({strand, retry_queue::BlockingRunner{}},
                           {items.size(), 1});
  std::vector<retry_queue::JobHandle<void>> handles;
  try {
    handles.reserve(items.size());
    for (auto &item : items) {
      auto job = queue.submit(
          attempt, std::move(item), {1, std::nullopt},
          [&queue](const retry_queue::AttemptResult<void> &result,
                   retry_queue::AttemptContext) {
            if (!result.exception)
              return retry_queue::Decision::accept();
            try {
              std::rethrow_exception(result.exception);
            } catch (const std::exception &error) {
              if (is_bridge_operation_cancelled(error))
                queue.request_stop(retry_queue::StopMode::Cancel);
            } catch (...) {
              queue.request_stop(retry_queue::StopMode::Cancel);
            }
            return retry_queue::Decision::reject();
          },
          retry_queue::SubmitPosition::Back);
      if (!job)
        throw std::runtime_error("Bridge retry batch admission failed");
      handles.push_back(std::move(*job));
    }
  } catch (...) {
    queue.request_stop(retry_queue::StopMode::Cancel);
    throw;
  }
  co_await queue.run_until_idle();
  for (auto &handle : handles) {
    auto completion = co_await handle.wait();
    if (completion.classifier_exception)
      std::rethrow_exception(completion.classifier_exception);
    if (completion.last_result && completion.last_result->exception)
      std::rethrow_exception(completion.last_result->exception);
    if (completion.state == retry_queue::JobState::Cancelled)
      throw boost::system::system_error(asio::error::operation_aborted);
  }
}
} // namespace

BridgeDeliveryRetryWorker::BridgeDeliveryRetryWorker(
    asio::any_io_executor executor,
    std::shared_ptr<d::BotOperationGateway> gateway,
    std::shared_ptr<BridgeStateRepository> repository,
    std::shared_ptr<obcx::core::BlockingExecutor> blocking,
    std::shared_ptr<const d::ConversationResolver> references,
    std::shared_ptr<Lifecycle> lifecycle,
    std::shared_ptr<obcx::core::ActorEventPublisher> publisher,
    const BridgeConfig &config, std::int64_t maximum_per_poll)
    : executor_(Lifecycle::make_serial_executor(std::move(executor))),
      timer_(executor_), gateway_(std::move(gateway)),
      repository_(std::move(repository)), blocking_(std::move(blocking)),
      references_(std::move(references)), lifecycle_(std::move(lifecycle)),
      publisher_(std::move(publisher)),
      interval_(config.retry_queue_check_interval_sec),
      maximum_per_poll_(maximum_per_poll) {
  if (!gateway_ || !repository_ || !blocking_ || !references_ || !lifecycle_ ||
      !publisher_ || interval_.count() <= 0 || maximum_per_poll_ <= 0 ||
      maximum_per_poll_ >
          static_cast<std::int64_t>(d::detail::domain_maximum_items))
    throw std::invalid_argument(
        "retry worker requires explicit owned services and bounds");
  const auto add = [&](const std::string &pair,
                       const d::BotInstallationRef &source,
                       d::ConfiguredConversation address, bool all_topics,
                       const d::BotInstallationRef &target,
                       d::ConfiguredConversation destination) {
    (void)require(references_->resolve(source, address));
    auto resolved = require(references_->resolve(target, destination));
    routes_.push_back(
        {pair, source, std::move(address), all_topics, std::move(resolved)});
  };
  for (const auto &[id, pair] : config.installation_pairs) {
    const d::BotInstallationRef qq{pair.onebot11_installation,
                                   d::SurfaceId{"onebot11.qq"}};
    const d::BotInstallationRef tg{pair.telegram_installation,
                                   d::SurfaceId{"telegram.bot_api"}};
    for (const auto &[key, mapping] : pair.group_map) {
      (void)key;
      const auto telegram_group =
          configured_group(mapping.telegram_group_id, std::nullopt);
      if (mapping.mode == BridgeMode::GROUP_TO_GROUP) {
        const auto qq_group =
            configured_group(mapping.qq_group_id, std::nullopt);
        if (mapping.enable_qq_to_tg)
          add(id, qq, qq_group, false, tg, telegram_group);
        if (mapping.enable_tg_to_qq)
          add(id, tg, telegram_group, true, qq, qq_group);
      } else {
        for (const auto &topic : mapping.topics) {
          const auto qq_group =
              configured_group(topic.qq_group_id, std::nullopt);
          const auto telegram_topic =
              configured_group(mapping.telegram_group_id,
                               std::to_string(topic.telegram_topic_id));
          if (topic.enable_qq_to_tg)
            add(id, qq, qq_group, false, tg, telegram_topic);
          if (topic.enable_tg_to_qq)
            add(id, tg, telegram_topic, false, qq, qq_group);
        }
      }
    }
  }
}

auto BridgeDeliveryRetryWorker::select_route(
    const ledger::DeliverySnapshot &snapshot) const
    -> std::optional<BridgeRetryRoute> {
  if (!snapshot.admission)
    return std::nullopt;
  const auto &admission = *snapshot.admission;
  const auto source = decode(admission.source_scope, "ConversationRef")
                          .get<d::ConversationRef>();
  const auto target = decode(admission.target_scope, "ConversationRef")
                          .get<d::ConversationRef>();
  const auto policy =
      decode(admission.policy_and_limits, "BridgeDeliveryPolicy");
  const auto &facts = policy.at("source_route");
  const d::ConfiguredConversation observed{
      d::ConfiguredConversationKind::Group,
      facts.at("group_id").get<std::string>(),
      facts.at("topic_id").is_null()
          ? std::nullopt
          : std::optional{facts.at("topic_id").get<std::int64_t>()}};
  if (require(references_->resolve(source.installation, observed)) != source)
    throw std::runtime_error("retry original source scope evidence mismatch");
  std::optional<BridgeRetryRoute> selected;
  for (const auto &route : routes_) {
    if (route.pair_id != admission.pair_id ||
        route.source_installation != source.installation ||
        route.source_address.native_id != observed.native_id ||
        (!route.all_topics && route.source_address.topic != observed.topic) ||
        route.target != target)
      continue;
    if (selected)
      throw std::runtime_error("ambiguous configured retry route");
    selected = BridgeRetryRoute{admission.pair_id, source, target};
  }
  return selected; // Removed routes remain unavailable; never remap the target.
}

void BridgeDeliveryRetryWorker::start(Lifecycle::TokenPtr token) noexcept {
  std::optional<Active> launch;
  try {
    {
      std::scoped_lock lock(mutex_);
      desired_ = std::move(token);
      if (active_ || !desired_ || !desired_->valid())
        return;
      auto lease = lifecycle_->acquire_work(desired_);
      if (!lease)
        return;
      active_ = Active{desired_, std::move(lease),
                       std::make_shared<asio::cancellation_signal>()};
      launch = active_;
    }
    auto self = shared_from_this();
    Lifecycle::spawn_owned(
        executor_, launch->lease, run(self, launch->token),
        [self, active = *launch](std::exception_ptr error) {
          // The core outer completion also pins closure
          // destruction after this DSO callback returns.
          self->finished(active.token, std::move(error));
        },
        launch->cancel->slot());
  } catch (...) {
    {
      std::scoped_lock lock(mutex_);
      if (launch && active_ && active_->token == launch->token)
        active_.reset();
    }
    try {
      OBCX_ERROR(
          "Typed bridge retry worker could not start; persisted work retained");
    } catch (...) {
      // Logging must not escape the noexcept generation callback.
    }
  }
}

void BridgeDeliveryRetryWorker::stop() noexcept {
  try {
    std::optional<Active> active;
    {
      std::scoped_lock lock(mutex_);
      desired_.reset();
      active = active_;
    }
    if (active) {
      // Every posted DSO callback also owns the route lease. Never stop the
      // executor or abandon SDK/DB continuations while unloading this actor.
      const auto lease = active->lease;
      Lifecycle::post_owned(executor_, lease, [active = std::move(*active)] {
        active.cancel->emit(asio::cancellation_type::all);
      });
    }
  } catch (...) {
    // Token invalidation still prevents another send; an in-flight operation
    // retains its lease until its ordinary completion/deadline and join.
    try {
      OBCX_ERROR("Typed bridge retry cancellation post failed; drain remains "
                 "required");
    } catch (...) {
      // Keep the existing drain/lease boundary even if diagnostics fail.
    }
  }
}

void BridgeDeliveryRetryWorker::finished(Lifecycle::TokenPtr token,
                                         std::exception_ptr error) noexcept {
  Lifecycle::TokenPtr restart;
  {
    std::scoped_lock lock(mutex_);
    if (!active_ || active_->token != token)
      return;
    active_.reset();
    if (desired_ && desired_ != token && desired_->valid())
      restart = desired_;
  }
  if (error && token->valid()) {
    try {
      OBCX_WARN("Typed bridge retry pump retired with an exception; durable "
                "states retained");
    } catch (...) {
      // Diagnostics cannot prevent retirement or an already-requested restart.
    }
  }
  if (restart)
    start(std::move(restart));
}

auto BridgeDeliveryRetryWorker::execute_delivery(
    std::shared_ptr<BridgeDeliveryRetryWorker> self, Lifecycle::TokenPtr token,
    std::string id) -> asio::awaitable<void> {
  if (!token->valid())
    throw boost::system::system_error(asio::error::operation_aborted);
  std::optional<obcx::core::MessageEnvelope> failure_notification;
  try {
    auto snapshot =
        co_await self->blocking_->run([repository = self->repository_, id] {
          return repository->load_delivery(id);
        });
    if (!snapshot || !snapshot->retry || !snapshot->safe_to_retry)
      co_return;
    const auto route = self->select_route(*snapshot);
    if (!route || !token->valid())
      co_return;
    auto result = co_await retry_bridge_delivery(
        self->gateway_, self->repository_, self->blocking_, id, *route, token);
    if (result.newly_completed) {
      const auto published = co_await publish_bridge_delivery(
          self->publisher_, token, std::move(result));
      if (!published)
        warn_retained("Bridge retry completed but notification was not "
                      "accepted; no resubmit");
    }
  } catch (const BridgeDeliveryPersistenceFailure &error) {
    failure_notification = bridge_delivery_failure_notification(error);
  } catch (const std::exception &error) {
    if (is_bridge_operation_cancelled(error))
      throw;
    // Do not reinterpret persistence errors or partial/unknown effects
    // as a retryable provider failure. The next poll uses ledger state.
    warn_retained("Typed bridge retry item unavailable or uncertain; ledger "
                  "retained");
  }
  if (failure_notification)
    co_await publish_failure(self->publisher_, token,
                             std::move(*failure_notification));
}

auto BridgeDeliveryRetryWorker::execute_revision(
    std::shared_ptr<BridgeDeliveryRetryWorker> self, Lifecycle::TokenPtr token,
    std::pair<std::string, std::string> id) -> asio::awaitable<void> {
  const auto &[delivery_id, revision_id] = id;
  if (!token->valid())
    throw boost::system::system_error(asio::error::operation_aborted);
  std::optional<obcx::core::MessageEnvelope> failure_notification;
  try {
    const auto snapshot = co_await self->blocking_->run(
        [repository = self->repository_, delivery_id] {
          return repository->load_delivery(delivery_id);
        });
    if (!snapshot)
      co_return;
    const auto route = self->select_route(*snapshot);
    const auto revision =
        std::ranges::find_if(snapshot->revisions, [&](const auto &entry) {
          return entry.admission.revision_id == revision_id;
        });
    if (!route || revision == snapshot->revisions.end() || !token->valid())
      co_return;
    auto result = co_await retry_bridge_revision(
        self->gateway_, self->repository_, self->blocking_, *revision, *route,
        token);
    if (result.newly_completed) {
      auto notification = bridge_revision_notification(result);
      const auto published =
          co_await self->publisher_->publish(token, std::move(notification));
      if (!published)
        warn_retained("Bridge edit retry completed without notification "
                      "acceptance; no resubmit");
    }
  } catch (const BridgeRevisionPersistenceFailure &error) {
    failure_notification = bridge_revision_failure_notification(error.result());
  } catch (const std::exception &error) {
    if (is_bridge_operation_cancelled(error))
      throw;
    warn_retained("Typed edit retry unavailable or uncertain; durable "
                  "effects retained");
  }
  if (failure_notification)
    co_await publish_failure(self->publisher_, token,
                             std::move(*failure_notification));
}

auto BridgeDeliveryRetryWorker::run(
    std::shared_ptr<BridgeDeliveryRetryWorker> self, Lifecycle::TokenPtr token)
    -> asio::awaitable<void> {
  const auto strand = asio::make_strand(self->executor_);
  while (token->valid()) {
    try {
      auto ids = co_await self->blocking_->run(
          [repository = self->repository_, maximum = self->maximum_per_poll_] {
            try {
              (void)repository->prune_terminal_retry_payloads(maximum);
            } catch (...) {
              warn_retained("Terminal retry payload cleanup deferred; retry "
                            "discovery remains available");
            }
            return repository->list_ready_retry_deliveries(now_ms(), maximum);
          });
      co_await asio::co_spawn(
          strand,
          run_retry_batch(
              strand, std::move(ids),
              [self, token](std::string &id, retry_queue::AttemptContext) {
                return execute_delivery(self, token, id);
              }),
          asio::use_awaitable);
      if (!token->valid())
        co_return;
      auto revisions = co_await self->blocking_->run(
          [repository = self->repository_, maximum = self->maximum_per_poll_] {
            return repository->list_ready_revision_retries(now_ms(), maximum);
          });
      co_await asio::co_spawn(
          strand,
          run_retry_batch(strand, std::move(revisions),
                          [self, token](std::pair<std::string, std::string> &id,
                                        retry_queue::AttemptContext) {
                            return execute_revision(self, token, id);
                          }),
          asio::use_awaitable);
    } catch (const std::exception &error) {
      if (is_bridge_operation_cancelled(error))
        co_return;
      warn_retained("Typed bridge retry scan unavailable; no legacy fallback");
    }
    if (!token->valid())
      co_return;
    self->timer_.expires_after(self->interval_);
    co_await self->timer_.async_wait(asio::use_awaitable);
  }
}
} // namespace bridge
