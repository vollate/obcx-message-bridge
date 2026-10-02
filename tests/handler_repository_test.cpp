#include "actor_config_fixture.hpp"
#include "bridge_state_repository.hpp"
#include "bridge_storage_models.hpp"
#include "common/config_snapshot.hpp"
#include "config.hpp"
#include "core/actor/blocking_executor.hpp"
#include "core/bot/typed_operation.hpp"
#include "core/infrastructure/db_manager.hpp"
#include "qq/message_formatter.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_future.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iterator>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

class NoopBotOperationGateway final : public obcx::bot::BotOperationGateway {
public:
  auto invoke(obcx::bot::OperationEnvelope)
      -> boost::asio::awaitable<obcx::bot::OperationReply> override {
    co_return obcx::bot::failed_operation<obcx::bot::Json>(
        obcx::bot::BotOperationErrorCode::UnsupportedAction, "noop gateway");
  }

  auto supported_actions(
      const obcx::bot::BotInstallationRef &installation) const
      -> obcx::bot::BotOperationResult<obcx::bot::SupportedActions> override {
    return obcx::bot::BotOperationResult<obcx::bot::SupportedActions>::success(
        {.installation = installation});
  }
};

auto noop_bridge_operations() -> std::shared_ptr<bridge::BridgeBotOperations> {
  return std::make_shared<bridge::BridgeBotOperations>(
      std::make_shared<NoopBotOperationGateway>(), "tg-main", "qq-main");
}

auto temp_db_path(const std::string &name) -> std::filesystem::path {
  const auto stamp =
      std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() /
         ("obcx_handler_repo_" + name + "_" + std::to_string(stamp) +
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

auto bridge_config_view(const std::filesystem::path &path,
                        const bool inject_default_bots = true)
    -> obcx::common::ActorConfigView {
  auto resolved = path;
  if (inject_default_bots) {
    std::ifstream input(path);
    const std::string content{std::istreambuf_iterator<char>{input},
                              std::istreambuf_iterator<char>{}};
    if (!content.contains("[bots.")) {
      resolved += ".with-bots.toml";
      std::ofstream output(resolved);
      output << R"(
[bots.qq-main]
enabled = true
surface = "onebot11.qq"
transport = "http"
[bots.qq-main.connection]
host = "localhost"
port = 3000
access_token = ""
use_tls = false
connect_timeout_ms = 5000
action_timeout_ms = 30000
poll_interval_ms = 1000

[bots.tg-main]
enabled = true
surface = "telegram.bot_api"
transport = "http"
[bots.tg-main.connection]
host = "api.telegram.org"
port = 443
access_token = "YOUR_TELEGRAM_TOKEN"
bot_username = "fixture_bot"
use_tls = true
connect_timeout_ms = 5000
action_timeout_ms = 30000
poll_timeout_ms = 25000
poll_force_close_ms = 30000
poll_retry_interval_ms = 3000

)" << content;
    }
  }
  auto built = obcx::test::actor_fixture_snapshot(resolved.string());
  if (resolved != path) {
    std::filesystem::remove(resolved);
  }
  if (!built) {
    throw std::runtime_error("failed to build test config snapshot");
  }
  return {std::move(built.snapshot), "bridge"};
}

} // namespace

TEST(BridgeHandlerRepositoryTest, RequiresExplicitAbsoluteMediaRoots) {
  const auto path =
      temp_db_path("explicit_media_roots").replace_extension(".toml");
  for (const auto &peer :
       {std::string{},
        std::string{"bridge_files_container_dir = \"relative\"\n"}}) {
    {
      std::ofstream config(path);
      config << "[actors.bridge.config]\n"
                "telegram_installation = \"tg-main\"\n"
                "onebot11_installation = \"qq-main\"\n"
                "bridge_files_dir = \"/tmp/bridge_files\"\n"
             << peer;
    }
    EXPECT_THROW((void)bridge::load_bridge_config(bridge_config_view(path)),
                 std::runtime_error);
  }
  std::filesystem::remove(path);
}

