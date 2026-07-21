#include "canvas/document/embedded_transform.h"

#include <gtest/gtest.h>

using namespace canvas;

TEST(EmbeddedTransformTest, NormalizesAndRestoresAttachedPoints) {
  const core::Rect original{100, 200, 400, 200};
  const core::Vec2 local =
      document::toParentNormalized({300, 250}, original);
  EXPECT_EQ(local, (core::Vec2{0.5F, 0.25F}));
  const core::Rect movedAndResized{20, 40, 800, 400};
  EXPECT_EQ(document::fromParentNormalized(local, movedAndResized),
            (core::Vec2{420, 140}));
}

TEST(EmbeddedTransformTest, ScalesAttachedStrokeWidthWithParent) {
  EXPECT_FLOAT_EQ(
      document::fromParentRelativeWidth(0.01F, {0, 0, 800, 400}), 4.0F);
}

TEST(EmbeddedTransformTest, AttachAndResolveStrokePreservesRelativeGeometry) {
  document::StrokeNode stroke;
  stroke.points = {{{200, 250}, 1.0F, 1}, {{400, 300}, 1.0F, 2}};
  stroke.width = 8.0F;

  const core::Rect parent{100, 200, 400, 200};
  const auto attached = document::attachStrokeToParent(stroke, parent);
  EXPECT_EQ(attached.coordinateSpace,
            document::StrokeCoordinateSpace::ParentNormalized);
  EXPECT_EQ(attached.points[0].position, (core::Vec2{0.25F, 0.25F}));
  EXPECT_FLOAT_EQ(attached.width, 0.04F);

  const auto resolved = document::resolveAttachedStroke(
      attached, core::Rect{0, 0, 800, 400});
  EXPECT_EQ(resolved.coordinateSpace, document::StrokeCoordinateSpace::World);
  EXPECT_EQ(resolved.points[0].position, (core::Vec2{200, 100}));
  EXPECT_FLOAT_EQ(resolved.width, 16.0F);
}

TEST(EmbeddedTransformTest, RejectsInvalidParentBounds) {
  EXPECT_THROW(document::toParentNormalized({1, 1}, {0, 0, 0, 1}),
               std::domain_error);
  EXPECT_THROW(document::toParentNormalized({1, 1}, {0, 0, 1, -1}),
               std::domain_error);
}
