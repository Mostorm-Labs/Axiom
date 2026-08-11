#include "platform/macos/macos_tablet_pointer_bridge.h"

#include <gtest/gtest.h>

#include <limits>

namespace canvas::macos {
namespace {

constexpr std::uint64_t capability(MacTabletCapability value) {
  return static_cast<std::uint64_t>(value);
}

MacosTabletSample tabletSample(
    std::uint64_t pointerId, input::PointerPhase phase,
    MacTabletTool tool = MacTabletTool::Pen,
    MacTabletIntent intent = MacTabletIntent::Ink,
    std::uint64_t capabilityMask =
        capability(MacTabletCapability::Pressure) |
        capability(MacTabletCapability::TiltX) |
        capability(MacTabletCapability::TiltY)) {
  MacosTabletSample sample;
  sample.pointerId = pointerId;
  sample.timestampMicros = 123456;
  sample.screenPosition = {25.0F, 50.0F};
  sample.pressure = 0.8F;
  sample.tiltScaled = {0.25F, -0.5F};
  sample.tangentialPressure = -0.2F;
  sample.rotationDegrees = 30.0F;
  sample.buttonMask = 1;
  sample.device.identity.deviceId = 7;
  sample.device.capabilityMask = capabilityMask;
  sample.device.tool = tool;
  sample.source = MacTabletPointSource::AssociatedMouse;
  sample.phase = phase;
  sample.intent = intent;
  return sample;
}

TEST(MacosTabletPointerBridgeTest,
     ConvertsOnlyInkPenWithoutInventingDegreeTilt) {
  MacosTabletSample tablet = tabletSample(42, input::PointerPhase::Down);
  const core::Vec2 originalScaledTilt = tablet.tiltScaled;

  const auto pointer = MacosTabletPointerBridge::convertSample(tablet);

  ASSERT_TRUE(pointer.has_value());
  EXPECT_EQ(pointer->pointerId, tablet.pointerId);
  EXPECT_EQ(pointer->timestampMicros, tablet.timestampMicros);
  EXPECT_EQ(pointer->screenPosition, tablet.screenPosition);
  EXPECT_FLOAT_EQ(pointer->pressure, 0.8F);
  EXPECT_FLOAT_EQ(pointer->tiltXDegrees, 0.0F);
  EXPECT_FLOAT_EQ(pointer->tiltYDegrees, 0.0F);
  EXPECT_EQ(pointer->kind, input::PointerKind::Pen);
  EXPECT_EQ(pointer->phase, input::PointerPhase::Down);
  EXPECT_FALSE(pointer->predicted);
  EXPECT_EQ(tablet.tiltScaled, originalScaledTilt)
      << "scaled AppKit tilt must remain available on the tablet sample";
}

TEST(MacosTabletPointerBridgeTest,
     UsesNeutralPressureWhenCapabilityIsMissing) {
  MacosTabletSample tablet = tabletSample(
      1, input::PointerPhase::Move, MacTabletTool::Pen,
      MacTabletIntent::Ink, capability(MacTabletCapability::TiltX));
  tablet.pressure = 0.91F;

  const auto pointer = MacosTabletPointerBridge::convertSample(tablet);

  ASSERT_TRUE(pointer.has_value());
  EXPECT_FLOAT_EQ(pointer->pressure, 0.5F);
}

TEST(MacosTabletPointerBridgeTest,
     RejectsEraserCursorUnknownAndIntentMismatch) {
  EXPECT_FALSE(MacosTabletPointerBridge::convertSample(tabletSample(
                   1, input::PointerPhase::Down, MacTabletTool::Eraser,
                   MacTabletIntent::EraserPending))
                   .has_value());
  EXPECT_FALSE(MacosTabletPointerBridge::convertSample(tabletSample(
                   2, input::PointerPhase::Down, MacTabletTool::Cursor,
                   MacTabletIntent::Unsupported))
                   .has_value());
  EXPECT_FALSE(MacosTabletPointerBridge::convertSample(tabletSample(
                   3, input::PointerPhase::Down, MacTabletTool::Unknown,
                   MacTabletIntent::Unsupported))
                   .has_value());
  EXPECT_FALSE(MacosTabletPointerBridge::convertSample(tabletSample(
                   4, input::PointerPhase::Down, MacTabletTool::Pen,
                   MacTabletIntent::Unsupported))
                   .has_value());
  EXPECT_FALSE(MacosTabletPointerBridge::convertSample(tabletSample(
                   5, input::PointerPhase::Down, MacTabletTool::Eraser,
                   MacTabletIntent::Ink))
                   .has_value());
}

TEST(MacosTabletPointerBridgeTest, RejectsInvalidSupportedPenSamples) {
  MacosTabletSample invalid = tabletSample(0, input::PointerPhase::Down);
  EXPECT_FALSE(MacosTabletPointerBridge::convertSample(invalid).has_value());

  invalid = tabletSample(1, input::PointerPhase::Down);
  invalid.device.identity.deviceId = 0;
  EXPECT_FALSE(MacosTabletPointerBridge::convertSample(invalid).has_value());

  invalid = tabletSample(1, input::PointerPhase::Down);
  invalid.screenPosition.x = std::numeric_limits<float>::quiet_NaN();
  EXPECT_FALSE(MacosTabletPointerBridge::convertSample(invalid).has_value());

  invalid = tabletSample(1, input::PointerPhase::Down);
  invalid.pressure = std::numeric_limits<float>::infinity();
  EXPECT_FALSE(MacosTabletPointerBridge::convertSample(invalid).has_value());

  invalid = tabletSample(1, input::PointerPhase::Down);
  invalid.pressure = -0.01F;
  EXPECT_FALSE(MacosTabletPointerBridge::convertSample(invalid).has_value());

  invalid = tabletSample(1, input::PointerPhase::Down);
  invalid.pressure = 1.01F;
  EXPECT_FALSE(MacosTabletPointerBridge::convertSample(invalid).has_value());
}

TEST(MacosTabletPointerBridgeTest,
     FiltersFixedOutputWhilePreservingEligibleOrder) {
  static_assert(MacosTabletPointerOutput::capacity ==
                MacosTabletSessionOutput::capacity);
  MacosTabletSessionOutput tabletOutput;
  tabletOutput.samples[0] = tabletSample(
      9, input::PointerPhase::Down, MacTabletTool::Eraser,
      MacTabletIntent::EraserPending);
  tabletOutput.samples[1] = tabletSample(2, input::PointerPhase::Down);
  tabletOutput.samples[2] = tabletSample(2, input::PointerPhase::Move);
  tabletOutput.samples[3] = tabletSample(
      8, input::PointerPhase::Down, MacTabletTool::Unknown,
      MacTabletIntent::Unsupported);
  tabletOutput.count = 4;

  const MacosTabletPointerOutput pointerOutput =
      MacosTabletPointerBridge::convertOutput(tabletOutput);

  ASSERT_EQ(pointerOutput.size(), 2U);
  EXPECT_EQ(pointerOutput[0].pointerId, 2U);
  EXPECT_EQ(pointerOutput[0].phase, input::PointerPhase::Down);
  EXPECT_EQ(pointerOutput[1].pointerId, 2U);
  EXPECT_EQ(pointerOutput[1].phase, input::PointerPhase::Move);
}

}  // namespace
}  // namespace canvas::macos
