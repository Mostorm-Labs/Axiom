#pragma once

#include "canvas/render/renderer.h"

#include <optional>

class SkCanvas;

namespace canvas::render {

class SkiaRenderer final : public Renderer {
 public:
  RasterFrame renderRaster(const document::Document& document,
                           document::LayerClass layer, int width,
                           int height) override;

  void drawLayer(
      SkCanvas& canvas, const document::Document& document,
      document::LayerClass layer,
      const std::optional<core::Rect>& dirtyBounds = std::nullopt);
};

}  // namespace canvas::render
