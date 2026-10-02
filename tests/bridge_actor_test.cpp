#include "actor_config_fixture.hpp"
#include "bridge_actor.hpp"
#include "bridge_forwarder.hpp"
#include "bridge_state_repository.hpp"
#include "common/config_snapshot.hpp"
#include "config.hpp"
#include "core/actor/blocking_executor.hpp"
#include "core/actor/native_actor_scheduler.hpp"
#include "core/infrastructure/db_manager.hpp"
#include "qq/message_formatter.hpp"
#include "qq/photo_normalizer.hpp"
#include "telegram/handler.hpp"
#include "telegram/media_group_buffer.hpp"
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <utility>

namespace asio = boost::asio;

namespace {

auto run_actor(std::shared_ptr<obcx::core::ActorServices> services,
               obcx::core::MessageEnvelope message) -> obcx::core::ActorResult {
  using namespace std::chrono_literals;

  asio::io_context ioc;
  auto blocking_executor = std::make_shared<obcx::core::BlockingExecutor>(2);
  services->register_service<obcx::core::BlockingExecutor>(blocking_executor);
  services->register_service<asio::any_io_executor>(
      std::make_shared<asio::any_io_executor>(ioc.get_executor()));
  auto work = asio::make_work_guard(ioc);
  std::jthread io_thread([&ioc] { ioc.run(); });

  obcx::core::NativeActorScheduler scheduler(
      obcx::core::NativeActorSchedulerOptions{.worker_count = 2}, services);
  scheduler.register_actor(std::make_shared<bridge::BridgeActor>());
  std::promise<obcx::core::ActorResult> completion;
  auto future = completion.get_future();
  if (!scheduler.enqueue(
          obcx::core::ActorInvocation{.actor_id = "bridge",
                                      .partition_key = "test",
                                      .db_instance = "main",
                                      .db_namespace = "bridge",
                                      .message = std::move(message)},
          [&completion](obcx::core::ActorResult result) {
            completion.set_value(std::move(result));
          })) {
    throw std::runtime_error("native bridge scheduler rejected invocation");
  }
  if (future.wait_for(5s) != std::future_status::ready) {
    scheduler.shutdown(obcx::core::ActorExecutorShutdownMode::Cancel);
    throw std::runtime_error("native bridge invocation timed out");
  }
  auto result = future.get();
  scheduler.shutdown();
  blocking_executor->shutdown();
  work.reset();
  ioc.stop();
  return result;
}

auto temp_db_path(const std::string &name) -> std::filesystem::path {
  return std::filesystem::temp_directory_path() /
         ("obcx_bridge_actor_" + name + "_" +
          std::to_string(
              std::chrono::steady_clock::now().time_since_epoch().count()) +
          ".sqlite3");
}

auto sqlite_config(const std::filesystem::path &path)
    -> obcx::common::DbInstanceConfig {
  obcx::common::DbInstanceConfig config;
  config.name = "main";
  config.type = "sqlite";
  config.path = path.string();
  return config;
}

auto message_stored(const std::string &source_message_id,
                    const std::string &target_message_id)
    -> obcx::core::MessageEnvelope {
  obcx::core::MessageEnvelope envelope;
  envelope.id = "stored-" + source_message_id;
  envelope.type = "obcx::message_store::events::MessageStored";
  envelope.source_platform = "qq";
  envelope.source_bot = "qq-main";
  envelope.correlation_id = "corr-" + source_message_id;
  envelope.conversation_id = "group:group-7";
  envelope.payload = {
      {"message_id", source_message_id},
      {"group_id", "group-7"},
      {"target_platform", "telegram"},
      {"target_bot", "tg-main"},
      {"target_conversation_id", "chat:tg-group"},
      {"target_message_id", target_message_id},
  };
  return envelope;
}

auto raw_heartbeat(std::string platform, std::string installation,
                   std::chrono::system_clock::time_point event_time)
    -> obcx::core::MessageEnvelope {
  obcx::core::events::RawHeartbeatEvent heartbeat{
      .payload = {{"interval_ms", 30000}},
  };
  obcx::core::MessageEnvelope envelope;
  envelope.id = "heartbeat-" + installation;
  envelope.type =
      obcx::core::canonical_message_type_name<decltype(heartbeat)>();
  envelope.source_platform = std::move(platform);
  envelope.source_bot = std::move(installation);
  envelope.conversation_id = "global";
  envelope.timestamp = event_time;
  envelope.payload = heartbeat;
  return envelope;
}

auto raw_message_activity(std::string platform, std::string installation,
                          std::chrono::system_clock::time_point event_time)
    -> obcx::core::MessageEnvelope {
  obcx::core::events::RawMessageEvent event{
      .payload = {{"message_id", "activity"}},
  };
  obcx::core::MessageEnvelope envelope;
  envelope.id = "message-" + installation;
  envelope.type = obcx::core::canonical_message_type_name<decltype(event)>();
  envelope.source_platform = std::move(platform);
  envelope.source_bot = std::move(installation);
  envelope.conversation_id = "group:42";
  envelope.timestamp = event_time;
  envelope.payload = event;
  return envelope;
}

auto installation_for(const std::string_view platform) -> std::string {
  return platform == "qq" ? "qq-main" : "tg-main";
}

auto mapped_target(bridge::BridgeStateRepository &repository,
                   const std::string &source_platform,
                   const std::string &source_message_id,
                   const std::string &target_platform)
    -> std::optional<std::string> {
  const auto result = repository.resolve_target_mapping(
      {.installation_id = installation_for(source_platform),
       .platform = source_platform,
       .conversation_id =
           source_platform == "qq" ? "group:qq-group" : "chat:tg-group",
       .message_id = source_message_id},
      {.installation_id = installation_for(target_platform),
       .platform = target_platform,
       .conversation_id =
           target_platform == "qq" ? "group:qq-group" : "chat:tg-group"});
  return result.unique()
             ? std::optional<std::string>{result.mapping->target_message_id}
             : std::nullopt;
}

} // namespace

