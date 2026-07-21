#include "canvas/stroke/stroke_builder.h"

#include <gtest/gtest.h>

using namespace canvas;

namespace {

input::PointerSample makeSample(core::Vec2 position,
                                std::uint64_t timestampMicros,
                                float pressure = 0.5F,
                                bool predicted = false) {
  return {1,
          timestampMicros,
          position,
          pressure,
          0.0F,
          0.0F,
          input::PointerKind::Pen,
          input::PointerPhase::Move,
          predicted};
}

}  // namespace

TEST(StrokeBuilderTest, ReplacesPredictedTailWithRealSamples) {
  stroke::StrokeBuilder builder(2.0F);
  builder.begin({1, 1000, {0, 0}, 0.5F, 0, 0, input::PointerKind::Pen,
                 input::PointerPhase::Down, false});
  builder.append({1, 2000, {10, 0}, 0.5F, 0, 0, input::PointerKind::Pen,
                  input::PointerPhase::Move, true});
  builder.append({1, 2100, {8, 1}, 0.6F, 0, 0, input::PointerKind::Pen,
                  input::PointerPhase::Move, false});

  const auto result = builder.finish();

  ASSERT_EQ(result.points.size(), 2U);
  EXPECT_EQ(result.points.back().position, (core::Vec2{8, 1}));
}

TEST(StrokeBuilderTest, ReturnsAnInflatedDirtyRect) {
  stroke::StrokeBuilder builder(4.0F);
  builder.begin({1, 1000, {10, 20}, 0.5F, 0, 0, input::PointerKind::Pen,
                 input::PointerPhase::Down, false});

  const auto update = builder.append(
      {1, 2000, {30, 40}, 1.0F, 0, 0, input::PointerKind::Pen,
       input::PointerPhase::Move, false});

  EXPECT_TRUE(update.accepted);
  EXPECT_EQ(update.dirtyBounds, (core::Rect{6, 16, 28, 28}));
}

TEST(StrokeBuilderTest, FiltersMovementBelowHalfAPixel) {
  stroke::StrokeBuilder builder(2.0F);
  builder.begin(makeSample({0, 0}, 1000));

  const auto update = builder.append(makeSample({0.3F, 0.3F}, 2000));
  const auto result = builder.finish();

  EXPECT_FALSE(update.accepted);
  EXPECT_EQ(update.dirtyBounds, (core::Rect{}));
  ASSERT_EQ(result.points.size(), 1U);
  EXPECT_EQ(result.points.front().position, (core::Vec2{0, 0}));
}

TEST(StrokeBuilderTest, AcceptsMovementAtHalfAPixel) {
  stroke::StrokeBuilder builder(2.0F);
  builder.begin(makeSample({0, 0}, 1000));

  const auto update = builder.append(makeSample({0.5F, 0}, 2000));
  const auto result = builder.finish();

  EXPECT_TRUE(update.accepted);
  ASSERT_EQ(result.points.size(), 2U);
  EXPECT_EQ(result.points.back().position, (core::Vec2{0.5F, 0}));
}

TEST(StrokeBuilderTest, UsesPreviousPredictedPointForDirtyBounds) {
  stroke::StrokeBuilder builder(1.0F);
  builder.begin(makeSample({0, 0}, 1000));
  ASSERT_TRUE(builder.append(makeSample({10, 0}, 2000, 0.5F, true)).accepted);

  const auto update = builder.append(makeSample({12, 3}, 3000, 0.5F, true));

  EXPECT_TRUE(update.accepted);
  EXPECT_EQ(update.dirtyBounds, (core::Rect{9, -1, 4, 5}));
}

TEST(StrokeBuilderTest, RealSamplesClearThePredictedTail) {
  stroke::StrokeBuilder builder(1.0F);
  builder.begin(makeSample({0, 0}, 1000));
  ASSERT_TRUE(builder.append(makeSample({10, 0}, 2000, 0.5F, true)).accepted);
  ASSERT_TRUE(builder.append(makeSample({8, 0}, 3000)).accepted);

  const auto update = builder.append(makeSample({12, 0}, 4000, 0.5F, true));

  EXPECT_TRUE(update.accepted);
  EXPECT_EQ(update.dirtyBounds, (core::Rect{7, -1, 6, 2}));
}

TEST(StrokeBuilderTest, FinishExcludesPredictedPoints) {
  stroke::StrokeBuilder builder(2.0F);
  builder.begin(makeSample({1, 2}, 1000));
  ASSERT_TRUE(builder.append(makeSample({5, 6}, 2000, 0.7F, true)).accepted);

  const auto result = builder.finish();

  ASSERT_EQ(result.points.size(), 1U);
  EXPECT_EQ(result.points.front().position, (core::Vec2{1, 2}));
}

TEST(StrokeBuilderTest, PreservesPressureTimestampAndWidth) {
  stroke::StrokeBuilder builder(3.5F);
  builder.begin(makeSample({1, 2}, 1000, 0.25F));
  ASSERT_TRUE(builder.append(makeSample({3, 4}, 2000, 0.9F)).accepted);

  const auto result = builder.finish();

  EXPECT_FLOAT_EQ(result.width, 3.5F);
  ASSERT_EQ(result.points.size(), 2U);
  EXPECT_FLOAT_EQ(result.points[0].pressure, 0.25F);
  EXPECT_EQ(result.points[0].timestampMicros, 1000U);
  EXPECT_FLOAT_EQ(result.points[1].pressure, 0.9F);
  EXPECT_EQ(result.points[1].timestampMicros, 2000U);
}

TEST(StrokeBuilderTest, BeginResetsTheBuilder) {
  stroke::StrokeBuilder builder(2.0F);
  builder.begin(makeSample({0, 0}, 1000));
  ASSERT_TRUE(builder.append(makeSample({10, 0}, 2000)).accepted);
  ASSERT_TRUE(builder.append(makeSample({20, 0}, 3000, 0.5F, true)).accepted);

  builder.begin(makeSample({100, 100}, 4000));
  ASSERT_TRUE(builder.append(makeSample({101, 100}, 5000)).accepted);
  const auto result = builder.finish();

  ASSERT_EQ(result.points.size(), 2U);
  EXPECT_EQ(result.points[0].position, (core::Vec2{100, 100}));
  EXPECT_EQ(result.points[1].position, (core::Vec2{101, 100}));
}
