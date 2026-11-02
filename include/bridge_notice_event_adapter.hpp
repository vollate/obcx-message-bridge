#pragma once

#include <common/message_type.hpp>
#include <core/actor/actor.hpp>

#include <optional>

namespace bridge {

// Proprietary poke presentation only. Ordinary messages and deletion authority
// come from the admitted typed observations, never this compatibility parser.
auto notice_event_from_raw_notice(const obcx::core::MessageEnvelope &message)
    -> std::optional<obcx::common::NoticeEvent>;

} // namespace bridge
