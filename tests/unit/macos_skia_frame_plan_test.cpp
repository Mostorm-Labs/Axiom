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

TEST(MacosSkiaFramePlan, SplitsBaseAndOverlayWithoutPaintingEmbeddedContent) {
  constexpr SkiaSurfaceFramePlan base =
      skiaSurfaceFramePlan(MetalSurfaceRole::Base);
  constexpr SkiaSurfaceFramePlan overlay =
      skiaSurfaceFramePlan(MetalSurfaceRole::Overlay);

  static_assert(base.opaque);
  static_assert(base.clearColorArgb == 0xFFFFFFFFU);
  static_assert(base.layerCount == 1);
  static_assert(base.layers[0] == document::LayerClass::Base);
  static_assert(!base.paints(document::LayerClass::Embedded));
  static_assert(!base.paints(document::LayerClass::Annotation));
  static_assert(!base.paints(document::LayerClass::Chrome));

  static_assert(!overlay.opaque);
  static_assert(overlay.clearColorArgb == 0x00000000U);
  static_assert(overlay.layerCount == 2);
  static_assert(overlay.layers[0] == document::LayerClass::Annotation);
  static_assert(overlay.layers[1] == document::LayerClass::Chrome);
  static_assert(!overlay.paints(document::LayerClass::Base));
  static_assert(!overlay.paints(document::LayerClass::Embedded));

  EXPECT_NE(base, overlay);
}

TEST(MacosSkiaFramePlan, DeclaresTheFixedBackToFrontNativeHostStack) {
  constexpr std::array expected{
      CanvasHostLayerSlot::BaseMetal,
      CanvasHostLayerSlot::EmbeddedViews,
      CanvasHostLayerSlot::OverlayMetal,
  };
  static_assert(kCanvasHostLayerOrder[0] == CanvasHostLayerSlot::BaseMetal);
  static_assert(kCanvasHostLayerOrder[1] ==
                CanvasHostLayerSlot::EmbeddedViews);
  static_assert(kCanvasHostLayerOrder[2] == CanvasHostLayerSlot::OverlayMetal);
  EXPECT_EQ(kCanvasHostLayerOrder, expected);
}

}  // namespace
}  // namespace canvas::macos
