#include "platform/macos/macos_tablet_input.h"

#include <gtest/gtest.h>

#include <limits>

namespace canvas::macos {
namespace {

constexpr std::uint64_t capability(MacTabletCapability value) {
  return static_cast<std::uint64_t>(value);
}

MacTabletDeviceProfile profile(
    std::uint64_t deviceId, MacTabletTool tool = MacTabletTool::Pen,
    std::uint64_t capabilityMask =
        capability(MacTabletCapability::DeviceId) |
        capability(MacTabletCapability::Pressure) |
        capability(MacTabletCapability::TiltX) |
        capability(MacTabletCapability::TiltY)) {
  MacTabletDeviceProfile result;
  result.identity.deviceId = deviceId;
  result.identity.pointingDeviceId = deviceId + 100;
  result.identity.systemTabletId = deviceId + 200;
  result.identity.uniqueId = deviceId + 300;
  result.capabilityMask = capabilityMask;
  result.tool = tool;
  return result;
}

RawMacTabletProximityEvent proximity(const MacTabletDeviceProfile& device,
                                     bool entering) {
  RawMacTabletProximityEvent raw;
  raw.profile = device;
  raw.timestampSeconds = 1.0;
  raw.entering = entering;
  return raw;
}

RawMacTabletPointEvent point(
    std::uint64_t deviceId, MacTabletPointPhase phase,
    MacTabletPointSource source = MacTabletPointSource::AssociatedMouse,
    double timestamp = 2.0, float x = 25.0F, double pressure = 0.75) {
  RawMacTabletPointEvent raw;
  raw.localPosition = {x, 70.0F};
  raw.boundsOrigin = {10.0F, 20.0F};
  raw.boundsSize = {300.0F, 200.0F};
  raw.viewFlipped = true;
  raw.backingScale = 2.0;
  raw.timestampSeconds = timestamp;
  raw.pressure = pressure;
  raw.tiltScaled = {0.25F, -0.5F};
  raw.tangentialPressure = -0.25;
  raw.rotationDegrees = 37.5;
  raw.buttonMask = 1;
  raw.deviceId = deviceId;
  raw.source = source;
  raw.phase = phase;
  if (source == MacTabletPointSource::AssociatedMouse) {
    raw.eventNumber = static_cast<std::int64_t>(timestamp * 10.0);
  }
  return raw;
}

TEST(MacosTabletInputTest,
     NormalizesPressureIdentityAndScaledTiltWithoutInventingDegrees) {
  const MacTabletDeviceProfile device = profile(7);
  const RawMacTabletPointEvent raw =
      point(7, MacTabletPointPhase::Down);

  const auto sample = MacosTabletAdapter::normalize(
      raw, device, 42, input::PointerPhase::Down);

  ASSERT_TRUE(sample.has_value());
  EXPECT_EQ(sample->pointerId, 42U);
  EXPECT_EQ(sample->timestampMicros, 2'000'000U);
  EXPECT_EQ(sample->screenPosition, (core::Vec2{15.0F, 50.0F}));
  EXPECT_FLOAT_EQ(sample->pressure, 0.75F);
  EXPECT_EQ(sample->tiltScaled, (core::Vec2{0.25F, -0.5F}));
  EXPECT_FLOAT_EQ(sample->tangentialPressure, -0.25F);
  EXPECT_FLOAT_EQ(sample->rotationDegrees, 37.5F);
  EXPECT_EQ(sample->buttonMask, 1U);
  EXPECT_EQ(sample->eventNumber, raw.eventNumber);
  EXPECT_EQ(sample->device.identity.deviceId, 7U);
  EXPECT_EQ(sample->device.identity.pointingDeviceId, 107U);
  EXPECT_EQ(sample->device.identity.systemTabletId, 207U);
  EXPECT_EQ(sample->device.identity.uniqueId, 307U);
  EXPECT_TRUE(sample->pressureSupported());
  EXPECT_TRUE(sample->tiltXSupported());
  EXPECT_TRUE(sample->tiltYSupported());
  EXPECT_EQ(sample->intent, MacTabletIntent::Ink);
  EXPECT_TRUE(sample->eligibleForInk());
}

TEST(MacosTabletInputTest, SanitizesOfficiallyBoundedValuesAndRejectsNaN) {
  const MacTabletDeviceProfile device = profile(7);
  RawMacTabletPointEvent bounded =
      point(7, MacTabletPointPhase::Move);
  bounded.pressure = 2.0;
  bounded.tiltScaled = {-2.0F, 3.0F};
  bounded.tangentialPressure = -4.0;

  const auto sample = MacosTabletAdapter::normalize(
      bounded, device, 1, input::PointerPhase::Move);

  ASSERT_TRUE(sample.has_value());
  EXPECT_FLOAT_EQ(sample->pressure, 1.0F);
  EXPECT_EQ(sample->tiltScaled, (core::Vec2{-1.0F, 1.0F}));
  EXPECT_FLOAT_EQ(sample->tangentialPressure, -1.0F);

  RawMacTabletPointEvent nan = bounded;
  nan.localPosition.x = std::numeric_limits<float>::quiet_NaN();
  EXPECT_FALSE(MacosTabletAdapter::normalize(
                   nan, device, 1, input::PointerPhase::Move)
                   .has_value());
  nan = bounded;
  nan.pressure = std::numeric_limits<double>::quiet_NaN();
  EXPECT_FALSE(MacosTabletAdapter::normalize(
                   nan, device, 1, input::PointerPhase::Move)
                   .has_value());
}

TEST(MacosTabletInputTest, EraserUnknownAndCursorAreNeverOrdinaryInk) {
  const RawMacTabletPointEvent raw =
      point(7, MacTabletPointPhase::Down);

  const auto eraser = MacosTabletAdapter::normalize(
      raw, profile(7, MacTabletTool::Eraser), 1,
      input::PointerPhase::Down);
  const auto unknown = MacosTabletAdapter::normalize(
      raw, profile(7, MacTabletTool::Unknown), 2,
      input::PointerPhase::Down);
  const auto cursor = MacosTabletAdapter::normalize(
      raw, profile(7, MacTabletTool::Cursor), 3,
      input::PointerPhase::Down);

  ASSERT_TRUE(eraser.has_value());
  ASSERT_TRUE(unknown.has_value());
  ASSERT_TRUE(cursor.has_value());
  EXPECT_EQ(eraser->intent, MacTabletIntent::EraserPending);
  EXPECT_EQ(unknown->intent, MacTabletIntent::Unsupported);
  EXPECT_EQ(cursor->intent, MacTabletIntent::Unsupported);
  EXPECT_FALSE(eraser->eligibleForInk());
  EXPECT_FALSE(unknown->eligibleForInk());
  EXPECT_FALSE(cursor->eligibleForInk());
}

TEST(MacosTabletInputTest,
     NativeUpdatesJoinAssociatedMouseContactWithoutASecondDown) {
  MacosTabletSession session;
  const MacTabletDeviceProfile device = profile(7);
  EXPECT_TRUE(session.consumeProximity(proximity(device, true)).empty());

  const auto down =
      session.consumePoint(point(7, MacTabletPointPhase::Down));
  const auto exactNative = session.consumePoint(point(
      7, MacTabletPointPhase::NativeUpdate,
      MacTabletPointSource::NativeTabletPoint));
  const auto pressureNative = session.consumePoint(point(
      7, MacTabletPointPhase::NativeUpdate,
      MacTabletPointSource::NativeTabletPoint, 2.1, 25.0F, 0.9));
  const auto exactAssociated = session.consumePoint(
      point(7, MacTabletPointPhase::Move,
            MacTabletPointSource::AssociatedMouse, 2.1, 25.0F, 0.9));
  const auto up = session.consumePoint(
      point(7, MacTabletPointPhase::Up,
            MacTabletPointSource::AssociatedMouse, 3.0, 40.0F));

  ASSERT_EQ(down.size(), 1U);
  EXPECT_EQ(down[0].phase, input::PointerPhase::Down);
  EXPECT_TRUE(exactNative.empty());
  ASSERT_EQ(pressureNative.size(), 1U);
  EXPECT_EQ(pressureNative[0].phase, input::PointerPhase::Move);
  EXPECT_EQ(pressureNative[0].pointerId, down[0].pointerId);
  EXPECT_TRUE(exactAssociated.empty());
  ASSERT_EQ(up.size(), 1U);
  EXPECT_EQ(up[0].phase, input::PointerPhase::Up);
  EXPECT_EQ(up[0].pointerId, down[0].pointerId);
  EXPECT_EQ(session.activeContactCount(), 0U);
}

TEST(MacosTabletInputTest, OrphanMismatchedAndUnproximatePointsFailClosed) {
  MacosTabletSession session;
  const MacTabletDeviceProfile device = profile(7);
  ASSERT_TRUE(session.consumeProximity(proximity(device, true)).empty());

  EXPECT_TRUE(session.consumePoint(point(
      7, MacTabletPointPhase::NativeUpdate,
      MacTabletPointSource::NativeTabletPoint)).empty());
  EXPECT_TRUE(session.consumePoint(point(
      8, MacTabletPointPhase::NativeUpdate,
      MacTabletPointSource::NativeTabletPoint)).empty());
  EXPECT_TRUE(session.consumePoint(point(
      8, MacTabletPointPhase::Down,
      MacTabletPointSource::AssociatedMouse)).empty());
  EXPECT_EQ(session.activeContactCount(), 0U);
}

TEST(MacosTabletInputTest, DuplicateDownCancelsThenBeginsOneReplacement) {
  MacosTabletSession session;
  ASSERT_TRUE(
      session.consumeProximity(proximity(profile(7), true)).empty());
  const auto first =
      session.consumePoint(point(7, MacTabletPointPhase::Down));
  const auto duplicate = session.consumePoint(
      point(7, MacTabletPointPhase::Down,
            MacTabletPointSource::AssociatedMouse, 3.0, 40.0F));

  ASSERT_EQ(first.size(), 1U);
  ASSERT_EQ(duplicate.size(), 2U);
  EXPECT_EQ(duplicate[0].phase, input::PointerPhase::Cancel);
  EXPECT_EQ(duplicate[0].pointerId, first[0].pointerId);
  EXPECT_EQ(duplicate[1].phase, input::PointerPhase::Down);
  EXPECT_NE(duplicate[1].pointerId, first[0].pointerId);
  EXPECT_EQ(session.activeContactCount(), 1U);
}

TEST(MacosTabletInputTest, ProximityLeaveCancelsExactlyOnceAndDropsProfile) {
  MacosTabletSession session;
  const MacTabletDeviceProfile device = profile(7);
  ASSERT_TRUE(session.consumeProximity(proximity(device, true)).empty());
  const auto down =
      session.consumePoint(point(7, MacTabletPointPhase::Down));

  const auto firstLeave =
      session.consumeProximity(proximity(device, false));
  const auto secondLeave =
      session.consumeProximity(proximity(device, false));

  ASSERT_EQ(down.size(), 1U);
  ASSERT_EQ(firstLeave.size(), 1U);
  EXPECT_EQ(firstLeave[0].phase, input::PointerPhase::Cancel);
  EXPECT_EQ(firstLeave[0].pointerId, down[0].pointerId);
  EXPECT_TRUE(secondLeave.empty());
  EXPECT_EQ(session.activeContactCount(), 0U);
  EXPECT_EQ(session.proximateDeviceCount(), 0U);
}

TEST(MacosTabletInputTest, ResetCancelsEveryContactAndIsIdempotent) {
  MacosTabletSession session;
  for (std::uint64_t deviceId = 1; deviceId <= 2; ++deviceId) {
    ASSERT_TRUE(session.consumeProximity(
                           proximity(profile(deviceId), true))
                    .empty());
    ASSERT_EQ(session.consumePoint(
                         point(deviceId, MacTabletPointPhase::Down,
                               MacTabletPointSource::AssociatedMouse,
                               static_cast<double>(deviceId)))
                  .size(),
              1U);
  }

  const auto firstReset = session.reset();
  const auto secondReset = session.reset();

  EXPECT_EQ(firstReset.size(), 2U);
  EXPECT_EQ(firstReset[0].phase, input::PointerPhase::Cancel);
  EXPECT_EQ(firstReset[1].phase, input::PointerPhase::Cancel);
  EXPECT_TRUE(secondReset.empty());
  EXPECT_EQ(session.activeContactCount(), 0U);
  EXPECT_EQ(session.proximateDeviceCount(), 0U);
}

TEST(MacosTabletInputTest, FixedCapacityFailsClosedWithoutAllocatingMoreSlots) {
  static_assert(MacosTabletSessionOutput::capacity ==
                MacosTabletSession::maxActiveContacts);
  MacosTabletSession session;
  for (std::uint64_t deviceId = 1;
       deviceId <= MacosTabletSession::maxProximateDevices; ++deviceId) {
    EXPECT_TRUE(session.consumeProximity(
                           proximity(profile(deviceId), true))
                    .empty());
  }
  EXPECT_EQ(session.proximateDeviceCount(),
            MacosTabletSession::maxProximateDevices);
  EXPECT_TRUE(session.consumeProximity(
                         proximity(profile(99), true))
                  .empty());
  EXPECT_EQ(session.proximateDeviceCount(),
            MacosTabletSession::maxProximateDevices);

  for (std::uint64_t deviceId = 1;
       deviceId <= MacosTabletSession::maxActiveContacts; ++deviceId) {
    EXPECT_EQ(session.consumePoint(
                         point(deviceId, MacTabletPointPhase::Down,
                               MacTabletPointSource::AssociatedMouse,
                               static_cast<double>(deviceId)))
                  .size(),
              1U);
  }
  EXPECT_TRUE(session.consumePoint(
                         point(MacosTabletSession::maxActiveContacts + 1,
                               MacTabletPointPhase::Down))
                  .empty());
  EXPECT_EQ(session.activeContactCount(),
            MacosTabletSession::maxActiveContacts);
  EXPECT_EQ(session.reset().size(), MacosTabletSessionOutput::capacity);
}

TEST(MacosTabletInputTest, DeviceToolAndCapabilitiesStayFixedForContact) {
  MacosTabletSession session;
  const MacTabletDeviceProfile eraser = profile(
      7, MacTabletTool::Eraser,
      capability(MacTabletCapability::DeviceId) |
          capability(MacTabletCapability::Pressure));
  ASSERT_TRUE(session.consumeProximity(proximity(eraser, true)).empty());

  const auto down =
      session.consumePoint(point(7, MacTabletPointPhase::Down));
  const auto move = session.consumePoint(
      point(7, MacTabletPointPhase::Move,
            MacTabletPointSource::AssociatedMouse, 3.0, 40.0F));

  ASSERT_EQ(down.size(), 1U);
  ASSERT_EQ(move.size(), 1U);
  EXPECT_EQ(down[0].device.tool, MacTabletTool::Eraser);
  EXPECT_EQ(move[0].device.identity.uniqueId, eraser.identity.uniqueId);
  EXPECT_EQ(move[0].device.capabilityMask, eraser.capabilityMask);
  EXPECT_EQ(move[0].intent, MacTabletIntent::EraserPending);
  EXPECT_FALSE(move[0].tiltXSupported());
  EXPECT_FALSE(move[0].tiltYSupported());
  EXPECT_FALSE(move[0].eligibleForInk());
}

}  // namespace
}  // namespace canvas::macos
