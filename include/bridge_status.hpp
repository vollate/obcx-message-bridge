#pragma once

#include "bridge_storage_models.hpp"

#include <chrono>
#include <optional>
#include <string>
#include <string_view>

namespace bridge {

[[nodiscard]] auto render_bridge_status(
    std::string_view onebot11_installation,
    const std::optional<storage::PlatformHeartbeatInfo> &qq_activity,
    std::string_view telegram_installation,
    const std::optional<storage::PlatformHeartbeatInfo> &telegram_activity,
    std::chrono::system_clock::time_point now) -> std::string;

} // namespace bridge
