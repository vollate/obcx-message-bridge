#include "bridge_forwarding_runtime.hpp"
#include "core/bot/typed_operation.hpp"

#include "bridge_notice_event_adapter.hpp"
#include "config.hpp"
#include "qq/handler.hpp"
#include "qq/text_forwarder.hpp"
#include "telegram/album_delivery_worker.hpp"
#include "telegram/handler.hpp"
#include "telegram/media_group_buffer.hpp"
#include "telegram/ordinary_forwarder.hpp"
#include <telegram/bot/bot_views.hpp>

#include <common/logger.hpp>

#include <algorithm>
#include <chrono>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace bridge {
namespace {

auto target_platform_for(const std::string &platform) -> std::string {
  if (platform == "qq") {
    return "telegram";
  }
  if (platform == "telegram") {
    return "qq";
  }
  return {};
}

} // namespace

struct BridgeForwardingRuntime::PairRuntime {
  std::shared_ptr<BridgeCommandOperations> operations;
  std::shared_ptr<QQHandler> qq_handler;
  std::shared_ptr<qq::TextForwarder> qq_text_forwarder;
  std::shared_ptr<TelegramHandler> telegram_handler;
  std::shared_ptr<telegram::OrdinaryForwarder> telegram_ordinary_forwarder;
  std::shared_ptr<telegram::AlbumDeliveryWorker> telegram_albums;
};

BridgeForwardingRuntime::BridgeForwardingRuntime(
    std::shared_ptr<obcx::bot::BotOperationGateway> operation_client,
    std::shared_ptr<const BridgeConfig> config,
    std::shared_ptr<BridgeStateRepository> state_repository,
    std::shared_ptr<ReceivedMessageRepository> received_message_repository,
    boost::asio::any_io_executor buffer_executor,
    std::shared_ptr<obcx::core::BlockingExecutor> blocking_executor,
    std::shared_ptr<const obcx::command::CommandScopeResolver> command_scopes,
    std::shared_ptr<const obcx::bot::ConversationResolver> references,
    std::shared_ptr<obcx::core::ActorGenerationLifecycle> lifecycle,
    std::shared_ptr<obcx::core::ActorEventPublisher> publisher)
    : config_(std::move(config)),
      state_repository_(std::move(state_repository)),
      received_message_repository_(std::move(received_message_repository)),
      blocking_executor_(std::move(blocking_executor)),
      command_scopes_(std::move(command_scopes)) {
  if (!config_) {
    throw std::invalid_argument("bridge forwarding requires configuration");
  }
  if (!operation_client) {
    throw std::invalid_argument("bridge forwarding requires operation client");
  }
  if (!blocking_executor_ || !command_scopes_ || !references || !lifecycle ||
      !publisher) {
    throw std::invalid_argument("bridge forwarding requires blocking, command, "
                                "reference services");
  }

  std::vector<std::shared_ptr<BridgeCommandOperations>> operations;
  operations.reserve(config_->installation_pairs.size());
  for (const auto &[pair_id, pair] : config_->installation_pairs) {
    operations.push_back(std::make_shared<BridgeCommandOperations>(
        operation_client, pair, references));
  }

  // The generation-owned typed retry worker is prepared by BridgeActor.
  // Each pair's AlbumDeliveryWorker is its only album buffer/flush owner.
  for (const auto &pair_operations : operations) {
    auto runtime = std::make_unique<PairRuntime>();
    runtime->operations = pair_operations;
    runtime->qq_text_forwarder = std::make_shared<qq::TextForwarder>(
        operation_client, references, state_repository_, blocking_executor_,
        *config_->pair(pair_operations->pair_id()),
        config_->enable_retry_queue ? config_->message_retry_max_attempts : 1,
        config_);
    runtime->telegram_ordinary_forwarder =
        std::make_shared<telegram::OrdinaryForwarder>(
            operation_client, references, state_repository_, blocking_executor_,
            *config_->pair(pair_operations->pair_id()), config_);
    runtime->telegram_albums = telegram::AlbumDeliveryWorker::prepare(
        buffer_executor, runtime->telegram_ordinary_forwarder, pair_operations,
        references, lifecycle, publisher);
    runtime->qq_handler = std::make_shared<QQHandler>(
        pair_operations, config_, state_repository_,
        received_message_repository_, blocking_executor_);
    runtime->telegram_handler = std::make_shared<TelegramHandler>(
        pair_operations, config_, state_repository_,
        received_message_repository_, blocking_executor_);
    pair_runtimes_.emplace(pair_operations->pair_id(), std::move(runtime));
  }
}

