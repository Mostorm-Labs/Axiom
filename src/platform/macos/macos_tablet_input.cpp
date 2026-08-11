#include "platform/macos/macos_tablet_input.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace canvas::macos {

namespace {

std::uint64_t timestampMicros(double timestampSeconds) noexcept {
  if (std::isnan(timestampSeconds) || timestampSeconds <= 0.0) return 0;
  if (!std::isfinite(timestampSeconds)) {
    return std::numeric_limits<std::uint64_t>::max();
  }
  const long double micros =
      static_cast<long double>(timestampSeconds) * 1'000'000.0L;
  const long double maximum =
      static_cast<long double>(std::numeric_limits<std::uint64_t>::max());
  return micros >= maximum ? std::numeric_limits<std::uint64_t>::max()
                           : static_cast<std::uint64_t>(micros);
}

float boundedFloat(double value, double minimum, double maximum) noexcept {
  return static_cast<float>(std::clamp(value, minimum, maximum));
}

float finiteFloat(double value) noexcept {
  const double limit =
      static_cast<double>(std::numeric_limits<float>::max());
  return static_cast<float>(std::clamp(value, -limit, limit));
}

MacTabletIntent intentForTool(MacTabletTool tool) noexcept {
  switch (tool) {
    case MacTabletTool::Pen:
      return MacTabletIntent::Ink;
    case MacTabletTool::Eraser:
      return MacTabletIntent::EraserPending;
    case MacTabletTool::Cursor:
    case MacTabletTool::Unknown:
      return MacTabletIntent::Unsupported;
  }
  return MacTabletIntent::Unsupported;
}

input::PointerPhase pointerPhase(MacTabletPointPhase phase) noexcept {
  switch (phase) {
    case MacTabletPointPhase::Down:
      return input::PointerPhase::Down;
    case MacTabletPointPhase::Move:
    case MacTabletPointPhase::NativeUpdate:
      return input::PointerPhase::Move;
    case MacTabletPointPhase::Up:
      return input::PointerPhase::Up;
    case MacTabletPointPhase::Cancel:
      return input::PointerPhase::Cancel;
  }
  return input::PointerPhase::Cancel;
}

bool samePhysicalDevice(const MacTabletDeviceIdentity& left,
                        const MacTabletDeviceIdentity& right) noexcept {
  if (left.deviceId == 0 || left.deviceId != right.deviceId) return false;
  if (left.uniqueId != 0 && right.uniqueId != 0) {
    return left.uniqueId == right.uniqueId;
  }
  return true;
}

}  // namespace

std::optional<MacosTabletSample> MacosTabletAdapter::normalize(
    const RawMacTabletPointEvent& raw,
    const MacTabletDeviceProfile& device, std::uint64_t pointerId,
    input::PointerPhase phase) {
  if (pointerId == 0 || raw.deviceId == 0 ||
      raw.deviceId != device.identity.deviceId ||
      !std::isfinite(raw.localPosition.x) ||
      !std::isfinite(raw.localPosition.y) ||
      !std::isfinite(raw.boundsOrigin.x) ||
      !std::isfinite(raw.boundsOrigin.y) ||
      !std::isfinite(raw.boundsSize.x) ||
      !std::isfinite(raw.boundsSize.y) || !std::isfinite(raw.pressure) ||
      !std::isfinite(raw.tiltScaled.x) ||
      !std::isfinite(raw.tiltScaled.y) ||
      !std::isfinite(raw.tangentialPressure) ||
      !std::isfinite(raw.rotationDegrees)) {
    return std::nullopt;
  }

  const float localX = raw.localPosition.x - raw.boundsOrigin.x;
  const float localY = raw.localPosition.y - raw.boundsOrigin.y;
  const core::Vec2 screenPosition{
      localX, raw.viewFlipped ? localY : raw.boundsSize.y - localY};
  if (!std::isfinite(screenPosition.x) ||
      !std::isfinite(screenPosition.y)) {
    return std::nullopt;
  }

  MacosTabletSample sample;
  sample.pointerId = pointerId;
  sample.timestampMicros = timestampMicros(raw.timestampSeconds);
  sample.screenPosition = screenPosition;
  sample.pressure = boundedFloat(raw.pressure, 0.0, 1.0);
  sample.tiltScaled = {
      boundedFloat(raw.tiltScaled.x, -1.0, 1.0),
      boundedFloat(raw.tiltScaled.y, -1.0, 1.0)};
  sample.tangentialPressure =
      boundedFloat(raw.tangentialPressure, -1.0, 1.0);
  sample.rotationDegrees = finiteFloat(raw.rotationDegrees);
  sample.buttonMask = raw.buttonMask;
  sample.eventNumber = raw.eventNumber;
  sample.device = device;
  sample.source = raw.source;
  sample.phase = phase;
  sample.intent = intentForTool(device.tool);
  return sample;
}

MacosTabletSession::DeviceSlot* MacosTabletSession::findDevice(
    std::uint64_t deviceId) noexcept {
  for (auto& slot : devices_) {
    if (slot.occupied && slot.profile.identity.deviceId == deviceId) {
      return &slot;
    }
  }
  return nullptr;
}

const MacosTabletSession::DeviceSlot* MacosTabletSession::findDevice(
    std::uint64_t deviceId) const noexcept {
  for (const auto& slot : devices_) {
    if (slot.occupied && slot.profile.identity.deviceId == deviceId) {
      return &slot;
    }
  }
  return nullptr;
}

