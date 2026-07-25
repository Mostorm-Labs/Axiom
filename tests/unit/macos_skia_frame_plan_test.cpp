#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <limits>

#include "platform/macos/skia_frame_plan.h"

namespace canvas::macos {
namespace {

TEST(MacosSkiaFramePlan, ConvertsRetinaPointsToRoundedPhysicalPixels) {
  EXPECT_EQ(drawablePixelSize(640.25, 360.0, 2.0),
            (DrawablePixelSize{1281, 720}));
  EXPECT_EQ(drawablePixelSize(100.2, 50.2, 1.5), (DrawablePixelSize{150, 75}));
}

TEST(MacosSkiaFramePlan, ClampsInvalidDimensionsAndSubunitScaleSafely) {
  EXPECT_DOUBLE_EQ(sanitizedBackingScale(2.0), 2.0);
  EXPECT_DOUBLE_EQ(sanitizedBackingScale(0.5), 1.0);
  EXPECT_DOUBLE_EQ(
      sanitizedBackingScale(std::numeric_limits<double>::quiet_NaN()), 1.0);
  EXPECT_EQ(drawablePixelSize(42.8, 24.2, 0.5), (DrawablePixelSize{43, 24}));
  EXPECT_EQ(drawablePixelSize(0.0, -10.0, 2.0), (DrawablePixelSize{1, 1}));
  EXPECT_EQ(drawablePixelSize(std::numeric_limits<double>::quiet_NaN(),
                              std::numeric_limits<double>::infinity(),
                              std::numeric_limits<double>::quiet_NaN()),
            (DrawablePixelSize{1, 1}));
}

TEST(MacosSkiaFramePlan, ClampsFinitePixelOverflowToTheIntegerLimit) {
  EXPECT_EQ(drawablePixelSize(std::numeric_limits<double>::max(), 1.0, 2.0),
            (DrawablePixelSize{std::numeric_limits<int>::max(), 2}));
}

TEST(MacosSkiaFramePlan, DrawsOnlyNativePaintLayersInCompositionOrder) {
  constexpr std::array expected{
      document::LayerClass::Base,
      document::LayerClass::Annotation,
      document::LayerClass::Chrome,
  };
  static_assert(kCanvasPaintLayers[0] == document::LayerClass::Base);
  static_assert(kCanvasPaintLayers[1] == document::LayerClass::Annotation);
  static_assert(kCanvasPaintLayers[2] == document::LayerClass::Chrome);
  EXPECT_EQ(kCanvasPaintLayers, expected);
}

}  // namespace
}  // namespace canvas::macos