class RecordingForwarder final : public bridge::IBridgeForwarder {
public:
  explicit RecordingForwarder(bridge::BridgeForwardResult result,
                              const bool infer_conversations = true)
      : result_(std::move(result)), infer_conversations_(infer_conversations) {}

  auto forward_message(const obcx::core::MessageEnvelope &message)
      -> asio::awaitable<bridge::BridgeForwardResult> override {
    seen_messages.push_back(message);
    auto result = result_;
    if (infer_conversations_ && result.source_conversation_id.empty()) {
      result.source_conversation_id = message.conversation_id;
    }
    if (infer_conversations_ && result.target_conversation_id.empty() &&
        !result.target_platform.empty()) {
      result.target_conversation_id =
          result.target_platform == "qq" ? "group:qq-group" : "chat:tg-group";
    }
    co_return result;
  }

  auto handle_command(const obcx::command::CommandInvocation &invocation)
      -> asio::awaitable<bool> override {
    seen_commands.push_back(invocation);
    co_return true;
  }

  auto handle_notice(const obcx::core::MessageEnvelope &notice)
      -> asio::awaitable<bool> override {
    seen_notices.push_back(notice);
    co_return true;
  }

  std::vector<obcx::core::MessageEnvelope> seen_messages;
  std::vector<obcx::core::MessageEnvelope> seen_notices;
  std::vector<obcx::command::CommandInvocation> seen_commands;

private:
  bridge::BridgeForwardResult result_;
  bool infer_conversations_ = true;
};

class SuspendingForwarder final : public bridge::IBridgeForwarder {
public:
  auto forward_message(const obcx::core::MessageEnvelope &message)
      -> asio::awaitable<bridge::BridgeForwardResult> override {
    auto executor = co_await asio::this_coro::executor;
    asio::steady_timer timer(executor, std::chrono::milliseconds(20));
    co_await timer.async_wait(asio::use_awaitable);
    seen_message = message;
    co_return bridge::BridgeForwardResult{
        .disposition = bridge::DirectForwardDisposition::NewDelivery,
        .source_platform = "qq",
        .source_bot = "qq-main",
        .source_conversation_id = "group:qq-group",
        .source_message_id = "qq-media-1",
        .target_platform = "telegram",
        .target_bot = "tg-main",
        .target_conversation_id = "chat:tg-group",
        .target_message_id = "tg-media-1",
    };
  }

  std::optional<obcx::core::MessageEnvelope> seen_message;
};