BridgeForwardingRuntime::~BridgeForwardingRuntime() { shutdown(); }

void BridgeForwardingRuntime::shutdown() noexcept {
  bool expected = false;
  if (!shutting_down_.compare_exchange_strong(expected, true,
                                              std::memory_order_acq_rel)) {
    return;
  }
  for (const auto &[id, runtime] : pair_runtimes_) {
    (void)id;
    if (runtime->telegram_albums)
      runtime->telegram_albums->stop();
  }
}

auto BridgeForwardingRuntime::forward_message(
    const obcx::message_store::events::MessageStored &stored,
    const obcx::core::MessageEnvelope &message)
    -> boost::asio::awaitable<BridgeForwardResult> {
  if (shutting_down_.load(std::memory_order_acquire)) {
    throw std::runtime_error("bridge forwarding runtime is shutting down");
  }
  stored.message.validate();
  stored.source.validate(stored.message);
  const auto &from_platform = stored.source.source_platform;
  const auto to_platform = target_platform_for(from_platform);
  if (to_platform.empty()) {
    throw std::runtime_error("unsupported bridge source platform " +
                             from_platform);
  }
  const auto &pair = resolve_bridge_source_pair(*config_, from_platform,
                                                stored.source.source_bot);
  const auto runtime = pair_runtimes_.find(pair.id);
  if (runtime == pair_runtimes_.end() || !runtime->second) {
    throw std::runtime_error("bridge pair runtime is unavailable: " + pair.id);
  }

  if (from_platform == "qq") {
    if (stored.message.message.conversation.installation !=
        runtime->second->operations->onebot11_installation())
      throw std::invalid_argument("QQ forwarding source installation mismatch");
    // Preserve the predecessor liveness update. It observes receipt, not
    // delivery success/confirmation; there is no legacy forwarding branch.
    if (state_repository_) {
      const auto now = std::chrono::system_clock::now();
      (void)co_await blocking_executor_->run(
          [repository = state_repository_,
           installation = stored.source.source_bot, now] {
            return repository->update_platform_heartbeat(installation, "qq",
                                                         now);
          });
    }
    // One QQ message has one typed delivery owner. Unsupported complete bodies
    // are rejected there before preparation; they never enter a raw sender,
    // per-image cache send, legacy retry payload or post-failure feedback send.
    co_return co_await runtime->second->qq_text_forwarder->forward(
        stored, {message.id, message.correlation_id, message.source_bot});
  }

  if (from_platform == "telegram" &&
      obcx::telegram::bot::message_update_view(stored.message)) {
    co_return co_await runtime->second->telegram_ordinary_forwarder
        ->forward_edit(
            stored, {message.id, message.correlation_id, message.source_bot});
  }

  if (from_platform == "telegram" && stored.message.media_group) {
    runtime->second->telegram_albums->enqueue(
        {stored, BridgeDeliveryPublication{message.id, message.correlation_id,
                                           message.source_bot}});
    co_return BridgeForwardResult{}; // The accepted batch owns
                                     // completion/publication.
  }
  // Select the original route before rejecting unsupported presentation.
  // Unmatched Telegram traffic remains a no-op; a matched unsupported body
  // still fails explicitly and never reaches a predecessor raw sender.
  co_return co_await runtime->second->telegram_ordinary_forwarder->forward(
      stored, {message.id, message.correlation_id, message.source_bot});
}

