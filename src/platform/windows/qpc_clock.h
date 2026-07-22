#pragma once

#include <cstdint>

namespace canvas::windows {

std::uint64_t qpcTicksToMicros(std::uint64_t ticks,
                               std::uint64_t frequency) noexcept;

}  // namespace canvas::windows