TEST(BridgeHandlerRepositoryTest, LoadsNamedPairsAndIsolatesCollidingRoutes) {
  const auto config_path =
      temp_db_path("named_installations").replace_extension(".toml");
  {
    std::ofstream config(config_path);
    config << R"(
[bots.qq-a]
enabled = true
surface = "onebot11.qq"
transport = "http"
[bots.qq-a.connection]
host = "localhost"
port = 3000
access_token = ""
use_tls = false
connect_timeout_ms = 5000
action_timeout_ms = 30000
poll_interval_ms = 1000

[bots.tg-a]
enabled = true
surface = "telegram.bot_api"
transport = "http"
[bots.tg-a.connection]
host = "api.telegram.org"
port = 443
access_token = "YOUR_TELEGRAM_TOKEN"
bot_username = "fixture_a_bot"
use_tls = true
connect_timeout_ms = 5000
action_timeout_ms = 30000
poll_timeout_ms = 25000
poll_force_close_ms = 30000
poll_retry_interval_ms = 3000

[bots.qq-b]
enabled = true
surface = "onebot11.qq"
transport = "http"
[bots.qq-b.connection]
host = "localhost"
port = 3000
access_token = ""
use_tls = false
connect_timeout_ms = 5000
action_timeout_ms = 30000
poll_interval_ms = 1000

[bots.tg-b]
enabled = true
surface = "telegram.bot_api"
transport = "http"
[bots.tg-b.connection]
host = "api.telegram.org"
port = 443
access_token = "YOUR_TELEGRAM_TOKEN"
bot_username = "fixture_b_bot"
use_tls = true
connect_timeout_ms = 5000
action_timeout_ms = 30000
poll_timeout_ms = 25000
poll_force_close_ms = 30000
poll_retry_interval_ms = 3000

[actors.bridge.config]
bridge_files_dir = "/tmp/bridge_files"
bridge_files_container_dir = "/root/llonebot/bridge_files"
legacy_state_pair = "primary"

[[actors.bridge.config.installation_pairs]]
id = "primary"
telegram_installation = "tg-a"
onebot11_installation = "qq-a"

[[actors.bridge.config.installation_pairs]]
id = "secondary"
telegram_installation = "tg-b"
onebot11_installation = "qq-b"

[[actors.bridge.config.legacy_mapping_routes]]
pair = "primary"
telegram_group_id = "old-tg"
qq_group_id = "old-qq"

[[group_mappings.group_to_group]]
pair = "primary"
telegram_group_id = "same-tg-group"
qq_group_id = "same-qq-group"

[[group_mappings.group_to_group]]
pair = "secondary"
telegram_group_id = "same-tg-group"
qq_group_id = "same-qq-group"
)";
  }

  const auto config = bridge::load_bridge_config(
      bridge_config_view(config_path, /*inject_default_bots=*/false));
  ASSERT_EQ(config->installation_pairs.size(), 2U);
  ASSERT_NE(config->pair("primary"), nullptr);
  ASSERT_NE(config->pair("secondary"), nullptr);
  EXPECT_EQ(config->pair_for_source("telegram", "tg-a")->id, "primary");
  EXPECT_EQ(config->pair_for_source("qq", "qq-b")->id, "secondary");
  EXPECT_EQ(config->tg_group_and_topic_id("primary", "same-qq-group").first,
            "same-tg-group");
  EXPECT_EQ(config->tg_group_and_topic_id("secondary", "same-qq-group").first,
            "same-tg-group");
  EXPECT_EQ(config->legacy_migration_pair()->id, "primary");
  ASSERT_EQ(config->legacy_mapping_routes.size(), 1U);
  EXPECT_EQ(config->legacy_mapping_routes.front().telegram_installation,
            "tg-a");
  EXPECT_EQ(config->legacy_mapping_routes.front().onebot11_installation,
            "qq-a");
  EXPECT_TRUE(config->telegram_installation.empty());
  std::filesystem::remove(config_path);
}

