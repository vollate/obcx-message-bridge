#include "bridge_status.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cstdint>

namespace bridge {
namespace {

constexpr auto qq_stale_after = std::chrono::seconds{60};
constexpr auto telegram_stale_after = std::chrono::seconds{300};

auto append_platform_status(
    std::string &output, const std::string_view heading,
    const std::string_view installation,
    const std::optional<storage::PlatformHeartbeatInfo> &activity,
    const std::chrono::seconds stale_after,
    const std::chrono::system_clock::time_point now) -> void {
  output += fmt::format("{}\n安装: {}\n", heading, installation);
  if (!activity.has_value()) {
    output += "状态: ❔ 无活动记录\n最后活动: 未知\n";
    return;
  }

  const auto raw_age = std::chrono::duration_cast<std::chrono::seconds>(
                           now - activity->last_heartbeat_at)
                           .count();
  const auto age = std::max<std::int64_t>(0, raw_age);
  const auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                             activity->last_heartbeat_at.time_since_epoch())
                             .count();
  output += fmt::format("状态: {}\n最后活动: {} ({} 秒前)\n",
                        age > stale_after.count() ? "⚠️ 可能离线" : "✅ 正常",
                        timestamp, age);
}

} // namespace

auto render_bridge_status(
    const std::string_view onebot11_installation,
    const std::optional<storage::PlatformHeartbeatInfo> &qq_activity,
    const std::string_view telegram_installation,
    const std::optional<storage::PlatformHeartbeatInfo> &telegram_activity,
    const std::chrono::system_clock::time_point now) -> std::string {
  std::string output = "🌉 Bridge 状态\n\n";
  append_platform_status(output, "🤖 QQ 平台", onebot11_installation,
                         qq_activity, qq_stale_after, now);
  output += "\n";
  append_platform_status(output, "💬 Telegram 平台", telegram_installation,
                         telegram_activity, telegram_stale_after, now);
  if (!output.empty() && output.back() == '\n') {
    output.pop_back();
  }
  return output;
}

} // namespace bridge