class ThrowingForwarder final : public bridge::IBridgeForwarder {
public:
  auto forward_message(const obcx::core::MessageEnvelope &)
      -> asio::awaitable<bridge::BridgeForwardResult> override {
    throw std::runtime_error("simulated forwarding failure");
    co_return bridge::BridgeForwardResult{};
  }
};

class HangingForwarder final : public bridge::IBridgeForwarder {
public:
  auto started() -> std::future<void> { return started_.get_future(); }

  auto forward_message(const obcx::core::MessageEnvelope &)
      -> asio::awaitable<bridge::BridgeForwardResult> override {
    started_.set_value();
    auto executor = co_await asio::this_coro::executor;
    asio::steady_timer timer(executor, std::chrono::seconds(30));
    co_await timer.async_wait(asio::use_awaitable);
    resumed_after_wait.store(true, std::memory_order_release);
    co_return bridge::BridgeForwardResult{};
  }

  std::atomic_bool resumed_after_wait = false;

private:
  std::promise<void> started_;
};

TEST(BridgeActorTest, AvailabilityScopesMatchExistingCommandRouteResolution) {
  bridge::BridgeConfig config;
  bridge::BridgeInstallationPair first{"a", "tg-a", "qq-a", {}};
  first.group_map.emplace(
      "-10", bridge::GroupBridgeConfig{"-10", "10", true, true, true, false});
  first.group_map.emplace(
      "-11", bridge::GroupBridgeConfig{"-11", "11", true, true, false, true});
  first.group_map.emplace("-20",
                          bridge::GroupBridgeConfig{
                              "-20", std::vector<bridge::TopicBridgeConfig>{
                                         {42, "20", true, true, true, false},
                                         {43, "21", true, true, false, true}}});
  config.installation_pairs.emplace("a", first);
  config.installation_pairs.emplace(
      "b", bridge::BridgeInstallationPair{
               "b",
               "tg-b",
               "qq-b",
               {{"-10", bridge::GroupBridgeConfig{"-10", "10"}}}});
  const std::vector<std::optional<std::int64_t>> topics{std::nullopt, 42, 43,
                                                        44};
  for (const auto name : {"bridge_status", "recall", "poke", "unsupported"}) {
    const auto scopes = bridge::bridge_command_scopes(config, name);
    for (const auto platform : {"qq", "telegram"}) {
      for (const auto bot : {"qq-a", "qq-b", "tg-a", "tg-b", "unknown"}) {
        for (const auto group :
             {"10", "11", "20", "21", "999", "-10", "-11", "-20"}) {
          for (const auto topic : topics) {
            const obcx::command::Subject subject{
                platform, bot, obcx::command::ConversationKind::Group,
                group,    "7", topic};
            const auto *pair = config.pair_for_source(platform, bot);
            bool expected = false;
            if (pair && std::string_view{platform} == "qq" && !topic &&
                std::string_view{name} == "bridge_status") {
              expected = !pair->tg_group_and_topic_id(group).first.empty();
            } else if (pair && std::string_view{platform} == "telegram" &&
                       std::string_view{name} != "unsupported") {
              expected = !pair->qq_group_id_for_topic(group, topic.value_or(-1))
                              .empty();
            }
            EXPECT_EQ(obcx::command::matches(scopes, subject), expected)
                << name << ":" << bot << ":" << group;
            EXPECT_EQ(bridge::resolve_bridge_command(config, name, subject)
                          .has_value(),
                      expected);
          }
        }
      }
    }
    EXPECT_FALSE(obcx::command::matches(
        scopes, {"qq", "qq-a", obcx::command::ConversationKind::Private, "10",
                 "7", std::nullopt}));
  }
}

TEST(BridgeActorTest, PersistsNativeHeartbeatAtLocalObservationTime) {
  const auto db_path = temp_db_path("heartbeat");
  auto db_manager = std::make_shared<obcx::core::DbManager>();
  db_manager->configure({sqlite_config(db_path)});
  auto repository = std::make_shared<bridge::BridgeStateRepository>(
      *db_manager, "main", "bridge");
  repository->initialize_schema();
  auto services = std::make_shared<obcx::core::ActorServices>();
  services->register_service<bridge::BridgeStateRepository>(repository);
  const auto old_event_time =
      std::chrono::system_clock::time_point{std::chrono::milliseconds{1}};
  const auto before = std::chrono::system_clock::time_point{
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())};

  const auto result =
      run_actor(services, raw_heartbeat("qq", "qq-main", old_event_time));
  const auto after = std::chrono::system_clock::now();

  ASSERT_TRUE(result.ok());
  const auto activity = repository->get_platform_heartbeat("qq-main");
  ASSERT_TRUE(activity.has_value());
  EXPECT_EQ(activity->platform, "qq");
  EXPECT_GE(activity->last_heartbeat_at, before);
  EXPECT_LE(activity->last_heartbeat_at, after);
  std::filesystem::remove(db_path);
}