TEST(BridgeHandlerRepositoryTest, RejectsInvalidMigrationConfiguration) {
  const std::vector<std::string> invalid = {
      R"(
[actors.bridge.config]
telegram_installation = "tg-main"
onebot11_installation = "qq-main"
bridge_files_dir = "/tmp/bridge_files"
bridge_files_container_dir = "/root/llonebot/bridge_files"
legacy_unresolved_mapping_policy = "guess"
)",
      R"(
[actors.bridge.config]
telegram_installation = "tg-main"
onebot11_installation = "qq-main"
bridge_files_dir = "/tmp/bridge_files"
bridge_files_container_dir = "/root/llonebot/bridge_files"
[[actors.bridge.config.legacy_mapping_routes]]
telegram_conversation_id = "group:wrong"
qq_conversation_id = "group:1"
)",
      R"(
[actors.bridge.config]
telegram_installation = "tg-main"
onebot11_installation = "qq-main"
bridge_files_dir = "/tmp/bridge_files"
bridge_files_container_dir = "/root/llonebot/bridge_files"
[[actors.bridge.config.legacy_mapping_routes]]
telegram_group_id = "1"
telegram_conversation_id = "chat:1"
qq_group_id = "2"
)",
      R"(
[actors.bridge.config]
telegram_installation = "tg-main"
onebot11_installation = "qq-main"
bridge_files_dir = "/tmp/bridge_files"
bridge_files_container_dir = "/root/llonebot/bridge_files"
[[actors.bridge.config.legacy_mapping_routes]]
telegram_group_id = "1"
qq_group_id = "2"
telegram_topic_id = 0
)",
      R"(
[actors.bridge.config]
telegram_installation = "tg-main"
onebot11_installation = "qq-main"
bridge_files_dir = "/tmp/bridge_files"
bridge_files_container_dir = "/root/llonebot/bridge_files"
[[actors.bridge.config.legacy_mapping_routes]]
telegram_group_id = "1"
qq_group_id = "2"
[[actors.bridge.config.legacy_mapping_routes]]
telegram_group_id = "1"
qq_group_id = "3"
)",
      R"(
[actors.bridge.config]
telegram_installation = "tg-main"
onebot11_installation = "qq-main"
bridge_files_dir = "/tmp/bridge_files"
bridge_files_container_dir = "/root/llonebot/bridge_files"
[[actors.bridge.config.legacy_mapping_routes]]
telegram_group_id = "1"
qq_group_id = "2"
[[group_mappings.group_to_group]]
telegram_group_id = "1"
qq_group_id = "2"
)",
  };
  for (std::size_t index = 0; index < invalid.size(); ++index) {
    const auto path = temp_db_path("invalid_migration_" + std::to_string(index))
                          .replace_extension(".toml");
    {
      std::ofstream config(path);
      config << invalid[index];
    }
    EXPECT_THROW((void)bridge::load_bridge_config(bridge_config_view(path)),
                 std::runtime_error)
        << index;
    std::filesystem::remove(path);
  }
}

TEST(BridgeHandlerRepositoryTest, RejectsAmbiguousNamedPairConfiguration) {
  const std::vector<std::string> invalid = {
      R"(
[actors.bridge.config]
bridge_files_dir = "/tmp/bridge_files"
bridge_files_container_dir = "/root/llonebot/bridge_files"
telegram_installation = "tg-a"
onebot11_installation = "qq-a"
[[actors.bridge.config.installation_pairs]]
id = "primary"
telegram_installation = "tg-a"
onebot11_installation = "qq-a"
)",
      R"(
[actors.bridge.config]
bridge_files_dir = "/tmp/bridge_files"
bridge_files_container_dir = "/root/llonebot/bridge_files"
[[actors.bridge.config.installation_pairs]]
id = "duplicate"
telegram_installation = "tg-a"
onebot11_installation = "qq-a"
[[actors.bridge.config.installation_pairs]]
id = "duplicate"
telegram_installation = "tg-b"
onebot11_installation = "qq-b"
)",
      R"(
