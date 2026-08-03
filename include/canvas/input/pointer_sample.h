#pragma once

#include <cstdint>

#include "canvas/core/geometry.h"

namespace canvas::input {

enum class PointerKind { Pen, Touch, Mouse };
enum class PointerPhase { Down, Move, Up, Cancel };

struct PointerSample {
  std::uint64_t pointerId = 0;
  std::uint64_t timestampMicros = 0;
  core::Vec2 screenPosition;
  float pressure = 0.5F;
  float tiltXDegrees = 0.0F;
  float tiltYDegrees = 0.0F;
  PointerKind kind = PointerKind::Pen;
  PointerPhase phase = PointerPhase::Move;
  bool predicted = false;
};

}  // namespace canvas::input
