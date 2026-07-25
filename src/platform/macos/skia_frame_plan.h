#pragma once

#include <algorithm>
#include <array>
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

// This vertical slice composites all native paint into one Metal surface in
// Windows' back-to-front order. It does not solve final embedded-view layering:
// the WKWebView increment must use an opaque Base Metal layer, WKWebViews in
// the middle, and a separate transparent Annotation/Chrome Metal layer above.
inline constexpr std::array<document::LayerClass, 3> kCanvasPaintLayers{
    document::LayerClass::Base,
    document::LayerClass::Annotation,
    document::LayerClass::Chrome,
};

}  // namespace canvas::macos