[actors.bridge.config]
bridge_files_dir = "/tmp/bridge_files"
bridge_files_container_dir = "/root/llonebot/bridge_files"
[[actors.bridge.config.installation_pairs]]
id = "primary"
telegram_installation = "tg-a"
onebot11_installation = "qq-a"
[[actors.bridge.config.installation_pairs]]
id = "secondary"
telegram_installation = "tg-a"
onebot11_installation = "qq-b"
)",
      R"(
[actors.bridge.config]
bridge_files_dir = "/tmp/bridge_files"
bridge_files_container_dir = "/root/llonebot/bridge_files"
[[actors.bridge.config.installation_pairs]]
id = "primary"
telegram_installation = "tg-a"
onebot11_installation = "qq-a"
[[actors.bridge.config.installation_pairs]]
id = "secondary"
telegram_installation = "tg-b"
onebot11_installation = "qq-b"
[[group_mappings.group_to_group]]
telegram_group_id = "tg-group"
qq_group_id = "qq-group"
)",
      R"(
[actors.bridge.config]
bridge_files_dir = "/tmp/bridge_files"
bridge_files_container_dir = "/root/llonebot/bridge_files"
[[actors.bridge.config.installation_pairs]]
id = "primary"
telegram_installation = "tg-a"
onebot11_installation = "qq-a"
[[group_mappings.group_to_group]]
pair = "missing"
telegram_group_id = "tg-group"
qq_group_id = "qq-group"
)",
  };
  for (std::size_t index = 0; index < invalid.size(); ++index) {
    const auto path = temp_db_path("invalid_named_" + std::to_string(index))
                          .replace_extension(".toml");
    {
      std::ofstream config(path);
      config << invalid[index];
    }
    EXPECT_THROW((void)bridge::load_bridge_config(bridge_config_view(path)),
                 std::runtime_error)
        << index;
    std::filesystem::remove(path);
  }
}

TEST(BridgeHandlerRepositoryTest, RejectsInvalidInstallationPair) {
  const std::vector<std::string> invalid = {
      R"(
[bots.qq-main]
enabled = true
surface = "onebot11.qq"
transport = "http"
[bots.qq-main.connection]

[bots.tg-main]
enabled = true
surface = "telegram.bot_api"
transport = "http"
[bots.tg-main.connection]
access_token = "YOUR_TELEGRAM_TOKEN"

[actors.bridge.config]
onebot11_installation = "qq-main"
bridge_files_dir = "/tmp/bridge_files"
bridge_files_container_dir = "/root/llonebot/bridge_files"
)",
      R"(
[bots.same]
enabled = true
surface = "telegram.bot_api"
transport = "http"
[bots.same.connection]
access_token = "YOUR_TELEGRAM_TOKEN"

[actors.bridge.config]
telegram_installation = "same"
onebot11_installation = "same"
bridge_files_dir = "/tmp/bridge_files"
bridge_files_container_dir = "/root/llonebot/bridge_files"
)",
      R"(
[bots.qq-main]
enabled = true
surface = "onebot11.qq"
transport = "http"
[bots.qq-main.connection]

[bots.tg-main]
enabled = true
surface = "onebot11.qq"
transport = "http"
[bots.tg-main.connection]

[actors.bridge.config]
telegram_installation = "tg-main"
onebot11_installation = "qq-main"
bridge_files_dir = "/tmp/bridge_files"
bridge_files_container_dir = "/root/llonebot/bridge_files"
)",
      R"(
[bots.qq-main]
enabled = true
surface = "onebot11.qq"
transport = "http"
[bots.qq-main.connection]

[bots.tg-main]
enabled = false
surface = "telegram.bot_api"
transport = "http"
[bots.tg-main.connection]
access_token = "YOUR_TELEGRAM_TOKEN"

[actors.bridge.config]
telegram_installation = "tg-main"
onebot11_installation = "qq-main"
bridge_files_dir = "/tmp/bridge_files"
bridge_files_container_dir = "/root/llonebot/bridge_files"
)",
      R"(