TEST(BridgeActorTest, PersistsTelegramMessageAtLocalObservationTime) {
  const auto db_path = temp_db_path("message-activity");
  auto db_manager = std::make_shared<obcx::core::DbManager>();
  db_manager->configure({sqlite_config(db_path)});
  auto repository = std::make_shared<bridge::BridgeStateRepository>(
      *db_manager, "main", "bridge");
  repository->initialize_schema();
  auto services = std::make_shared<obcx::core::ActorServices>();
  services->register_service<bridge::BridgeStateRepository>(repository);
  const auto old_event_time =
      std::chrono::system_clock::time_point{std::chrono::milliseconds{1}};
  const auto before = std::chrono::system_clock::time_point{
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())};

  const auto result =
      run_actor(services, raw_message_activity("telegram", "telegram-main",
                                               old_event_time));
  const auto after = std::chrono::system_clock::now();

  ASSERT_TRUE(result.ok());
  const auto activity = repository->get_platform_heartbeat("telegram-main");
  ASSERT_TRUE(activity.has_value());
  EXPECT_EQ(activity->platform, "telegram");
  EXPECT_GE(activity->last_heartbeat_at, before);
  EXPECT_LE(activity->last_heartbeat_at, after);
  std::filesystem::remove(db_path);
}

TEST(BridgeActorTest, ProcessedStoredCommandIsNotForwardedOrMappedAgain) {
  auto services = std::make_shared<obcx::core::ActorServices>();
  auto forwarder =
      std::make_shared<RecordingForwarder>(bridge::BridgeForwardResult{});
  services->register_service<bridge::IBridgeForwarder>(forwarder);
  auto stored = message_stored("command-source", "");
  stored.payload.erase("target_platform");
  stored.payload.erase("target_message_id");
  stored.headers.emplace(std::string{obcx::command::processed_header}, "true");

  const auto result = run_actor(services, std::move(stored));

  EXPECT_TRUE(result.ok());
  EXPECT_TRUE(result.emitted.empty());
  EXPECT_TRUE(forwarder->seen_messages.empty());
}

TEST(BridgeActorTest, EmitsMessageForwardFailedWhenMappingFieldsAreMissing) {
  const auto db_path = temp_db_path("failed");
  auto db_manager = std::make_shared<obcx::core::DbManager>();
  db_manager->configure({sqlite_config(db_path)});

  auto services = std::make_shared<obcx::core::ActorServices>();
  services->register_service<obcx::core::DbManager>(db_manager);

  auto stored = message_stored("qq-9", "tg-9");
  stored.payload.erase("target_message_id");

  const auto result = run_actor(services, std::move(stored));

  ASSERT_FALSE(result.ok());
  ASSERT_TRUE(result.failure.has_value());
  EXPECT_EQ(result.failure->code, "missing_forward_mapping");
  ASSERT_EQ(result.emitted.size(), 1);
  EXPECT_EQ(result.emitted.front().type,
            "bridge::events::MessageForwardFailed");
  EXPECT_EQ(result.emitted.front().payload["code"], "missing_forward_mapping");

  std::filesystem::remove(db_path);
}

TEST(BridgeActorTest, UnmappedMessageIsSuccessfulNoOp) {
  const auto db_path = temp_db_path("runtime_forwarder_noop");
  auto db_manager = std::make_shared<obcx::core::DbManager>();
  db_manager->configure({sqlite_config(db_path)});

  auto services = std::make_shared<obcx::core::ActorServices>();
  services->register_service<obcx::core::DbManager>(db_manager);
  auto forwarder =
      std::make_shared<RecordingForwarder>(bridge::BridgeForwardResult{});
  services->register_service<bridge::IBridgeForwarder>(forwarder);

  auto stored = message_stored("qq-unmapped-1", "unused-target");
  stored.payload.erase("target_platform");
  stored.payload.erase("target_message_id");
  const auto result = run_actor(services, std::move(stored));

  EXPECT_TRUE(result.ok());
  EXPECT_TRUE(result.emitted.empty());
  ASSERT_EQ(forwarder->seen_messages.size(), 1U);

  std::filesystem::remove(db_path);
}

