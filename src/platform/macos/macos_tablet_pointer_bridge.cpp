#include "platform/macos/macos_tablet_pointer_bridge.h"

#include <algorithm>
#include <cmath>

namespace canvas::macos {

std::optional<input::PointerSample>
MacosTabletPointerBridge::convertSample(const MacosTabletSample& sample) {
  if (sample.device.tool != MacTabletTool::Pen ||
      sample.intent != MacTabletIntent::Ink || sample.pointerId == 0 ||
      sample.device.identity.deviceId == 0 ||
      !std::isfinite(sample.screenPosition.x) ||
      !std::isfinite(sample.screenPosition.y)) {
    return std::nullopt;
  }

  float pressure = 0.5F;
  if (sample.pressureSupported()) {
    if (!std::isfinite(sample.pressure) || sample.pressure < 0.0F ||
        sample.pressure > 1.0F) {
      return std::nullopt;
    }
    pressure = sample.pressure;
  }

  input::PointerSample pointer;
  pointer.pointerId = sample.pointerId;
  pointer.timestampMicros = sample.timestampMicros;
  pointer.screenPosition = sample.screenPosition;
  pointer.pressure = pressure;
  // AppKit's scaled [-1, 1] tilt is not an angle. PointerSample has no
  // validity bit, so zero means unknown at this bridge; the scaled metadata
  // remains unchanged on MacosTabletSample for a future richer model.
  pointer.tiltXDegrees = 0.0F;
  pointer.tiltYDegrees = 0.0F;
  pointer.kind = input::PointerKind::Pen;
  pointer.phase = sample.phase;
  pointer.predicted = false;
  return pointer;
}

MacosTabletPointerOutput MacosTabletPointerBridge::convertOutput(
    const MacosTabletSessionOutput& output) {
  MacosTabletPointerOutput converted;
  const std::size_t inputCount =
      std::min(output.size(), output.samples.size());
  for (std::size_t index = 0; index < inputCount; ++index) {
    const auto pointer = convertSample(output[index]);
    if (!pointer || converted.count >= converted.samples.size()) continue;
    converted.samples[converted.count++] = *pointer;
  }
  return converted;
}

}  // namespace canvas::macos