[bots.qq-main]
enabled = true
surface = "onebot11.qq"
transport = "http"
[bots.qq-main.connection]

[actors.bridge.config]
telegram_installation = "missing-tg"
onebot11_installation = "qq-main"
bridge_files_dir = "/tmp/bridge_files"
bridge_files_container_dir = "/root/llonebot/bridge_files"
)",
  };

  for (std::size_t index = 0; index < invalid.size(); ++index) {
    const auto config_path =
        temp_db_path("invalid_installations_" + std::to_string(index))
            .replace_extension(".toml");
    {
      std::ofstream config(config_path);
      config << invalid[index];
    }
    EXPECT_THROW((void)bridge::load_bridge_config(bridge_config_view(
                     config_path, /*inject_default_bots=*/false)),
                 std::runtime_error)
        << "invalid case " << index;
    std::filesystem::remove(config_path);
  }
}

TEST(BridgeHandlerRepositoryTest, ExactSourceBotMustMatchConfiguredSide) {
  bridge::BridgeConfig config;
  config.installation_pairs.emplace(
      "primary",
      bridge::BridgeInstallationPair{.id = "primary",
                                     .telegram_installation = "tg-main",
                                     .onebot11_installation = "qq-main"});

  EXPECT_NO_THROW(
      bridge::validate_bridge_source(config, "telegram", "tg-main"));
  EXPECT_NO_THROW(bridge::validate_bridge_source(config, "qq", "qq-main"));
  EXPECT_THROW(bridge::validate_bridge_source(config, "telegram", ""),
               std::runtime_error);
  EXPECT_THROW(bridge::validate_bridge_source(config, "telegram", "tg-other"),
               std::runtime_error);
  EXPECT_THROW(bridge::validate_bridge_source(config, "qq", "qq-other"),
               std::runtime_error);
  EXPECT_THROW(
      bridge::validate_bridge_source(config, "discord", "discord-main"),
      std::runtime_error);
}

TEST(BridgeHandlerRepositoryTest, RejectsInvalidQqMediaDownloadLimit) {
  const std::vector<std::string> invalid_values = {
      "qq_media_download_max_bytes = 0\n",
      "qq_media_download_max_bytes = -1\n",
      "qq_media_download_max_bytes = 10485761\n",
  };

  for (std::size_t index = 0; index < invalid_values.size(); ++index) {
    const auto config_path =
        temp_db_path("invalid_qq_media_limit_" + std::to_string(index))
            .replace_extension(".toml");
    {
      std::ofstream config(config_path);
      config << "[actors.bridge.config]\n"
                "telegram_installation = \"tg-main\"\n"
                "onebot11_installation = \"qq-main\"\n"
                "bridge_files_dir = \"/tmp/bridge_files\"\n"
                "bridge_files_container_dir = \"/root/llonebot/bridge_files\"\n"
             << invalid_values[index];
    }
    EXPECT_THROW(
        (void)bridge::load_bridge_config(bridge_config_view(config_path)),
        std::runtime_error)
        << "invalid case " << index;
    std::filesystem::remove(config_path);
  }
}

TEST(BridgeHandlerRepositoryTest, RejectsInvalidMessageRetryPolicy) {
  const std::vector<std::string> invalid_values = {
      "message_retry_max_attempts = 0\n",
      "message_retry_base_interval_sec = 0\n",
      "retry_queue_check_interval_sec = -1\n",
      "max_retry_interval_sec = 0\n",
      "message_retry_base_interval_sec = 11\nmax_retry_interval_sec = 10\n",
      "retry_queue_check_interval_sec = 11\nmax_retry_interval_sec = 10\n",
  };

  for (std::size_t index = 0; index < invalid_values.size(); ++index) {
    const auto config_path =
        temp_db_path("invalid_retry_policy_" + std::to_string(index))
            .replace_extension(".toml");
    {
      std::ofstream config(config_path);
      config << "[actors.bridge.config]\n"
                "telegram_installation = \"tg-main\"\n"
                "onebot11_installation = \"qq-main\"\n"
                "bridge_files_dir = \"/tmp/bridge_files\"\n"
                "bridge_files_container_dir = \"/root/llonebot/bridge_files\"\n"
             << invalid_values[index];
    }

    EXPECT_THROW(
        (void)bridge::load_bridge_config(bridge_config_view(config_path)),
        std::runtime_error)
        << "invalid case " << index;
    std::filesystem::remove(config_path);
  }
}