TEST(BridgeActorTest, DeliveryFailureKeepsTypedDiagnosticWithoutMapping) {
  const auto db_path = temp_db_path("runtime_forwarder_delivery_failure");
  auto db_manager = std::make_shared<obcx::core::DbManager>();
  db_manager->configure({sqlite_config(db_path)});

  auto services = std::make_shared<obcx::core::ActorServices>();
  services->register_service<obcx::core::DbManager>(db_manager);
  auto forwarder =
      std::make_shared<RecordingForwarder>(bridge::BridgeForwardResult{
          .disposition = bridge::DirectForwardDisposition::DeliveryFailed,
          .source_platform = "qq",
          .source_bot = "qq-main",
          .source_message_id = "qq-failed-1",
          .target_platform = "telegram",
          .target_bot = "tg-main",
          .failure_message = "transport_failure",
          .failure_retryable = true,
      });
  services->register_service<bridge::IBridgeForwarder>(forwarder);

  auto stored = message_stored("qq-failed-1", "unused-target");
  stored.payload.erase("target_platform");
  stored.payload.erase("target_message_id");
  const auto result = run_actor(services, std::move(stored));

  ASSERT_FALSE(result.ok());
  ASSERT_TRUE(result.failure.has_value());
  EXPECT_EQ(result.failure->code, "bridge_delivery_failed");
  EXPECT_EQ(result.failure->message, "transport_failure");
  EXPECT_TRUE(result.failure->retryable);
  ASSERT_EQ(result.emitted.size(), 1U);
  EXPECT_EQ(result.emitted.front().type,
            "bridge::events::MessageForwardFailed");

  std::filesystem::remove(db_path);
}

TEST(BridgeActorTest, RejectsIncompleteDirectForwardOutcomeWithoutWriting) {
  const auto db_path = temp_db_path("incomplete_direct_outcome");
  auto db_manager = std::make_shared<obcx::core::DbManager>();
  db_manager->configure({sqlite_config(db_path)});
  auto repository = std::make_shared<bridge::BridgeStateRepository>(
      *db_manager, "main", "bridge");
  repository->initialize_schema();
  repository->reset_message_mapping_operation_counts();

  auto services = std::make_shared<obcx::core::ActorServices>();
  services->register_service<obcx::core::DbManager>(db_manager);
  services->register_service<bridge::BridgeStateRepository>(repository);
  auto forwarder =
      std::make_shared<RecordingForwarder>(bridge::BridgeForwardResult{
          .disposition = bridge::DirectForwardDisposition::NewDelivery,
          .source_platform = "qq",
          .source_bot = "qq-main",
          .source_message_id = "qq-incomplete-1",
          .target_platform = "telegram",
          .target_bot = "tg-main",
      });
  services->register_service<bridge::IBridgeForwarder>(forwarder);

  auto stored = message_stored("qq-incomplete-1", "unused-target");
  stored.payload.erase("target_platform");
  stored.payload.erase("target_message_id");
  const auto result = run_actor(services, std::move(stored));

  ASSERT_FALSE(result.ok());
  ASSERT_TRUE(result.failure.has_value());
  EXPECT_EQ(result.failure->code, "missing_forward_mapping");
  ASSERT_EQ(result.emitted.size(), 1U);
  EXPECT_EQ(result.emitted.front().type,
            "bridge::events::MessageForwardFailed");
  EXPECT_EQ(
      repository->message_mapping_operation_counts().direct_forward_writes, 0U);

  std::filesystem::remove(db_path);
}

