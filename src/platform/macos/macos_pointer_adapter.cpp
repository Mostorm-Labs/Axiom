#include "platform/macos/macos_pointer_adapter.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace canvas::macos {

namespace {

std::uint64_t timestampMicros(double timestampSeconds) noexcept {
  if (std::isnan(timestampSeconds) || timestampSeconds <= 0.0) {
    return 0;
  }
  if (!std::isfinite(timestampSeconds)) {
    return std::numeric_limits<std::uint64_t>::max();
  }

  const long double micros =
      static_cast<long double>(timestampSeconds) * 1'000'000.0L;
  const long double maximum =
      static_cast<long double>(std::numeric_limits<std::uint64_t>::max());
  if (micros >= maximum) {
    return std::numeric_limits<std::uint64_t>::max();
  }
  return static_cast<std::uint64_t>(micros);
}

float normalizedPressure(const std::optional<double>& rawPressure) noexcept {
  if (!rawPressure || !std::isfinite(*rawPressure)) {
    return 0.5F;
  }
  return static_cast<float>(std::clamp(*rawPressure, 0.0, 1.0));
}

input::PointerPhase normalizedPhase(MacMousePhase phase) noexcept {
  switch (phase) {
    case MacMousePhase::Down:
      return input::PointerPhase::Down;
    case MacMousePhase::Move:
      return input::PointerPhase::Move;
    case MacMousePhase::Up:
      return input::PointerPhase::Up;
    case MacMousePhase::Cancel:
      return input::PointerPhase::Cancel;
  }
  return input::PointerPhase::Cancel;
}

}  // namespace

input::PointerSample MacosPointerAdapter::normalize(
    const RawMacMouseEvent& raw, std::uint64_t pointerId) {
  const float localX = raw.localPosition.x - raw.boundsOrigin.x;
  const float localY = raw.localPosition.y - raw.boundsOrigin.y;

  input::PointerSample sample;
  sample.pointerId = pointerId;
  sample.timestampMicros = timestampMicros(raw.timestampSeconds);
  sample.screenPosition = {
      localX, raw.viewFlipped ? localY : raw.boundsSize.y - localY};
  sample.pressure = normalizedPressure(raw.pressure);
  sample.tiltXDegrees = 0.0F;
  sample.tiltYDegrees = 0.0F;
  sample.kind = input::PointerKind::Mouse;
  sample.phase = normalizedPhase(raw.phase);
  sample.predicted = false;
  return sample;
}

}  // namespace canvas::macos
