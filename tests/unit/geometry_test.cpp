#include "canvas/core/geometry.h"
#include "canvas/input/pointer_sample.h"

#include <gtest/gtest.h>

using canvas::core::Rect;
using canvas::core::Transform2D;
using canvas::core::Vec2;

TEST(GeometryTest, UnionsAndInflatesRects) {
  const Rect result = Rect::fromPoints({10, 20}, {30, 40})
                          .united(Rect::fromPoints({0, 25}, {15, 50}))
                          .inflated(2.0F);
  EXPECT_EQ(result, (Rect{-2, 18, 34, 34}));
}

TEST(GeometryTest, ConvertsBetweenWorldAndScreen) {
  const Transform2D transform{2.0F, 3.0F, 100.0F, 50.0F};
  const Vec2 screen = transform.map({4.0F, 5.0F});
  EXPECT_EQ(screen, (Vec2{108.0F, 65.0F}));
  EXPECT_EQ(transform.inverse().map(screen), (Vec2{4.0F, 5.0F}));
}

TEST(GeometryTest, HandlesReversedPoints) {
  EXPECT_EQ(Rect::fromPoints({30, 40}, {10, 20}), (Rect{10, 20, 20, 20}));
}

TEST(GeometryTest, ContainsIncludesBoundary) {
  const Rect rect{10, 20, 30, 40};
  EXPECT_TRUE(rect.contains({10, 20}));
  EXPECT_TRUE(rect.contains({40, 60}));
  EXPECT_FALSE(rect.contains({40.001F, 60}));
}

TEST(GeometryTest, RejectsNonInvertibleTransforms) {
  EXPECT_THROW((Transform2D{0.0F, 1.0F, 0.0F, 0.0F}).inverse(),
               std::domain_error);
  EXPECT_THROW((Transform2D{1.0e-7F, 1.0F, 0.0F, 0.0F}).inverse(),
               std::domain_error);
}

TEST(PointerSampleTest, DefaultsAndEnumsAreUsable) {
  const canvas::input::PointerSample sample;
  EXPECT_EQ(sample.pointerId, 0U);
  EXPECT_EQ(sample.timestampMicros, 0U);
  EXPECT_EQ(sample.screenPosition, (Vec2{0.0F, 0.0F}));
  EXPECT_FLOAT_EQ(sample.pressure, 0.5F);
  EXPECT_FLOAT_EQ(sample.tiltXDegrees, 0.0F);
  EXPECT_FLOAT_EQ(sample.tiltYDegrees, 0.0F);
  EXPECT_EQ(sample.kind, canvas::input::PointerKind::Pen);
  EXPECT_EQ(sample.phase, canvas::input::PointerPhase::Move);
  EXPECT_FALSE(sample.predicted);
}