TEST(BridgeActorTest, RejectsDeliveredOutcomeWithoutConversations) {
  const auto db_path = temp_db_path("missing_conversations");
  auto db_manager = std::make_shared<obcx::core::DbManager>();
  db_manager->configure({sqlite_config(db_path)});
  auto repository = std::make_shared<bridge::BridgeStateRepository>(
      *db_manager, "main", "bridge");
  repository->initialize_schema();
  repository->reset_message_mapping_operation_counts();
  auto services = std::make_shared<obcx::core::ActorServices>();
  services->register_service<obcx::core::DbManager>(db_manager);
  services->register_service<bridge::BridgeStateRepository>(repository);
  services->register_service<bridge::IBridgeForwarder>(
      std::make_shared<RecordingForwarder>(
          bridge::BridgeForwardResult{
              .disposition = bridge::DirectForwardDisposition::NewDelivery,
              .source_platform = "qq",
              .source_bot = "qq-main",
              .source_message_id = "source",
              .target_platform = "telegram",
              .target_bot = "tg-main",
              .target_message_id = "target"},
          false));

  const auto result = run_actor(services, message_stored("source", "unused"));

  ASSERT_FALSE(result.ok());
  ASSERT_TRUE(result.failure.has_value());
  EXPECT_EQ(result.failure->code, "missing_forward_mapping");
  EXPECT_EQ(
      repository->message_mapping_operation_counts().direct_forward_writes, 0U);
  std::filesystem::remove(db_path);
}

TEST(BridgeActorTest,
     MappingPersistenceFailureEmitsFailureWithoutResendingOrForwardedEvent) {
  const auto db_path = temp_db_path("direct_mapping_failure");
  auto db_manager = std::make_shared<obcx::core::DbManager>();
  db_manager->configure({sqlite_config(db_path)});
  auto repository = std::make_shared<bridge::BridgeStateRepository>(
      *db_manager, "main", "bridge");
  repository->initialize_schema();
  db_manager->run_write<void>("main",
                              [](obcx::core::IDbConnection &connection) {
                                connection.execute(R"(
          CREATE TRIGGER fail_direct_mapping_insert
          BEFORE INSERT ON bridge_message_mappings
          BEGIN
            SELECT RAISE(FAIL, 'injected direct mapping persistence failure');
          END;
        )");
                              });
  repository->reset_message_mapping_operation_counts();

  auto services = std::make_shared<obcx::core::ActorServices>();
  services->register_service<obcx::core::DbManager>(db_manager);
  services->register_service<bridge::BridgeStateRepository>(repository);
  auto forwarder =
      std::make_shared<RecordingForwarder>(bridge::BridgeForwardResult{
          .disposition = bridge::DirectForwardDisposition::NewDelivery,
          .source_platform = "qq",
          .source_bot = "qq-main",
          .source_message_id = "qq-persist-failure-1",
          .target_platform = "telegram",
          .target_bot = "tg-main",
          .target_message_id = "tg-uncommitted-1",
      });
  services->register_service<bridge::IBridgeForwarder>(forwarder);

  auto stored = message_stored("qq-persist-failure-1", "unused-target");
  stored.payload.erase("target_platform");
  stored.payload.erase("target_message_id");
  const auto result = run_actor(services, std::move(stored));

  ASSERT_FALSE(result.ok());
  ASSERT_TRUE(result.failure.has_value());
  EXPECT_EQ(result.failure->code, "mapping_persistence_failed");
  EXPECT_FALSE(result.failure->retryable);
  ASSERT_EQ(forwarder->seen_messages.size(), 1U);
  ASSERT_EQ(result.emitted.size(), 1U);
  EXPECT_EQ(result.emitted.front().type,
            "bridge::events::MessageForwardFailed");
  EXPECT_EQ(
      repository->message_mapping_operation_counts().direct_forward_writes, 1U);
  EXPECT_FALSE(
      mapped_target(*repository, "qq", "qq-persist-failure-1", "telegram")
          .has_value());

  std::filesystem::remove(db_path);
}