auto BridgeForwardingRuntime::handle_command(
    const obcx::command::CommandInvocation &invocation)
    -> boost::asio::awaitable<bool> {
  if (shutting_down_.load(std::memory_order_acquire)) {
    throw std::runtime_error("bridge forwarding runtime is shutting down");
  }
  const auto source = command_scopes_->source_from_invocation(invocation);
  if (!source.ok() || source.value->conversation_kind() !=
                          obcx::command::ConversationKind::Group) {
    co_return false;
  }
  std::optional<BridgeCommandRoute> route;
  for (auto &candidate : bridge_command_routes(*config_, invocation.name)) {
    const auto scope = command_scopes_->resolve_group(candidate.source);
    if (!scope.ok()) {
      throw std::runtime_error("bridge command scope is unavailable");
    }
    if (source.value->matches(*scope.value)) {
      route = std::move(candidate);
      break;
    }
  }
  if (!route) {
    co_return false;
  }
  const auto runtime = pair_runtimes_.find(route->pair_id);
  if (runtime == pair_runtimes_.end() || !runtime->second) {
    throw std::runtime_error("bridge pair runtime is unavailable");
  }
  if (invocation.name == "bridge_status") {
    // Own the original scoped reference before any handler/DB suspension.
    // Status replies never reconstruct a group/topic or parse native segments.
    auto reply_source = source.value->message().message;
    const auto &operations = runtime->second->operations;
    if (reply_source.conversation.installation ==
        operations->telegram_installation()) {
      co_await runtime->second->telegram_handler->handle_bridge_status_command(
          std::move(reply_source));
    } else if (reply_source.conversation.installation ==
               operations->onebot11_installation()) {
      co_await runtime->second->qq_handler->handle_bridge_status_command(
          std::move(reply_source));
    } else {
      throw std::invalid_argument("bridge status source does not match pair");
    }
    co_return true;
  }
  if (invocation.source_platform == "telegram") {
    // Keep the admitted observation and the existing chat:-prefixed mapping
    // key. The matched configuration selector has the original group spelling;
    // do not substitute an input alias or decode an opaque conversation ID.
    BridgeCommandSource command_source{
        source.value->message(),
        telegram_conversation_id(route->source.group_id)};
    const auto &qq_group_id = route->target_group;
    if (invocation.name == "recall") {
      co_await runtime->second->telegram_handler->handle_recall_command(
          std::move(command_source), qq_group_id);
    } else if (invocation.name == "poke") {
      co_await runtime->second->telegram_handler->handle_poke_command(
          std::move(command_source), qq_group_id);
    } else if (invocation.name == "userinfo") {
      co_await runtime->second->telegram_handler->handle_userinfo_command(
          std::move(command_source), qq_group_id, invocation.arguments);
    } else if (invocation.name == "react" || invocation.name == "unreact") {
      co_await runtime->second->telegram_handler->handle_reaction_command(
          std::move(command_source), qq_group_id, invocation.arguments,
          invocation.name == "react");
    } else {
      co_return false;
    }
    co_return true;
  }

  co_return false;
}

auto BridgeForwardingRuntime::handle_notice(
    const obcx::core::MessageEnvelope &message,
    std::optional<obcx::bot::MessageDeleted> deleted)
    -> boost::asio::awaitable<bool> {
  if (shutting_down_.load(std::memory_order_acquire)) {
    throw std::runtime_error("bridge forwarding runtime is shutting down");
  }
  if (message.source_platform != "qq") {
    co_return false;
  }
  const auto &pair = resolve_bridge_source_pair(
      *config_, message.source_platform, message.source_bot);
  const auto runtime = pair_runtimes_.find(pair.id);
  if (runtime == pair_runtimes_.end() || !runtime->second) {
    throw std::runtime_error("bridge pair runtime is unavailable: " + pair.id);
  }
  if (deleted) {
    // Actor ingress already checked the envelope against this observation.
    // Do not require a second native notice decoder to authorize its delivery.
    co_await runtime->second->qq_handler->handle_recall_event(
        std::move(*deleted));
    co_return true;
  }
  // Raw notice inspection is confined to the proprietary poke extension. It
  // cannot manufacture a missing MessageDeleted or enter the recall path.
  const auto notice = notice_event_from_raw_notice(message);
  if (!notice.has_value()) {
    co_return false;
  }
  const auto subtype = notice->data.find("sub_type");
  if (notice->notice_type == "notify" && subtype != notice->data.end() &&
      subtype->is_string() && subtype->get<std::string>() == "poke")
    co_await runtime->second->qq_handler->handle_poke_event(*notice);
  co_return true;
}

} // namespace bridge
