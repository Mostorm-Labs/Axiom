#include "canvas/render/skia_renderer.h"

#include "canvas/document/embedded_transform.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkRect.h"
#include "include/core/SkSurface.h"

#include <cstddef>
#include <stdexcept>
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
  if (cachedDocumentId_ != document.instanceId() ||
      cachedDocumentGeneration_ != document.generation()) {
    pathCache_.clear();
    cachedDocumentId_ = document.instanceId();
    cachedDocumentGeneration_ = document.generation();
  }
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
    const document::Node* attachedParent =
        node.parentId ? document.find(*node.parentId) : nullptr;
    const core::Rect cullBounds =
        stroke.coordinateSpace ==
                    document::StrokeCoordinateSpace::ParentNormalized &&
                attachedParent != nullptr
            ? attachedParent->bounds
            : node.bounds;
    if (dirtyBounds && cullBounds.width > 0.0F &&
        cullBounds.height > 0.0F && !cullBounds.intersects(*dirtyBounds)) {
      continue;
    }
    const document::StrokeNode* drawable = &stroke;
    document::StrokeNode resolved;
    if (stroke.coordinateSpace ==
        document::StrokeCoordinateSpace::ParentNormalized) {
      if (!node.parentId) {
        continue;
      }
      if (attachedParent == nullptr) {
        continue;
      }
      try {
        resolved =
            document::resolveAttachedStroke(stroke, attachedParent->bounds);
      } catch (const std::domain_error&) {
        continue;
      }
      drawable = &resolved;
    }

    if (drawable->points.empty()) {
      continue;
    }

    const auto parentBounds =
        attachedParent ? attachedParent->bounds : core::Rect{};
    const auto lastPoint = drawable->points.back().position;
    const auto firstPoint = drawable->points.front().position;
    auto& cached = pathCache_[node.id];
    const bool identityValid = cached.documentId == document.instanceId() &&
        cached.source == &stroke;
    const bool metadataValid = identityValid &&
        cached.nonAppendRevision == node.nonAppendRevision &&
        cached.width == drawable->width &&
        cached.colorArgb == drawable->colorArgb &&
        cached.coordinateSpace == drawable->coordinateSpace &&
        cached.parentBounds == parentBounds && cached.firstPoint == firstPoint;
    if (metadataValid && drawable->points.size() > cached.pointCount) {
      for (std::size_t index = cached.pointCount;
           index < drawable->points.size(); ++index) {
        cached.path.lineTo(drawable->points[index].position.x,
                           drawable->points[index].position.y);
      }
      cached.pointCount = drawable->points.size();
      cached.source = &stroke;
      cached.nodeRevision = node.revision;
      cached.nonAppendRevision = node.nonAppendRevision;
      cached.lastPoint = lastPoint;
      ++incrementalAppendCount_;
    } else if (!(metadataValid && cached.nodeRevision == node.revision &&
                 cached.pointCount == drawable->points.size() &&
                 cached.lastPoint == lastPoint)) {
      cached.path.reset();
      cached.path.moveTo(drawable->points.front().position.x,
                         drawable->points.front().position.y);
      for (std::size_t index = 1; index < drawable->points.size(); ++index) {
        cached.path.lineTo(drawable->points[index].position.x,
                           drawable->points[index].position.y);
      }
      cached.source = &stroke;
      cached.documentId = document.instanceId();
      cached.nodeRevision = node.revision;
      cached.nonAppendRevision = node.nonAppendRevision;
      cached.pointCount = drawable->points.size();
      cached.firstPoint = firstPoint;
      cached.lastPoint = lastPoint;
      cached.width = drawable->width;
      cached.colorArgb = drawable->colorArgb;
      cached.coordinateSpace = drawable->coordinateSpace;
      cached.parentBounds = parentBounds;
      ++fullPathBuildCount_;
    }

    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setStyle(SkPaint::kStroke_Style);
    paint.setStrokeCap(SkPaint::kRound_Cap);
    paint.setStrokeJoin(SkPaint::kRound_Join);
    paint.setStrokeWidth(drawable->width);
    paint.setColor(static_cast<SkColor>(drawable->colorArgb));
    canvas.drawPath(cached.path, paint);
  }

  canvas.restore();
}

}  // namespace canvas::render