TEST(BridgeActorTest, PreservesMediaPayloadAcrossActorAsioSuspension) {
  const auto db_path = temp_db_path("media_suspension");
  auto db_manager = std::make_shared<obcx::core::DbManager>();
  db_manager->configure({sqlite_config(db_path)});

  auto services = std::make_shared<obcx::core::ActorServices>();
  services->register_service<obcx::core::DbManager>(db_manager);
  auto forwarder = std::make_shared<SuspendingForwarder>();
  services->register_service<bridge::IBridgeForwarder>(forwarder);

  auto stored = message_stored("qq-media-1", "unused-target");
  stored.payload.erase("target_platform");
  stored.payload.erase("target_message_id");
  stored.raw = {
      {"message",
       {{{"type", "image"},
         {"data",
          {{"file", "photo.jpg"},
           {"url", "https://example.test/photo.jpg"}}}}}},
  };

  const auto result = run_actor(services, stored);

  ASSERT_TRUE(result.ok());
  ASSERT_TRUE(forwarder->seen_message.has_value());
  EXPECT_EQ(forwarder->seen_message->raw, stored.raw);
  ASSERT_EQ(result.emitted.size(), 1);
  EXPECT_EQ(result.emitted.front().payload["target_message_id"], "tg-media-1");

  std::filesystem::remove(db_path);
}

TEST(BridgeActorTest, ConvertsForwardingExceptionIntoRetryableFailure) {
  const auto db_path = temp_db_path("forward_failure");
  auto db_manager = std::make_shared<obcx::core::DbManager>();
  db_manager->configure({sqlite_config(db_path)});

  auto services = std::make_shared<obcx::core::ActorServices>();
  services->register_service<obcx::core::DbManager>(db_manager);
  services->register_service<bridge::IBridgeForwarder>(
      std::make_shared<ThrowingForwarder>());

  const auto result =
      run_actor(services, message_stored("qq-failure-1", "unused-target"));

  ASSERT_FALSE(result.ok());
  ASSERT_TRUE(result.failure.has_value());
  EXPECT_EQ(result.failure->code, "bridge_error");
  EXPECT_TRUE(result.failure->retryable);
  ASSERT_EQ(result.emitted.size(), 1);
  EXPECT_EQ(result.emitted.front().type,
            "bridge::events::MessageForwardFailed");
  EXPECT_EQ(result.emitted.front().payload["retryable"], true);

  std::filesystem::remove(db_path);
}

TEST(BridgeActorTest, ShutdownCancelsSuspendedForwardingWithoutLateResume) {
  using namespace std::chrono_literals;

  const auto db_path = temp_db_path("shutdown");
  auto db_manager = std::make_shared<obcx::core::DbManager>();
  db_manager->configure({sqlite_config(db_path)});

  asio::io_context ioc;
  auto work = asio::make_work_guard(ioc);
  std::jthread io_thread([&ioc] { ioc.run(); });

  auto services = std::make_shared<obcx::core::ActorServices>();
  auto blocking_executor = std::make_shared<obcx::core::BlockingExecutor>(1);
  services->register_service<obcx::core::DbManager>(db_manager);
  services->register_service<obcx::core::BlockingExecutor>(blocking_executor);
  services->register_service<asio::any_io_executor>(
      std::make_shared<asio::any_io_executor>(ioc.get_executor()));
  auto forwarder = std::make_shared<HangingForwarder>();
  auto started = forwarder->started();
  services->register_service<bridge::IBridgeForwarder>(forwarder);

  obcx::core::NativeActorScheduler scheduler(
      obcx::core::NativeActorSchedulerOptions{.worker_count = 1}, services);
  scheduler.register_actor(std::make_shared<bridge::BridgeActor>());

  std::promise<obcx::core::ActorResult> completion;
  auto completed = completion.get_future();
  ASSERT_TRUE(scheduler.enqueue(
      obcx::core::ActorInvocation{
          .actor_id = "bridge",
          .partition_key = "shutdown",
          .db_instance = "main",
          .db_namespace = "bridge",
          .message = message_stored("qq-shutdown-1", "unused-target")},
      [&completion](obcx::core::ActorResult result) {
        completion.set_value(std::move(result));
      }));
  ASSERT_EQ(started.wait_for(2s), std::future_status::ready);

  scheduler.shutdown(obcx::core::ActorExecutorShutdownMode::Cancel);
  ASSERT_EQ(completed.wait_for(2s), std::future_status::ready);
  const auto result = completed.get();
  ASSERT_TRUE(result.failure.has_value());
  EXPECT_EQ(result.failure->code, "scheduler_cancelled");

  std::this_thread::sleep_for(20ms);
  EXPECT_FALSE(forwarder->resumed_after_wait.load(std::memory_order_acquire));
  blocking_executor->shutdown();
  work.reset();
  ioc.stop();
  std::filesystem::remove(db_path);
}
