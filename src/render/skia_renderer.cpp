#include "canvas/render/skia_renderer.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkRect.h"
#include "include/core/SkSurface.h"

#include <cstddef>
#include <utility>

namespace canvas::render {

RasterFrame SkiaRenderer::renderRaster(const document::Document& document,
                                       document::LayerClass layer, int width,
                                       int height) {
  RasterFrame frame{width, height, {}};
  if (width <= 0 || height <= 0) {
    return frame;
  }

  const auto imageInfo = SkImageInfo::MakeN32Premul(width, height);
  auto surface = SkSurfaces::Raster(imageInfo);
  if (surface == nullptr) {
    return frame;
  }

  SkCanvas* canvas = surface->getCanvas();
  canvas->clear(SK_ColorTRANSPARENT);
  drawLayer(*canvas, document, layer);

  frame.bgraPremultiplied.resize(static_cast<std::size_t>(width) *
                                 static_cast<std::size_t>(height));
  const bool read = surface->readPixels(
      imageInfo, frame.bgraPremultiplied.data(),
      static_cast<std::size_t>(width) * sizeof(std::uint32_t), 0, 0);
  if (!read) {
    frame.bgraPremultiplied.clear();
  }
  return frame;
}

void SkiaRenderer::drawLayer(
    SkCanvas& canvas, const document::Document& document,
    document::LayerClass layer,
    const std::optional<core::Rect>& dirtyBounds) {
  canvas.save();
  if (dirtyBounds.has_value()) {
    const auto& bounds = *dirtyBounds;
    canvas.clipRect(SkRect::MakeXYWH(bounds.x, bounds.y, bounds.width,
                                     bounds.height));
  }

  for (const auto& node : document.nodes()) {
    if (node.layer != layer ||
        !std::holds_alternative<document::StrokeNode>(node.payload)) {
      continue;
    }

    const auto& stroke = std::get<document::StrokeNode>(node.payload);
    if (stroke.points.empty()) {
      continue;
    }

    SkPath path;
    path.moveTo(stroke.points.front().position.x,
                stroke.points.front().position.y);
    for (std::size_t index = 1; index < stroke.points.size(); ++index) {
      path.lineTo(stroke.points[index].position.x,
                  stroke.points[index].position.y);
    }

    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setStyle(SkPaint::kStroke_Style);
    paint.setStrokeCap(SkPaint::kRound_Cap);
    paint.setStrokeJoin(SkPaint::kRound_Join);
    paint.setStrokeWidth(stroke.width);
    paint.setColor(static_cast<SkColor>(stroke.colorArgb));
    canvas.drawPath(path, paint);
  }

  canvas.restore();
}

}  // namespace canvas::render
