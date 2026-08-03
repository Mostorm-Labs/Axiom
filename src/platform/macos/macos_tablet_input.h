#pragma once

#include "canvas/core/geometry.h"
#include "canvas/input/pointer_sample.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace canvas::macos {

enum class MacTabletPointSource { AssociatedMouse, NativeTabletPoint };
enum class MacTabletPointPhase { Down, Move, Up, Cancel, NativeUpdate };
enum class MacTabletTool { Unknown, Pen, Cursor, Eraser };
enum class MacTabletIntent { Ink, EraserPending, Unsupported };

// Values intentionally mirror the opaque capability bits delivered by
// NSEvent. The pure seam does not include AppKit or IOKit headers.
enum class MacTabletCapability : std::uint64_t {
  DeviceId = 0x0001,
  Buttons = 0x0040,
  TiltX = 0x0080,
  TiltY = 0x0100,
  Pressure = 0x0400,
  TangentialPressure = 0x0800,
  Orientation = 0x1000,
  Rotation = 0x2000,
};

constexpr bool hasMacTabletCapability(
    std::uint64_t mask, MacTabletCapability capability) noexcept {
  return (mask & static_cast<std::uint64_t>(capability)) != 0;
}

struct MacTabletDeviceIdentity {
  std::uint64_t deviceId = 0;
  std::uint64_t pointingDeviceId = 0;
  std::uint64_t systemTabletId = 0;
  std::uint64_t uniqueId = 0;
};

struct MacTabletDeviceProfile {
  MacTabletDeviceIdentity identity;
  std::uint64_t capabilityMask = 0;
  MacTabletTool tool = MacTabletTool::Unknown;
};

struct RawMacTabletProximityEvent {
  MacTabletDeviceProfile profile;
  double timestampSeconds = 0.0;
  bool entering = false;
};

struct RawMacTabletPointEvent {
  core::Vec2 localPosition;
  core::Vec2 boundsOrigin;
  core::Vec2 boundsSize;
  bool viewFlipped = true;
  double backingScale = 1.0;
  double timestampSeconds = 0.0;
  double pressure = 0.0;
  core::Vec2 tiltScaled;
  double tangentialPressure = 0.0;
  double rotationDegrees = 0.0;
  std::uint64_t buttonMask = 0;
  std::uint64_t deviceId = 0;
  std::optional<std::int64_t> eventNumber;
  MacTabletPointSource source = MacTabletPointSource::NativeTabletPoint;
  MacTabletPointPhase phase = MacTabletPointPhase::NativeUpdate;
};

struct MacosTabletSample {
  std::uint64_t pointerId = 0;
  std::uint64_t timestampMicros = 0;
  core::Vec2 screenPosition;
  float pressure = 0.0F;
  core::Vec2 tiltScaled;
  float tangentialPressure = 0.0F;
  float rotationDegrees = 0.0F;
  std::uint64_t buttonMask = 0;
  std::optional<std::int64_t> eventNumber;
  MacTabletDeviceProfile device;
  MacTabletPointSource source = MacTabletPointSource::NativeTabletPoint;
  input::PointerPhase phase = input::PointerPhase::Move;
  MacTabletIntent intent = MacTabletIntent::Unsupported;

  bool pressureSupported() const noexcept {
    return hasMacTabletCapability(device.capabilityMask,
                                  MacTabletCapability::Pressure);
  }
  bool tiltXSupported() const noexcept {
    return hasMacTabletCapability(device.capabilityMask,
                                  MacTabletCapability::TiltX);
  }
  bool tiltYSupported() const noexcept {
    return hasMacTabletCapability(device.capabilityMask,
                                  MacTabletCapability::TiltY);
  }
  bool eligibleForInk() const noexcept {
    return intent == MacTabletIntent::Ink;
  }
};

class MacosTabletAdapter {
 public:
  static std::optional<MacosTabletSample> normalize(
      const RawMacTabletPointEvent& raw,
      const MacTabletDeviceProfile& device, std::uint64_t pointerId,
      input::PointerPhase phase);
};

struct MacosTabletSessionOutput {
  static constexpr std::size_t capacity = 4;
  std::array<MacosTabletSample, capacity> samples;
  std::size_t count = 0;

  bool empty() const noexcept { return count == 0; }
  std::size_t size() const noexcept { return count; }
  const MacosTabletSample& operator[](std::size_t index) const noexcept {
    return samples[index];
  }
};

class MacosTabletSession {
 public:
  static constexpr std::size_t maxProximateDevices = 8;
  static constexpr std::size_t maxActiveContacts =
      MacosTabletSessionOutput::capacity;

  MacosTabletSessionOutput consumeProximity(
      const RawMacTabletProximityEvent& raw);
  MacosTabletSessionOutput consumePoint(const RawMacTabletPointEvent& raw);
  MacosTabletSessionOutput reset();

  std::size_t proximateDeviceCount() const noexcept;
  std::size_t activeContactCount() const noexcept;

 private:
  struct DeviceSlot {
    bool occupied = false;
    MacTabletDeviceProfile profile;
  };

  struct ContactSlot {
    bool occupied = false;
    std::uint64_t deviceId = 0;
    std::uint64_t pointerId = 0;
    MacTabletDeviceProfile device;
    std::optional<MacosTabletSample> lastSample;
  };

  DeviceSlot* findDevice(std::uint64_t deviceId) noexcept;
  const DeviceSlot* findDevice(std::uint64_t deviceId) const noexcept;
  ContactSlot* findContact(std::uint64_t deviceId) noexcept;
  ContactSlot* findFreeContact() noexcept;
  std::optional<std::uint64_t> allocatePointerId() noexcept;
  static void append(MacosTabletSessionOutput& output,
                     const MacosTabletSample& sample) noexcept;
  static bool sameMeasurement(const MacosTabletSample& left,
                              const MacosTabletSample& right) noexcept;
  static void cancelContact(ContactSlot& contact,
                            MacosTabletSessionOutput& output) noexcept;

  std::array<DeviceSlot, maxProximateDevices> devices_;
  std::array<ContactSlot, maxActiveContacts> contacts_;
  std::uint64_t nextPointerId_ = 1;
};

}  // namespace canvas::macos
