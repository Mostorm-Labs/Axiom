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
  lastDrawnChunkCount_ = 0;
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
    if (stroke.points.empty() ||
        (stroke.coordinateSpace ==
             document::StrokeCoordinateSpace::ParentNormalized &&
         attachedParent == nullptr)) {
      continue;
    }

    const auto parentBounds =
        attachedParent ? attachedParent->bounds : core::Rect{};
    const core::Rect conservativeBounds =
        stroke.coordinateSpace ==
                    document::StrokeCoordinateSpace::ParentNormalized
            ? parentBounds
            : node.bounds;
    if (dirtyBounds && conservativeBounds.width > 0.0F &&
        conservativeBounds.height > 0.0F &&
        !conservativeBounds.intersects(*dirtyBounds)) {
      continue;
    }
    float drawWidth = stroke.width;
    if (stroke.coordinateSpace ==
        document::StrokeCoordinateSpace::ParentNormalized) {
      try {
        drawWidth = document::fromParentRelativeWidth(stroke.width,
                                                      parentBounds);
      } catch (const std::domain_error&) {
        continue;
      }
    }
    const auto mapPoint = [&](core::Vec2 point) {
      return stroke.coordinateSpace ==
                     document::StrokeCoordinateSpace::ParentNormalized
                 ? document::fromParentNormalized(point, parentBounds)
                 : point;
    };
    const auto lastPoint = stroke.points.back().position;
    const auto firstPoint = stroke.points.front().position;
    auto& cached = pathCache_[node.id];
    const auto pointBounds = [drawWidth](core::Vec2 point) {
      constexpr float kAntialiasMargin = 1.0F;
      return core::Rect{point.x, point.y, 0, 0}.inflated(
          drawWidth * 0.5F + kAntialiasMargin);
    };
    const auto startChunk = [&](core::Vec2 point) {
      cached.chunks.emplace_back();
      auto& chunk = cached.chunks.back();
      chunk.path.moveTo(point.x, point.y);
      chunk.bounds = pointBounds(point);
    };
    const auto appendPoint = [&](core::Vec2 previous, core::Vec2 point) {
      if (cached.chunks.empty() ||
          cached.chunks.back().segmentCount >= kMaximumSegmentsPerChunk) {
        startChunk(previous);
      }
      auto& chunk = cached.chunks.back();
      chunk.path.lineTo(point.x, point.y);
      chunk.bounds = chunk.bounds.united(pointBounds(point));
      ++chunk.segmentCount;
      cached.geometryBounds = cached.geometryBounds.united(pointBounds(point));
    };
    const bool identityValid = cached.documentId == document.instanceId() &&
        cached.nodeIdentity == node.cacheIdentity;
    const bool metadataValid = identityValid &&
        cached.nonAppendRevision == node.nonAppendRevision &&
        cached.width == stroke.width && cached.drawWidth == drawWidth &&
        cached.colorArgb == stroke.colorArgb &&
        cached.coordinateSpace == stroke.coordinateSpace &&
        cached.parentBounds == parentBounds && cached.firstPoint == firstPoint;
    try {
      if (metadataValid && stroke.points.size() > cached.pointCount) {
        for (std::size_t index = cached.pointCount;
             index < stroke.points.size(); ++index) {
          const auto previous = mapPoint(stroke.points[index - 1].position);
          const auto point = mapPoint(stroke.points[index].position);
          appendPoint(previous, point);
        }
        cached.pointCount = stroke.points.size();
        cached.nodeRevision = node.revision;
        cached.lastPoint = lastPoint;
        ++incrementalAppendCount_;
      } else if (!(metadataValid && cached.nodeRevision == node.revision &&
                   cached.pointCount == stroke.points.size() &&
                   cached.lastPoint == lastPoint)) {
        cached.chunks.clear();
        const auto first = mapPoint(stroke.points.front().position);
        startChunk(first);
        cached.geometryBounds = pointBounds(first);
        for (std::size_t index = 1; index < stroke.points.size(); ++index) {
          const auto previous = mapPoint(stroke.points[index - 1].position);
          const auto point = mapPoint(stroke.points[index].position);
          appendPoint(previous, point);
        }
        cached.documentId = document.instanceId();
        cached.nodeIdentity = node.cacheIdentity;
        cached.nodeRevision = node.revision;
        cached.nonAppendRevision = node.nonAppendRevision;
        cached.pointCount = stroke.points.size();
        cached.firstPoint = firstPoint;
        cached.lastPoint = lastPoint;
        cached.width = stroke.width;
        cached.drawWidth = drawWidth;
        cached.colorArgb = stroke.colorArgb;
        cached.coordinateSpace = stroke.coordinateSpace;
        cached.parentBounds = parentBounds;
        ++fullPathBuildCount_;
      }
    } catch (const std::domain_error&) {
      pathCache_.erase(node.id);
      continue;
    }

    if (dirtyBounds && cached.geometryBounds.width > 0.0F &&
        cached.geometryBounds.height > 0.0F &&
        !cached.geometryBounds.intersects(*dirtyBounds)) {
      continue;
    }

    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setStyle(SkPaint::kStroke_Style);
    paint.setStrokeCap(SkPaint::kRound_Cap);
    paint.setStrokeJoin(SkPaint::kRound_Join);
    paint.setStrokeWidth(cached.drawWidth);
    paint.setColor(static_cast<SkColor>(stroke.colorArgb));
    for (const auto& chunk : cached.chunks) {
      if (dirtyBounds && !chunk.bounds.intersects(*dirtyBounds)) continue;
      canvas.drawPath(chunk.path, paint);
      ++lastDrawnChunkCount_;
    }
  }

  canvas.restore();
}

}  // namespace canvas::render
