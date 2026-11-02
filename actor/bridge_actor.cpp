#include "bridge_actor.hpp"
#include "bridge_bot_failures.hpp"
#include "bridge_delivery_retry_worker.hpp"
#include "bridge_delivery_revision.hpp"
#include "bridge_delivery_sender.hpp"
#include "bridge_forwarder.hpp"
#include "bridge_forwarding_runtime.hpp"
#include "config.hpp"
#include "core/actor/command_scope.hpp"
#include "core/bot/typed_operation.hpp"
#include "received_message_repository.hpp"

#include <common/json_utils.hpp>
#include <core/bot/domain_codec.hpp>
#include <core/bot/operation_gateway.hpp>

#include <chrono>
#include <stdexcept>
#include <utility>
#include <vector>

namespace bridge {
namespace asio = boost::asio;
namespace {

auto payload_string(const obcx::common::json &payload, const char *key)
    -> std::string {
  if (!payload.contains(key) || payload.at(key).is_null()) {
    return {};
  }
  const auto &value = payload.at(key);
  if (value.is_string()) {
    return value.get<std::string>();
  }
  if (value.is_number_integer()) {
    return std::to_string(value.get<std::int64_t>());
  }
  if (value.is_number_unsigned()) {
    return std::to_string(value.get<std::uint64_t>());
  }
  return value.dump();
}

auto build_failed_event(const obcx::core::MessageEnvelope &message,
                        const std::string &code,
                        const std::string &error_message, const bool retryable)
    -> obcx::core::MessageEnvelope {
  obcx::core::MessageEnvelope envelope;
  envelope.id = "forward-failed-" + message.id;
  envelope.type = "bridge::events::MessageForwardFailed";
  envelope.source_platform = message.source_platform;
  envelope.source_bot = message.source_bot;
  envelope.correlation_id = message.correlation_id;
  envelope.causation_id = message.id;
  envelope.timestamp = std::chrono::system_clock::now();
  envelope.payload = {
      {"code", code},
      {"message", error_message},
      {"retryable", retryable},
      {"source_platform", message.source_platform},
      {"source_bot", message.source_bot},
      {"source_message_id", payload_string(message.payload, "message_id")},
  };
  return envelope;
}

} // namespace

auto BridgeActor::resolve_repository(obcx::core::ActorContext &context)
    -> std::shared_ptr<BridgeStateRepository> {
  std::scoped_lock lock(runtime_mutex_);
  if (repository_) {
    return repository_;
  }
  if (auto injected = context.get_service<BridgeStateRepository>()) {
    // An injected repository is an explicit test/embedding seam and is
    // expected to have completed its own initialization before registration.
    repository_ = std::move(injected);
    return repository_;
  }

  auto db_manager = context.get_service<obcx::core::DbManager>();
  if (!db_manager) {
    throw std::runtime_error("bridge requires DbManager service");
  }

  auto db_instance = context.db_instance().empty() ? std::string{"main"}
                                                   : context.db_instance();
  auto db_namespace = context.db_namespace().empty() ? context.actor_id()
                                                     : context.db_namespace();
  auto candidate = std::make_shared<BridgeStateRepository>(
      *db_manager, std::move(db_instance), std::move(db_namespace));

  const auto generation =
      context.get_service<obcx::core::ActorGenerationInfo>();
  const auto reload_candidate =
      generation && generation->purpose ==
                        obcx::core::ActorGenerationPurpose::ReloadCandidate;
  if (reload_candidate) {
    candidate->validate_schema();
  } else {
    candidate->initialize_schema();
  }
  // Publish only a repository whose current schema has been verified.
  repository_ = std::move(candidate);
  return repository_;
}

auto BridgeActor::resolve_config(obcx::core::ActorContext &context)
    -> std::shared_ptr<const BridgeConfig> {
  std::scoped_lock lock(runtime_mutex_);
  if (!config_) {
    config_ = load_bridge_config(context.config());
  }
  return config_;
}

auto BridgeActor::prepare_generation(obcx::core::ActorContext &context)
    -> obcx::core::ActorPreparationResult {
  try {
    const auto config = resolve_config(context);
    const auto publisher =
        context.get_service<obcx::command::AvailabilityPublisher>();
    const auto resolver =
        context.get_service<obcx::command::CommandScopeResolver>();
    if (!publisher || !resolver) {
      return obcx::core::ActorPreparationResult::failed(
          "bridge requires command availability and scope resolver services");
    }
    // Telegram command routes retain their QQ target even when QQ-to-Telegram
    // forwarding is disabled and no QQ status-command scope is published.
    // Validate that destination through the same pure configuration boundary
    // before ValidationOnly can return, without constructing a DB/runtime.
    for (const auto &route : bridge_command_routes(*config, "recall")) {
      const auto *pair = config->pair(route.pair_id);
      if (!pair ||
          !resolver
               ->resolve_group(
                   {"qq", pair->onebot11_installation, route.target_group,
                    obcx::command::TopicSelection::None, std::nullopt})
               .ok()) {
        return obcx::core::ActorPreparationResult::failed(
            "bridge command target scope is unavailable");
      }
    }
    for (const auto name :
         {"bridge_status", "recall", "poke", "userinfo", "react", "unreact"}) {
      const auto configured = bridge_command_scopes(*config, name);
      std::vector<obcx::command::ResolvedGroupScope> scopes;
      scopes.reserve(configured.size());
      for (const auto &selector : configured) {
        auto resolved = resolver->resolve_group(selector);
        if (!resolved.ok()) {
          return obcx::core::ActorPreparationResult::failed(
              "bridge command scope is unavailable");
        }
        scopes.push_back(std::move(*resolved.value));
      }
      // Retain the original None/Exact/Any selectors, not just the canonical
      // conversation: Telegram general-topic identity alone is not an ACL.
      resolver->publish_scopes(*publisher, name, scopes);
    }
    const auto generation =
        context.get_service<obcx::core::ActorGenerationInfo>();
    if (generation && generation->purpose ==
                          obcx::core::ActorGenerationPurpose::ValidationOnly) {
      return obcx::core::ActorPreparationResult::ready();
    }
    const auto repository = resolve_repository(context);
    if (config->enable_retry_queue && !delivery_retry_worker_) {
      const auto executor = context.get_service<asio::any_io_executor>();
      const auto gateway =
          context.get_service<obcx::bot::BotOperationGateway>();
      const auto blocking = context.get_service<obcx::core::BlockingExecutor>();
      const auto references =
          context.get_service<obcx::bot::ConversationResolver>();
      const auto lifecycle =
          context.get_service<obcx::core::ActorGenerationLifecycle>();
      const auto events =
          context.get_service<obcx::core::ActorEventPublisher>();
      if (!executor || !gateway || !blocking || !references || !lifecycle ||
          !events)
        return obcx::core::ActorPreparationResult::failed(
            "bridge typed retry requires generation services");
      auto worker = std::make_shared<BridgeDeliveryRetryWorker>(
          *executor, gateway, repository, blocking, references, lifecycle,
          events, *config,
          static_cast<std::int64_t>(obcx::bot::detail::domain_maximum_items));
      std::weak_ptr<BridgeDeliveryRetryWorker> weak = worker;
      lifecycle->subscribe(
          [weak](
              obcx::core::ActorGenerationLifecycle::TokenPtr token) noexcept {
            if (auto current = weak.lock())
              current->start(std::move(token));
          },
          [weak]() noexcept {
            if (auto current = weak.lock())
              current->stop();
          });
      delivery_retry_worker_ = std::move(worker);
    }
    // Album lifecycle subscriptions must exist before activation seals the
    // service. Constructing this runtime does not start timers or provider I/O.
    const auto forwarding_executor =
        context.get_service<asio::any_io_executor>();
    if (!forwarding_executor ||
        !resolve_forwarder(context, *forwarding_executor))
      return obcx::core::ActorPreparationResult::failed(
          "bridge album owner requires prepared forwarding services");
    return obcx::core::ActorPreparationResult::ready();
  } catch (const std::exception &error) {
    return obcx::core::ActorPreparationResult::failed(error.what());
  } catch (...) {
    return obcx::core::ActorPreparationResult::failed(
        "bridge generation preparation failed with an unknown exception");
  }
}

auto BridgeActor::resolve_forwarder(obcx::core::ActorContext &context,
                                    boost::asio::any_io_executor executor)
    -> std::shared_ptr<IBridgeForwarder> {
  if (auto injected = context.get_service<IBridgeForwarder>()) {
    return injected;
  }
  {
    std::scoped_lock lock(runtime_mutex_);
    if (forwarder_) {
      return forwarder_;
    }
  }

  auto operation_client = context.get_service<obcx::bot::BotOperationGateway>();
  if (!operation_client) {
    return nullptr;
  }

  auto db_manager = context.get_service<obcx::core::DbManager>();
  if (!db_manager) {
    throw std::runtime_error("bridge forwarding requires DbManager service");
  }
  auto blocking_executor = context.get_service<obcx::core::BlockingExecutor>();
  const auto command_scopes =
      context.get_service<obcx::command::CommandScopeResolver>();
  const auto references =
      context.get_service<obcx::bot::ConversationResolver>();
  const auto lifecycle =
      context.get_service<obcx::core::ActorGenerationLifecycle>();
  const auto publisher = context.get_service<obcx::core::ActorEventPublisher>();
  if (!blocking_executor || !command_scopes || !references || !lifecycle ||
      !publisher) {
    throw std::runtime_error("bridge forwarding requires blocking, command, "
                             "reference services");
  }
  // These accessors take runtime_mutex_ themselves. Resolve them before the
  // final publication lock rather than recursively locking a plain mutex.
  const auto config = resolve_config(context);
  const auto repository = resolve_repository(context);
  std::scoped_lock lock(runtime_mutex_);
  if (forwarder_) {
    return forwarder_;
  }

  const auto db_instance = context.db_instance().empty()
                               ? std::string{"main"}
                               : context.db_instance();
  received_message_repository_ = std::make_shared<ReceivedMessageRepository>(
      *db_manager, db_instance, "message_store");

  forwarder_ = std::make_shared<BridgeForwardingRuntime>(
      std::move(operation_client), config, repository,
      received_message_repository_, std::move(executor),
      std::move(blocking_executor), command_scopes, references, lifecycle,
      publisher);
  return forwarder_;
}

auto BridgeActor::handle(const commands::RecallCommand &request,
                         const obcx::core::MessageEnvelope &message,
                         obcx::core::ActorContext &context)
    -> obcx::core::ActorTask<obcx::core::ActorResult> {
  return handle_command(request.invocation, message, context);
}

auto BridgeActor::handle(const commands::BridgeStatusCommand &request,
                         const obcx::core::MessageEnvelope &message,
                         obcx::core::ActorContext &context)
    -> obcx::core::ActorTask<obcx::core::ActorResult> {
  return handle_command(request.invocation, message, context);
}

auto BridgeActor::handle(const commands::PokeCommand &request,
                         const obcx::core::MessageEnvelope &message,
                         obcx::core::ActorContext &context)
    -> obcx::core::ActorTask<obcx::core::ActorResult> {
  return handle_command(request.invocation, message, context);
}

auto BridgeActor::handle(const commands::UserInfoCommand &request,
                         const obcx::core::MessageEnvelope &message,
                         obcx::core::ActorContext &context)
    -> obcx::core::ActorTask<obcx::core::ActorResult> {
  return handle_command(request.invocation, message, context);
}

auto BridgeActor::handle(const commands::ReactCommand &request,
                         const obcx::core::MessageEnvelope &message,
                         obcx::core::ActorContext &context)
    -> obcx::core::ActorTask<obcx::core::ActorResult> {
  return handle_command(request.invocation, message, context);
}

auto BridgeActor::handle(const commands::UnreactCommand &request,
                         const obcx::core::MessageEnvelope &message,
                         obcx::core::ActorContext &context)
    -> obcx::core::ActorTask<obcx::core::ActorResult> {
  return handle_command(request.invocation, message, context);
}

auto BridgeActor::handle_command(
    const obcx::command::CommandInvocation &invocation,
    const obcx::core::MessageEnvelope &message,
    obcx::core::ActorContext &context)
    -> obcx::core::ActorTask<obcx::core::ActorResult> {
  try {
    auto executor = context.get_service<asio::any_io_executor>();
    if (!executor) {
      co_return obcx::core::ActorResult::failed(
          "bridge_command_executor_missing",
          "bridge command handling requires an Asio executor", true);
    }
    auto forwarder =
        co_await context.run_blocking([this, &context, executor = *executor] {
          return resolve_forwarder(context, executor);
        });
    if (!forwarder) {
      co_return obcx::core::ActorResult::failed(
          "bridge_command_runtime_unavailable",
          "bridge command runtime is unavailable", true);
    }
    const auto handled = co_await context.await_asio(
        *executor, [forwarder, invocation]() -> asio::awaitable<bool> {
          co_return co_await forwarder->handle_command(invocation);
        });
    if (!handled) {
      co_return obcx::core::ActorResult::failed(
          "bridge_command_unsupported",
          "bridge command is unsupported for the source platform", false);
    }
    auto result = obcx::core::ActorResult::success();
    result.emit(
        obcx::command::CommandCompleted{
            .transaction_id = invocation.transaction_id,
            .propagation = obcx::command::Propagation::Continue,
        },
        message);
    co_return result;
  } catch (const BridgeBotSendFailure &error) {
    const auto disposition = classify_bridge_operation_failure(error);
    co_return obcx::core::ActorResult::failed(
        "bridge_command_send_failed", disposition.diagnostic,
        disposition.retryable && (invocation.name == "bridge_status" ||
                                  invocation.name == "userinfo"));
  } catch (const std::exception &error) {
    // A failed feedback send must not repeat an earlier delete/poke/reaction.
    // Unclassified exceptions cannot establish zero effects for any command.
    co_return obcx::core::ActorResult::failed("bridge_command_failed",
                                              error.what(), false);
  }
}

auto BridgeActor::handle(
    const obcx::message_store::events::MessageStored &stored,
    const obcx::core::MessageEnvelope &message,
    obcx::core::ActorContext &context)
    -> obcx::core::ActorTask<obcx::core::ActorResult> {
  if (message.headers.contains(std::string{obcx::command::processed_header})) {
    co_return obcx::core::ActorResult::success();
  }
  // Check the original stored source before repository resolution or awaits.
  // These archival keys are not reconstructed from opaque domain IDs.
  try {
    stored.message.validate();
    stored.source.validate(stored.message);
    const auto &platform = stored.source.source_platform;
    const auto &surface =
        stored.message.message.conversation.installation.surface;
    if ((platform != "qq" && platform != "telegram") ||
        (platform == "qq" && surface != obcx::bot::SurfaceId{"onebot11.qq"}) ||
        (platform == "telegram" &&
         surface != obcx::bot::SurfaceId{"telegram.bot_api"})) {
      throw std::invalid_argument("MessageStored source platform mismatch");
    }
    if (stored.source.source_platform != message.source_platform ||
        stored.source.source_bot != message.source_bot ||
        stored.source.conversation_id != message.conversation_id ||
        stored.source.message_id !=
            message.payload.at("message_id").get_ref<const std::string &>() ||
        stored.payload != message.payload) {
      throw std::invalid_argument("MessageStored source/envelope mismatch");
    }
  } catch (const std::exception &error) {
    auto result = obcx::core::ActorResult::failed("invalid_stored_source",
                                                  error.what(), false);
    result.emit(build_failed_event(message, "invalid_stored_source",
                                   error.what(), false));
    co_return result;
  }
  try {
    auto admitted = stored;
    auto forwarded = message;
    auto executor = context.get_service<asio::any_io_executor>();
    if (!executor) {
      throw std::runtime_error(
          "bridge forwarding requires an Asio executor service");
    }
    auto forwarder =
        co_await context.run_blocking([this, &context, executor = *executor] {
          return resolve_forwarder(context, executor);
        });
    if (forwarder) {
      auto forward_result = co_await context.await_asio(
          *executor,
          [forwarder, stored = std::move(admitted),
           forwarded = std::move(
               forwarded)]() mutable -> asio::awaitable<BridgeForwardResult> {
            co_return co_await forwarder->forward_message(stored, forwarded);
          });
      if (forward_result.domain_revision) {
        const auto &revision = *forward_result.domain_revision;
        if (revision.newly_completed) {
          auto result = obcx::core::ActorResult::success();
          result.emit(bridge_revision_notification(revision));
          co_return result;
        }
        if (revision.persisted.checkpoint &&
            revision.persisted.checkpoint->complete &&
            revision.persisted.observations_match_checkpoint)
          co_return obcx::core::ActorResult::success(); // No replay
                                                        // publication.
        auto result = obcx::core::ActorResult::failed(
            "bridge_edit_incomplete", "bridge edit is incomplete or uncertain",
            false);
        auto notification =
            build_failed_event(message, "bridge_edit_incomplete",
                               "bridge edit is incomplete or uncertain", false);
        notification.payload["delivery_id"] =
            revision.persisted.admission.delivery_id;
        notification.payload["revision_id"] =
            revision.persisted.admission.revision_id;
        notification.payload["observed_target_messages"] =
            bridge_revision_output_messages(revision.persisted);
        result.emit(std::move(notification));
        co_return result;
      }
      if (forward_result.domain_delivery) {
        const auto &delivery = *forward_result.domain_delivery;
        if (delivery.newly_completed) {
          if (delivery.persisted.state != ledger::DeliveryState::Complete ||
              !delivery.outcome ||
              !std::holds_alternative<obcx::bot::SendReceipt>(
                  *delivery.outcome)) {
            throw std::runtime_error(
                "invalid bridge logical completion evidence");
          }
          // All outputs were persisted by the delivery sender. Never write a
          // v3 single-output mapping or publish from physical confirmations.
          auto notification = bridge_delivery_notification(delivery);
          auto result = obcx::core::ActorResult::success();
          result.emit(std::move(notification));
          co_return result;
        }
        if (delivery.persisted.state == ledger::DeliveryState::Complete) {
          co_return obcx::core::ActorResult::success(); // Replay, no
                                                        // publication.
        }
        // Even a zero-effect retry belongs to the durable retry owner, not a
        // replay of MessageStored or the predecessor CQ retry queue.
        std::string diagnostic = "bridge delivery is not complete";
        if (delivery.outcome) {
          if (const auto *failure =
                  std::get_if<obcx::bot::SendFailure>(&*delivery.outcome)) {
            // Reuse the redacted error diagnostic without discarding the send's
            // confirmed/possible effects or granting ingress retry permission.
            const auto reason = classify_bridge_operation_failure(
                BridgeBotOperationFailure{failure->error});
            diagnostic +=
                ": " + reason.diagnostic + "; confirmed=" +
                std::to_string(failure->confirmed_messages.size()) +
                "; possible_unconfirmed=" +
                (failure->may_have_unconfirmed_messages ? "true" : "false");
          } else {
            diagnostic += ": SDK receipt awaits ledger reconciliation";
          }
        }
        auto result = obcx::core::ActorResult::failed(
            "bridge_delivery_incomplete", diagnostic, false);
        auto notification = build_failed_event(
            message, "bridge_delivery_incomplete", diagnostic, false);
        notification.payload["delivery_id"] = delivery.persisted.delivery_id;
        notification.payload["target_messages"] =
            bridge_delivery_output_messages(delivery.persisted);
        result.emit(std::move(notification));
        co_return result;
      }
      if (forward_result.disposition ==
          DirectForwardDisposition::NotForwarded) {
        // Unmatched routes and accepted album batches produce no immediate
        // logical notification. Replays were handled by the durable result.
        co_return obcx::core::ActorResult::success();
      }
      // A scalar predecessor result cannot prove a complete v4 delivery.
      // Never write a legacy mapping or authorize whole-message replay here.
      throw std::runtime_error(
          "bridge forwarder returned no durable delivery or revision result");
    }

    // Stored payload fields are archival evidence, not send receipts. Missing
    // runtime services must not select the predecessor payload-to-mapping path.
    throw std::runtime_error("bridge forwarding runtime is unavailable");
  } catch (const BridgeDeliveryPersistenceFailure &error) {
    auto result = obcx::core::ActorResult::failed(
        "bridge_delivery_persistence_failed", error.what(), false);
    result.emit(bridge_delivery_failure_notification(error));
    co_return result;
  } catch (const BridgeRevisionPersistenceFailure &error) {
    auto result = obcx::core::ActorResult::failed(
        "bridge_edit_persistence_failed", error.what(), false);
    result.emit(bridge_revision_failure_notification(error.result()));
    co_return result;
  } catch (const BridgeRetryUnavailable &error) {
    auto result = obcx::core::ActorResult::failed("bridge_retry_unavailable",
                                                  error.what(), true);
    result.emit(build_failed_event(message, "bridge_retry_unavailable",
                                   error.what(), true));
    co_return result;
  } catch (const std::exception &error) {
    // An unclassified exception may follow durable attempt admission or a
    // provider effect. It cannot authorize replay of the original message.
    auto result =
        obcx::core::ActorResult::failed("bridge_error", error.what(), false);
    result.emit(
        build_failed_event(message, "bridge_error", error.what(), false));
    co_return result;
  }
}

auto BridgeActor::refresh_platform_activity(
    const obcx::core::MessageEnvelope &message,
    obcx::core::ActorContext &context)
    -> obcx::core::ActorTask<obcx::core::ActorResult> {
  try {
    auto repository = co_await context.run_blocking(
        [this, &context] { return resolve_repository(context); });
    const auto observed_at = std::chrono::system_clock::now();
    (void)co_await context.run_blocking(
        [repository, installation = message.source_bot,
         platform = message.source_platform, observed_at] {
          return repository->update_platform_heartbeat(installation, platform,
                                                       observed_at);
        });
    co_return obcx::core::ActorResult::success();
  } catch (const std::exception &error) {
    co_return obcx::core::ActorResult::failed("bridge_activity_update_failed",
                                              error.what(), true);
  }
}

auto BridgeActor::handle(const obcx::core::events::RawMessageEvent &event,
                         const obcx::core::MessageEnvelope &message,
                         obcx::core::ActorContext &context)
    -> obcx::core::ActorTask<obcx::core::ActorResult> {
  (void)event;
  return refresh_platform_activity(message, context);
}

auto BridgeActor::handle(const obcx::core::events::BotSendConfirmedEvent &event,
                         const obcx::core::MessageEnvelope &message,
                         obcx::core::ActorContext &context)
    -> obcx::core::ActorTask<obcx::core::ActorResult> {
  if (!is_bridge_delivery_confirmation(event.confirmation))
    co_return obcx::core::ActorResult::success();
  try {
    auto confirmation = event.confirmation;
    confirmation.validate();
    const auto &installation =
        confirmation.output.message.conversation.installation;
    const auto expected_surface =
        message.source_platform == "qq"
            ? obcx::bot::SurfaceId{"onebot11.qq"}
            : obcx::bot::SurfaceId{"telegram.bot_api"};
    if ((message.source_platform != "qq" &&
         message.source_platform != "telegram") ||
        installation.surface != expected_surface ||
        message.source_bot != installation.installation_id ||
        message.correlation_id != confirmation.attempt_id)
      throw std::invalid_argument("bridge confirmation envelope mismatch");
    // Observations never instantiate the predecessor forwarder/retry worker.
    // Own the original confirmation before repository resolution and
    // suspension.
    co_await context.run_blocking(
        [this, &context, confirmation = std::move(confirmation)] {
          auto repository = resolve_repository(context);
          // The persisted attempt supplies the original target scope. A later
          // route edit must not discard an already observed physical effect.
          (void)record_bridge_delivery_confirmation(*repository, confirmation);
        });
    co_return obcx::core::ActorResult::success();
  } catch (const std::invalid_argument &error) {
    co_return obcx::core::ActorResult::failed("invalid_bridge_confirmation",
                                              error.what(), false);
  } catch (const std::exception &error) {
    // This retries only an idempotent observation, never a provider submission.
    co_return obcx::core::ActorResult::failed(
        "bridge_confirmation_persistence_failed", error.what(), true);
  }
}

auto BridgeActor::handle(const obcx::core::events::BotMessageSentEvent &event,
                         const obcx::core::MessageEnvelope &message,
                         obcx::core::ActorContext &context)
    -> obcx::core::ActorTask<obcx::core::ActorResult> {
  (void)event;
  return refresh_platform_activity(message, context);
}

auto BridgeActor::handle(const obcx::core::events::RawHeartbeatEvent &heartbeat,
                         const obcx::core::MessageEnvelope &message,
                         obcx::core::ActorContext &context)
    -> obcx::core::ActorTask<obcx::core::ActorResult> {
  (void)heartbeat;
  return refresh_platform_activity(message, context);
}

auto BridgeActor::handle(const obcx::core::events::RawNoticeEvent &notice,
                         const obcx::core::MessageEnvelope &message,
                         obcx::core::ActorContext &context)
    -> obcx::core::ActorTask<obcx::core::ActorResult> {
  try {
    const auto deleted = notice.message_deleted();
    if (deleted) {
      const auto &source = deleted->message.conversation;
      deleted->validate();
      if (notice.payload != message.payload ||
          message.source_platform != "qq" ||
          source.installation.surface != obcx::bot::SurfaceId{"onebot11.qq"} ||
          source.installation.installation_id != message.source_bot)
        co_return obcx::core::ActorResult::failed(
            "bridge_notice_source_mismatch",
            "deleted notice installation or envelope mismatch", false);
      // The normalized typed subject owns scope. A scalar envelope routing
      // key cannot import a reference or authorize a mutation.
      // Observation consistency is not mutation authority. The forwarding
      // owner still needs exact persisted outputs and durable admission.
    }
    auto executor = context.get_service<asio::any_io_executor>();
    if (!executor) {
      co_return obcx::core::ActorResult::failed(
          "bridge_notice_executor_missing",
          "bridge notice handling requires an Asio executor", true);
    }
    auto forwarder =
        co_await context.run_blocking([this, &context, executor = *executor] {
          return resolve_forwarder(context, executor);
        });
    if (!forwarder) {
      co_return obcx::core::ActorResult::failed(
          "bridge_notice_runtime_unavailable",
          "bridge notice runtime is unavailable", true);
    }
    co_await context.await_asio(*executor,
                                [forwarder, forwarded = message,
                                 deleted]() mutable -> asio::awaitable<void> {
                                  (void)co_await forwarder->handle_notice(
                                      forwarded, std::move(deleted));
                                });
    co_return obcx::core::ActorResult::success();
  } catch (const BridgeRecallPropagationFailure &error) {
    co_return obcx::core::ActorResult::failed(
        "bridge_recall_unavailable_or_uncertain", error.what(), false);
  } catch (const std::exception &error) {
    co_return obcx::core::ActorResult::failed("bridge_notice_failed",
                                              error.what(), true);
  }
}

#ifndef OBCX_BRIDGE_ACTOR_NO_EXPORT
OBCX_ACTOR_EXPORT_V2(bridge::BridgeActor)
#endif

} // namespace bridge
