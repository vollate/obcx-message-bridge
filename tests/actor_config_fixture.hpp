#pragma once

#include "common/config_snapshot.hpp"
#include <stdexcept>

namespace obcx::test {

// Test-only adapter for existing synthetic TOML fixtures. It deliberately does
// NOT validate connection options. Provider schema tests use the process
// loader. Strip the fixture's private table before calling the explicit Actor
// SDK builder.
inline auto actor_fixture_snapshot(const std::string &path)
    -> common::RuntimeConfigBuildResult {
  auto document = toml::parse_file(path);
  std::vector<common::BotInstallationMetadata> metadata;
  if (const auto *bots = document.get_as<toml::table>("bots")) {
    for (const auto &[key, node] : *bots) {
      const auto &table = *node.as_table();
      const bot::SurfaceId surface{
          table["surface"].value<std::string>().value()};
      std::string platform;
      if (surface == bot::SurfaceId{"onebot11.qq"}) {
        platform = "qq";
      } else if (surface == bot::SurfaceId{"telegram.bot_api"}) {
        platform = "telegram";
      } else {
        throw std::invalid_argument("fixture requires explicit known metadata");
      }
      std::string target;
      if (const auto *connection = table.get_as<toml::table>("connection")) {
        if (const auto username =
                (*connection)["bot_username"].value<std::string>()) {
          target = *username;
        }
      }
      metadata.push_back({std::string{key.str()},
                          table["enabled"].value<bool>().value(), surface,
                          table["transport"].value<std::string>().value(),
                          platform, target});
    }
  }
  document.erase("bots");
  return common::ActorConfigSnapshotBuilder::build(std::move(document),
                                                   std::move(metadata), path);
}

} // namespace obcx::test
