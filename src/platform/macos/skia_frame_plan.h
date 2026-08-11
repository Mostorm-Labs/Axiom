#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <limits>

#include "canvas/document/node.h"

namespace canvas::macos {

struct DrawablePixelSize {
  int width = 1;
  int height = 1;

  friend constexpr bool operator==(DrawablePixelSize left,
                                   DrawablePixelSize right) noexcept {
    return left.width == right.width && left.height == right.height;
  }
  friend constexpr bool operator!=(DrawablePixelSize left,
                                   DrawablePixelSize right) noexcept {
    return !(left == right);
  }
};

inline double sanitizedBackingScale(double backingScale) noexcept {
  return std::isfinite(backingScale) && backingScale >= 1.0 ? backingScale
                                                            : 1.0;
}

inline int drawablePixelExtent(double extentInPoints,
                               double backingScale) noexcept {
  if (!std::isfinite(extentInPoints) || extentInPoints <= 0.0) return 1;

  const long double pixels =
      static_cast<long double>(extentInPoints) *
      static_cast<long double>(sanitizedBackingScale(backingScale));
  constexpr int maximum = std::numeric_limits<int>::max();
  if (pixels >= static_cast<long double>(maximum)) return maximum;
  return static_cast<int>(std::max<long double>(1.0L, std::round(pixels)));
}

inline DrawablePixelSize drawablePixelSize(double widthInPoints,
                                           double heightInPoints,
                                           double backingScale) noexcept {
  return {drawablePixelExtent(widthInPoints, backingScale),
          drawablePixelExtent(heightInPoints, backingScale)};
}

// A CanvasCompositionView always owns two independent Metal surfaces. The
// opaque surface paints the document background below embedded AppKit views;
// the transparent surface paints ink and interaction chrome above them.
enum class MetalSurfaceRole : std::uint8_t {
  Base,
  Overlay,
};

struct SkiaSurfaceFramePlan {
  bool opaque = false;
  std::uint32_t clearColorArgb = 0;
  std::array<document::LayerClass, 2> layers{};
  std::size_t layerCount = 0;

  constexpr bool paints(document::LayerClass layer) const noexcept {
    for (std::size_t index = 0; index < layerCount; ++index) {
      if (layers[index] == layer) return true;
    }
    return false;
  }

  friend constexpr bool operator==(const SkiaSurfaceFramePlan& left,
                                   const SkiaSurfaceFramePlan& right) noexcept {
    if (left.opaque != right.opaque ||
        left.clearColorArgb != right.clearColorArgb ||
        left.layerCount != right.layerCount) {
      return false;
    }
    for (std::size_t index = 0; index < left.layers.size(); ++index) {
      if (left.layers[index] != right.layers[index]) return false;
    }
    return true;
  }

  friend constexpr bool operator!=(const SkiaSurfaceFramePlan& left,
                                   const SkiaSurfaceFramePlan& right) noexcept {
    return !(left == right);
  }
};

inline constexpr SkiaSurfaceFramePlan skiaSurfaceFramePlan(
    MetalSurfaceRole role) noexcept {
  switch (role) {
    case MetalSurfaceRole::Base:
      return {true,
              0xFFFFFFFFU,
              {document::LayerClass::Base, document::LayerClass::Base},
              1};
    case MetalSurfaceRole::Overlay:
      return {false,
              0x00000000U,
              {document::LayerClass::Annotation,
               document::LayerClass::Chrome},
              2};
  }
  return {};
}

enum class CanvasHostLayerSlot : std::uint8_t {
  BaseMetal,
  EmbeddedViews,
  OverlayMetal,
};

inline constexpr std::array<CanvasHostLayerSlot, 3> kCanvasHostLayerOrder{
    CanvasHostLayerSlot::BaseMetal,
    CanvasHostLayerSlot::EmbeddedViews,
    CanvasHostLayerSlot::OverlayMetal,
};

}  // namespace canvas::macos