TEST(BridgeHandlerRepositoryTest,
     ReverseLookupIgnoresMappingsWithQqToTelegramDisabled) {
  bridge::BridgeConfig config;
  config.group_map.emplace("tg-inbound-only",
                           bridge::GroupBridgeConfig("tg-inbound-only",
                                                     "qq-shared", true, true,
                                                     false, true));
  config.group_map.emplace("tg-bidirectional",
                           bridge::GroupBridgeConfig("tg-bidirectional",
                                                     "qq-shared", true, true,
                                                     true, true));

  const auto [telegram_group_id, topic_id] =
      config.tg_group_and_topic_id("legacy", "qq-shared");

  EXPECT_EQ(telegram_group_id, "tg-bidirectional");
  EXPECT_EQ(topic_id, -1);
}

TEST(BridgeHandlerRepositoryTest,
     QQReplyReverseLookupUsesCurrentConversationWithEqualTargetIds) {
  const auto bridge_db_path = temp_db_path("qq_reverse_collision");
  auto db_manager = std::make_shared<obcx::core::DbManager>();
  db_manager->configure({sqlite_config(bridge_db_path)});
  auto repository = std::make_shared<bridge::BridgeStateRepository>(
      *db_manager, "main", "bridge");
  repository->initialize_schema();
  ASSERT_TRUE(repository->add_message_mapping(
      {.source_installation = "tg-main",
       .source_platform = "telegram",
       .source_conversation_id = "chat:tg-a",
       .source_message_id = "tg-source-a",
       .target_installation = "qq-main",
       .target_platform = "qq",
       .target_conversation_id = "group:qq-a",
       .target_message_id = "same-qq-id",
       .created_at = std::chrono::system_clock::now()}));
  ASSERT_TRUE(repository->add_message_mapping(
      {.source_installation = "tg-main",
       .source_platform = "telegram",
       .source_conversation_id = "chat:tg-b",
       .source_message_id = "tg-source-b",
       .target_installation = "qq-main",
       .target_platform = "qq",
       .target_conversation_id = "group:qq-b",
       .target_message_id = "same-qq-id",
       .created_at = std::chrono::system_clock::now()}));
  auto config = std::make_shared<bridge::BridgeConfig>();
  bridge::BridgeInstallationPair pair{.id = "legacy",
                                      .telegram_installation = "tg-main",
                                      .onebot11_installation = "qq-main"};
  pair.group_map.emplace("tg-a", bridge::GroupBridgeConfig("tg-a", "qq-a"));
  pair.group_map.emplace("tg-b", bridge::GroupBridgeConfig("tg-b", "qq-b"));
  config->installation_pairs.emplace("legacy", std::move(pair));
  auto blocking = std::make_shared<obcx::core::BlockingExecutor>(1);
  bridge::qq::QQMessageFormatter formatter(noop_bridge_operations(), config,
                                           repository, blocking);
  obcx::common::MessageEvent event;
  event.message_type = "group";
  event.group_id = "qq-a";
  event.message.push_back({.type = "reply", .data = {{"id", "same-qq-id"}}});
  obcx::common::Message output;
  boost::asio::io_context ioc;
  auto future =
      boost::asio::co_spawn(ioc, formatter.format_reply_message(event, output),
                            boost::asio::use_future);
  ioc.run();

  EXPECT_TRUE(future.get());
  ASSERT_EQ(output.size(), 1U);
  EXPECT_EQ(output.front().data.value("id", std::string{}), "tg-source-a");
  blocking->shutdown();
  std::filesystem::remove(bridge_db_path);
}
