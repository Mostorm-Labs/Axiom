#include "platform/windows/win_pointer_adapter.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <vector>

namespace canvas::windows {
namespace {

TEST(WinPointerAdapterTest, NormalizesCoordinatesPressureAndTimestamp) {
  const RawPenPoint raw{7, 5000, 120, 80, 768, 12, -8};

  const input::PointerSample sample = WinPointerAdapter::normalize(
      raw, POINT{20, 10}, 1000, input::PointerPhase::Down);

  EXPECT_EQ(sample.pointerId, 7U);
  EXPECT_EQ(sample.timestampMicros, 5'000'000U);
  EXPECT_FLOAT_EQ(sample.screenPosition.x, 100.0F);
  EXPECT_FLOAT_EQ(sample.screenPosition.y, 70.0F);
  EXPECT_FLOAT_EQ(sample.pressure, 0.75F);
  EXPECT_FLOAT_EQ(sample.tiltXDegrees, 12.0F);
  EXPECT_FLOAT_EQ(sample.tiltYDegrees, -8.0F);
  EXPECT_EQ(sample.kind, input::PointerKind::Pen);
  EXPECT_EQ(sample.phase, input::PointerPhase::Down);
  EXPECT_FALSE(sample.predicted);
}

TEST(WinPointerAdapterTest, ReturnsHistoryInOldestFirstOrder) {
  const std::vector<RawPenPoint> newestFirst{
      RawPenPoint{1, 300, 30, 0},
      RawPenPoint{1, 200, 20, 0},
      RawPenPoint{1, 100, 10, 0},
  };

  const auto samples = WinPointerAdapter::normalizeHistory(
      newestFirst, POINT{0, 0}, 100, input::PointerPhase::Move);

  ASSERT_EQ(samples.size(), 3U);
  EXPECT_FLOAT_EQ(samples[0].screenPosition.x, 10.0F);
  EXPECT_FLOAT_EQ(samples[1].screenPosition.x, 20.0F);
  EXPECT_FLOAT_EQ(samples[2].screenPosition.x, 30.0F);
  EXPECT_EQ(samples[0].timestampMicros, 1'000'000U);
  EXPECT_EQ(samples[2].timestampMicros, 3'000'000U);
}

TEST(WinPointerAdapterTest, ClampsPressureToNormalizedRange) {
  const input::PointerSample low = WinPointerAdapter::normalize(
      RawPenPoint{1, 0, 0, 0, 0}, POINT{0, 0}, 1,
      input::PointerPhase::Move);
  const input::PointerSample high = WinPointerAdapter::normalize(
      RawPenPoint{1, 0, 0, 0, 2048}, POINT{0, 0}, 1,
      input::PointerPhase::Move);

  EXPECT_FLOAT_EQ(low.pressure, 0.0F);
  EXPECT_FLOAT_EQ(high.pressure, 1.0F);
}

TEST(WinPointerAdapterTest, HandlesZeroQpcFrequencySafely) {
  const input::PointerSample sample = WinPointerAdapter::normalize(
      RawPenPoint{1, 123456, 0, 0}, POINT{0, 0}, 0,
      input::PointerPhase::Move);

  EXPECT_EQ(sample.timestampMicros, 0U);
}

TEST(WinPointerAdapterTest, PreservesSuppliedTouchKindWithoutPrediction) {
  const input::PointerSample sample = WinPointerAdapter::normalize(
      RawPenPoint{2, 1, 5, 6}, POINT{0, 0}, 1,
      input::PointerPhase::Up, input::PointerKind::Touch);

  EXPECT_EQ(sample.kind, input::PointerKind::Touch);
  EXPECT_EQ(sample.phase, input::PointerPhase::Up);
  EXPECT_FALSE(sample.predicted);
}

TEST(WinPointerAdapterTest, EmptyHistoryProducesNoSamples) {
  const auto samples = WinPointerAdapter::normalizeHistory(
      {}, POINT{0, 0}, 1, input::PointerPhase::Move,
      input::PointerKind::Touch);

  EXPECT_TRUE(samples.empty());
}

TEST(WinPointerAdapterTest, ExtremeScreenCoordinatesRemainFinite) {
  const RawPenPoint raw{1, 0, std::numeric_limits<long>::max(),
                        std::numeric_limits<long>::min()};
  const POINT origin{std::numeric_limits<long>::min(),
                     std::numeric_limits<long>::max()};

  const auto sample = WinPointerAdapter::normalize(
      raw, origin, 1, input::PointerPhase::Move);

  EXPECT_TRUE(std::isfinite(sample.screenPosition.x));
  EXPECT_TRUE(std::isfinite(sample.screenPosition.y));
}

}  // namespace
}  // namespace canvas::windows
