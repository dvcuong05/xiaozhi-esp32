#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace kynangso {

constexpr std::size_t kMaxLunarCalendarEvents = 3;
constexpr std::size_t kLunarCalendarEventTitleBytes = 73;

struct LunarCalendarEvent {
    std::array<char, kLunarCalendarEventTitleBytes> title{};
    std::uint8_t icon = 3;
};

using LunarCalendarEvents = std::array<LunarCalendarEvent, kMaxLunarCalendarEvents>;

}  // namespace kynangso
