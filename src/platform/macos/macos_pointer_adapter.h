#pragma once

#include "canvas/input/pointer_sample.h"

#include <cstdint>
#include <optional>

namespace canvas::macos {

enum class MacMousePhase { Down, Move, Up, Cancel };

struct RawMacMouseEvent {
  core::Vec2 localPosition;
  core::Vec2 boundsOrigin;
  core::Vec2 boundsSize;
  bool viewFlipped = true;
  double backingScale = 1.0;
  double timestampSeconds = 0.0;
  std::optional<double> pressure;
  std::int64_t buttonNumber = 0;
  std::int64_t eventNumber = 0;
  std::int64_t deviceId = 0;
  MacMousePhase phase = MacMousePhase::Move;
};

class MacosPointerAdapter {
 public:
  static input::PointerSample normalize(const RawMacMouseEvent& raw,
                                        std::uint64_t pointerId);
};

}  // namespace canvas::macos
