#include "platform/macos/macos_pointer_adapter.h"

#include <gtest/gtest.h>

#include <limits>

namespace canvas::macos {
namespace {

RawMacMouseEvent eventAt(float x, float y) {
  RawMacMouseEvent raw;
  raw.localPosition = {x, y};
  raw.boundsOrigin = {10.0F, 20.0F};
  raw.boundsSize = {300.0F, 200.0F};
  raw.timestampSeconds = 1.25;
  raw.phase = MacMousePhase::Down;
  return raw;
}

TEST(MacosPointerAdapterTest, NormalizesFlippedNonZeroBoundsInLogicalPoints) {
  RawMacMouseEvent raw = eventAt(35.0F, 70.0F);
  raw.viewFlipped = true;
  raw.backingScale = 2.0;
  raw.pressure = 0.75;

  const input::PointerSample sample = MacosPointerAdapter::normalize(raw, 7);

  EXPECT_EQ(sample.pointerId, 7U);
  EXPECT_EQ(sample.screenPosition, (core::Vec2{25.0F, 50.0F}));
  EXPECT_EQ(sample.timestampMicros, 1'250'000U);
  EXPECT_FLOAT_EQ(sample.pressure, 0.75F);
  EXPECT_FLOAT_EQ(sample.tiltXDegrees, 0.0F);
  EXPECT_FLOAT_EQ(sample.tiltYDegrees, 0.0F);
  EXPECT_EQ(sample.kind, input::PointerKind::Mouse);
  EXPECT_EQ(sample.phase, input::PointerPhase::Down);
  EXPECT_FALSE(sample.predicted);
}

TEST(MacosPointerAdapterTest, FlipsUnflippedViewExactlyOnce) {
  RawMacMouseEvent raw = eventAt(35.0F, 70.0F);
  raw.viewFlipped = false;

  const input::PointerSample sample = MacosPointerAdapter::normalize(raw, 1);

  EXPECT_EQ(sample.screenPosition, (core::Vec2{25.0F, 150.0F}));
}

TEST(MacosPointerAdapterTest, BackingScaleDoesNotChangeLogicalCoordinates) {
  RawMacMouseEvent oneX = eventAt(35.0F, 70.0F);
  RawMacMouseEvent twoX = oneX;
  oneX.backingScale = 1.0;
  twoX.backingScale = 2.0;

  EXPECT_EQ(MacosPointerAdapter::normalize(oneX, 1).screenPosition,
            MacosPointerAdapter::normalize(twoX, 1).screenPosition);
}

TEST(MacosPointerAdapterTest, PreservesOutOfBoundsCoordinates) {
  RawMacMouseEvent raw = eventAt(-5.0F, 250.0F);
  raw.viewFlipped = true;

  const input::PointerSample sample = MacosPointerAdapter::normalize(raw, 1);

  EXPECT_EQ(sample.screenPosition, (core::Vec2{-15.0F, 230.0F}));
}

TEST(MacosPointerAdapterTest, SanitizesTimestampAndSaturatesOverflow) {
  RawMacMouseEvent negative = eventAt(0.0F, 0.0F);
  RawMacMouseEvent nan = negative;
  RawMacMouseEvent finiteOverflow = negative;
  RawMacMouseEvent positiveInfinity = negative;
  RawMacMouseEvent negativeInfinity = negative;
  negative.timestampSeconds = -1.0;
  nan.timestampSeconds = std::numeric_limits<double>::quiet_NaN();
  finiteOverflow.timestampSeconds = std::numeric_limits<double>::max();
  positiveInfinity.timestampSeconds =
      std::numeric_limits<double>::infinity();
  negativeInfinity.timestampSeconds =
      -std::numeric_limits<double>::infinity();

  EXPECT_EQ(MacosPointerAdapter::normalize(negative, 1).timestampMicros, 0U);
  EXPECT_EQ(MacosPointerAdapter::normalize(nan, 1).timestampMicros, 0U);
  EXPECT_EQ(MacosPointerAdapter::normalize(negativeInfinity, 1).timestampMicros,
            0U);
  EXPECT_EQ(MacosPointerAdapter::normalize(finiteOverflow, 1).timestampMicros,
            std::numeric_limits<std::uint64_t>::max());
  EXPECT_EQ(MacosPointerAdapter::normalize(positiveInfinity, 1).timestampMicros,
            std::numeric_limits<std::uint64_t>::max());
}

TEST(MacosPointerAdapterTest, DefaultsInvalidPressureAndClampsFinitePressure) {
  RawMacMouseEvent missing = eventAt(0.0F, 0.0F);
  RawMacMouseEvent nan = missing;
  RawMacMouseEvent infinity = missing;
  RawMacMouseEvent low = missing;
  RawMacMouseEvent high = missing;
  nan.pressure = std::numeric_limits<double>::quiet_NaN();
  infinity.pressure = std::numeric_limits<double>::infinity();
  low.pressure = -0.25;
  high.pressure = 1.25;

  EXPECT_FLOAT_EQ(MacosPointerAdapter::normalize(missing, 1).pressure, 0.5F);
  EXPECT_FLOAT_EQ(MacosPointerAdapter::normalize(nan, 1).pressure, 0.5F);
  EXPECT_FLOAT_EQ(MacosPointerAdapter::normalize(infinity, 1).pressure, 0.5F);
  EXPECT_FLOAT_EQ(MacosPointerAdapter::normalize(low, 1).pressure, 0.0F);
  EXPECT_FLOAT_EQ(MacosPointerAdapter::normalize(high, 1).pressure, 1.0F);
}

TEST(MacosPointerAdapterTest, MapsEveryMousePhase) {
  RawMacMouseEvent raw = eventAt(0.0F, 0.0F);
  const struct {
    MacMousePhase raw;
    input::PointerPhase normalized;
  } cases[] = {
      {MacMousePhase::Down, input::PointerPhase::Down},
      {MacMousePhase::Move, input::PointerPhase::Move},
      {MacMousePhase::Up, input::PointerPhase::Up},
      {MacMousePhase::Cancel, input::PointerPhase::Cancel},
  };

  for (const auto& item : cases) {
    raw.phase = item.raw;
    EXPECT_EQ(MacosPointerAdapter::normalize(raw, 1).phase, item.normalized);
  }
}

}  // namespace
}  // namespace canvas::macos
