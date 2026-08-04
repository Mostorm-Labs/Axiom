#pragma once

#include "canvas/input/pointer_sample.h"
#include "platform/macos/macos_tablet_input.h"

#include <array>
#include <cstddef>
#include <optional>

namespace canvas::macos {

struct MacosTabletPointerOutput {
  static constexpr std::size_t capacity =
      MacosTabletSessionOutput::capacity;
  std::array<input::PointerSample, capacity> samples;
  std::size_t count = 0;

  bool empty() const noexcept { return count == 0; }
  std::size_t size() const noexcept { return count; }
  const input::PointerSample& operator[](std::size_t index) const noexcept {
    return samples[index];
  }
};

class MacosTabletPointerBridge {
 public:
  static std::optional<input::PointerSample> convertSample(
      const MacosTabletSample& sample);
  static MacosTabletPointerOutput convertOutput(
      const MacosTabletSessionOutput& output);
};

}  // namespace canvas::macos