MacosTabletSession::ContactSlot* MacosTabletSession::findContact(
    std::uint64_t deviceId) noexcept {
  for (auto& contact : contacts_) {
    if (contact.occupied && contact.deviceId == deviceId) return &contact;
  }
  return nullptr;
}

MacosTabletSession::ContactSlot* MacosTabletSession::findFreeContact()
    noexcept {
  for (auto& contact : contacts_) {
    if (!contact.occupied) return &contact;
  }
  return nullptr;
}

std::optional<std::uint64_t> MacosTabletSession::allocatePointerId()
    noexcept {
  if (nextPointerId_ == 0) return std::nullopt;
  const std::uint64_t allocated = nextPointerId_;
  nextPointerId_ = nextPointerId_ == std::numeric_limits<std::uint64_t>::max()
                       ? 0
                       : nextPointerId_ + 1;
  return allocated;
}

void MacosTabletSession::append(MacosTabletSessionOutput& output,
                                const MacosTabletSample& sample) noexcept {
  if (output.count >= output.samples.size()) return;
  output.samples[output.count++] = sample;
}

bool MacosTabletSession::sameMeasurement(
    const MacosTabletSample& left,
    const MacosTabletSample& right) noexcept {
  return left.device.identity.deviceId == right.device.identity.deviceId &&
         left.timestampMicros == right.timestampMicros &&
         left.screenPosition == right.screenPosition &&
         left.pressure == right.pressure &&
         left.tiltScaled == right.tiltScaled &&
         left.tangentialPressure == right.tangentialPressure &&
         left.rotationDegrees == right.rotationDegrees &&
         left.buttonMask == right.buttonMask;
}

void MacosTabletSession::cancelContact(
    ContactSlot& contact, MacosTabletSessionOutput& output) noexcept {
  if (contact.occupied && contact.lastSample) {
    MacosTabletSample cancelled = *contact.lastSample;
    cancelled.phase = input::PointerPhase::Cancel;
    append(output, cancelled);
  }
  contact = {};
}

MacosTabletSessionOutput MacosTabletSession::consumeProximity(
    const RawMacTabletProximityEvent& raw) {
  MacosTabletSessionOutput output;
  const std::uint64_t deviceId = raw.profile.identity.deviceId;
  if (deviceId == 0) return output;

  DeviceSlot* existing = findDevice(deviceId);
  if (!raw.entering) {
    if (existing == nullptr ||
        !samePhysicalDevice(existing->profile.identity,
                            raw.profile.identity)) {
      return output;
    }
    if (ContactSlot* contact = findContact(deviceId)) {
      cancelContact(*contact, output);
    }
    *existing = {};
    return output;
  }

  if (existing != nullptr) {
    if (ContactSlot* contact = findContact(deviceId)) {
      cancelContact(*contact, output);
    }
    existing->profile = raw.profile;
    return output;
  }

  for (auto& slot : devices_) {
    if (!slot.occupied) {
      slot.occupied = true;
      slot.profile = raw.profile;
      break;
    }
  }
  return output;
}

MacosTabletSessionOutput MacosTabletSession::consumePoint(
    const RawMacTabletPointEvent& raw) {
  MacosTabletSessionOutput output;
  if (raw.deviceId == 0) return output;
  const DeviceSlot* device = findDevice(raw.deviceId);
  if (device == nullptr) return output;

  const bool native =
      raw.source == MacTabletPointSource::NativeTabletPoint;
  if ((native && raw.phase != MacTabletPointPhase::NativeUpdate) ||
      (!native && raw.phase == MacTabletPointPhase::NativeUpdate)) {
    return output;
  }

  ContactSlot* contact = findContact(raw.deviceId);
  if (raw.phase == MacTabletPointPhase::Down) {
    if (contact != nullptr) cancelContact(*contact, output);
    contact = findFreeContact();
    if (contact == nullptr) return output;
    const std::optional<std::uint64_t> pointerId = allocatePointerId();
    if (!pointerId) return output;
    const auto sample = MacosTabletAdapter::normalize(
        raw, device->profile, *pointerId, input::PointerPhase::Down);
    if (!sample) return output;
    contact->occupied = true;
    contact->deviceId = raw.deviceId;
    contact->pointerId = *pointerId;
    contact->device = device->profile;
    contact->lastSample = *sample;
    append(output, *sample);
    return output;
  }

  if (contact == nullptr) return output;
  const input::PointerPhase phase = pointerPhase(raw.phase);
  const auto sample = MacosTabletAdapter::normalize(
      raw, contact->device, contact->pointerId, phase);
  if (!sample) return output;
  if (phase == input::PointerPhase::Move && contact->lastSample &&
      sameMeasurement(*contact->lastSample, *sample)) {
    return output;
  }

  append(output, *sample);
  if (phase == input::PointerPhase::Up ||
      phase == input::PointerPhase::Cancel) {
    *contact = {};
  } else {
    contact->lastSample = *sample;
  }
  return output;
}

MacosTabletSessionOutput MacosTabletSession::reset() {
  MacosTabletSessionOutput output;
  for (auto& contact : contacts_) cancelContact(contact, output);
  for (auto& device : devices_) device = {};
  return output;
}

std::size_t MacosTabletSession::proximateDeviceCount() const noexcept {
  std::size_t count = 0;
  for (const auto& device : devices_) {
    if (device.occupied) ++count;
  }
  return count;
}

std::size_t MacosTabletSession::activeContactCount() const noexcept {
  std::size_t count = 0;
  for (const auto& contact : contacts_) {
    if (contact.occupied) ++count;
  }
  return count;
}

}  // namespace canvas::macos
