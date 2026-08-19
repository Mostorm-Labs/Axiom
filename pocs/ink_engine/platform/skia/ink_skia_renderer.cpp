#include "ink_skia_renderer.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPaint.h"
#include "include/core/SkSurface.h"

namespace canvas::poc02 {
namespace {

SkImageInfo RgbaInfo(uint32_t width, uint32_t height) {
  return SkImageInfo::Make(static_cast<int>(width), static_cast<int>(height),
                           kRGBA_8888_SkColorType, kPremul_SkAlphaType,
                           SkColorSpace::MakeSRGB());
}

SkColor StrokeColor(const BrushDescriptor& brush, bool preview) {
  const uint8_t alpha = static_cast<uint8_t>(
      std::clamp(brush.opacity * (preview ? 0.78F : 1.0F), 0.0F, 1.0F) * 255.0F);
  return brush.type == BrushType::kVector
             ? SkColorSetARGB(alpha, 31, 91, 210)
             : SkColorSetARGB(alpha, 116, 55, 189);
}

void DrawVector(SkCanvas& canvas, std::span<const VectorPoint> points,
                SkColor color) {
  if (points.empty()) return;
  // POC goldens compare raster, WebGL2, D3D12 and GLES. Keep coverage binary
  // so backend AA kernels do not hide a semantic geometry regression.
  SkPaint fill;
  fill.setAntiAlias(false);
  fill.setColor(color);
  fill.setStyle(SkPaint::kFill_Style);
  const auto draw_disc = [&canvas, &fill](const VectorPoint& point) {
    const int32_t left = static_cast<int32_t>(std::floor(
        static_cast<double>(point.position.x) - point.radius));
    const int32_t top = static_cast<int32_t>(std::floor(
        static_cast<double>(point.position.y) - point.radius));
    const int32_t right = static_cast<int32_t>(std::ceil(
        static_cast<double>(point.position.x) + point.radius));
    const int32_t bottom = static_cast<int32_t>(std::ceil(
        static_cast<double>(point.position.y) + point.radius));
    const double radius_squared = static_cast<double>(point.radius) *
                                  point.radius;
    for (int32_t y = top; y < bottom; ++y) {
      int32_t run_start = right;
      int32_t run_end = left;
      for (int32_t x = left; x < right; ++x) {
        const double dx = static_cast<double>(x) + 0.5 - point.position.x;
        const double dy = static_cast<double>(y) + 0.5 - point.position.y;
        if (dx * dx + dy * dy <= radius_squared) {
          run_start = std::min(run_start, x);
          run_end = std::max(run_end, x + 1);
        }
      }
      if (run_start < run_end) {
        canvas.drawIRect(SkIRect::MakeLTRB(run_start, y, run_end, y + 1), fill);
      }
    }
  };
  draw_disc(points.front());
  for (size_t index = 1; index < points.size(); ++index) {
    const VectorPoint& prior = points[index - 1];
    const VectorPoint& current = points[index];
    const double dx = static_cast<double>(current.position.x) -
                      prior.position.x;
    const double dy = static_cast<double>(current.position.y) -
                      prior.position.y;
    const double length_squared = dx * dx + dy * dy;
    const float maximum_radius = std::max(prior.radius, current.radius);
    const int32_t left = static_cast<int32_t>(std::floor(
        std::min(prior.position.x, current.position.x) - maximum_radius));
    const int32_t top = static_cast<int32_t>(std::floor(
        std::min(prior.position.y, current.position.y) - maximum_radius));
    const int32_t right = static_cast<int32_t>(std::ceil(
        std::max(prior.position.x, current.position.x) + maximum_radius));
    const int32_t bottom = static_cast<int32_t>(std::ceil(
        std::max(prior.position.y, current.position.y) + maximum_radius));
    for (int32_t y = top; y < bottom; ++y) {
      int32_t run_start = right;
      int32_t run_end = left;
      for (int32_t x = left; x < right; ++x) {
        const double px = static_cast<double>(x) + 0.5;
        const double py = static_cast<double>(y) + 0.5;
        const double projection = length_squared == 0.0
            ? 0.0
            : std::clamp(((px - prior.position.x) * dx +
                          (py - prior.position.y) * dy) /
                             length_squared,
                         0.0, 1.0);
        const double center_x = prior.position.x + dx * projection;
        const double center_y = prior.position.y + dy * projection;
        const double radius = static_cast<double>(prior.radius) +
            (static_cast<double>(current.radius) - prior.radius) * projection;
        const double distance_x = px - center_x;
        const double distance_y = py - center_y;
        if (distance_x * distance_x + distance_y * distance_y <=
            radius * radius) {
          run_start = std::min(run_start, x);
          run_end = std::max(run_end, x + 1);
        }
      }
      if (run_start < run_end) {
        canvas.drawIRect(SkIRect::MakeLTRB(run_start, y, run_end, y + 1), fill);
      }
    }
    draw_disc(current);
  }
}

void DrawDabs(SkCanvas& canvas, std::span<const Dab> dabs, SkColor color) {
  SkPaint paint;
  paint.setAntiAlias(false);
  paint.setStyle(SkPaint::kFill_Style);
  for (const Dab& dab : dabs) {
    paint.setColor(SkColorSetA(color, static_cast<uint8_t>(
        static_cast<float>(SkColorGetA(color)) * dab.opacity)));
    const double radians = static_cast<double>(dab.rotation_degrees) *
                           std::numbers::pi / 180.0;
    const double cosine = std::cos(radians);
    const double sine = std::sin(radians);
    const double radius_x = dab.radius;
    const double radius_y = static_cast<double>(dab.radius) * 0.65;
    const double extent_x = std::sqrt(radius_x * radius_x * cosine * cosine +
                                      radius_y * radius_y * sine * sine);
    const double extent_y = std::sqrt(radius_x * radius_x * sine * sine +
                                      radius_y * radius_y * cosine * cosine);
    const int32_t left = static_cast<int32_t>(
        std::floor(static_cast<double>(dab.position.x) - extent_x));
    const int32_t top = static_cast<int32_t>(
        std::floor(static_cast<double>(dab.position.y) - extent_y));
    const int32_t right = static_cast<int32_t>(
        std::ceil(static_cast<double>(dab.position.x) + extent_x));
    const int32_t bottom = static_cast<int32_t>(
        std::ceil(static_cast<double>(dab.position.y) + extent_y));
    for (int32_t y = top; y < bottom; ++y) {
      int32_t run_start = right;
      int32_t run_end = left;
      for (int32_t x = left; x < right; ++x) {
        const double dx = static_cast<double>(x) + 0.5 - dab.position.x;
        const double dy = static_cast<double>(y) + 0.5 - dab.position.y;
        const double local_x = cosine * dx + sine * dy;
        const double local_y = -sine * dx + cosine * dy;
        const double normalized = local_x * local_x / (radius_x * radius_x) +
                                  local_y * local_y / (radius_y * radius_y);
        if (normalized <= 1.0) {
          run_start = std::min(run_start, x);
          run_end = std::max(run_end, x + 1);
        }
      }
      if (run_start < run_end) {
        canvas.drawIRect(SkIRect::MakeLTRB(run_start, y, run_end, y + 1),
                         paint);
      }
    }
  }
}

void DrawPreviewSemantics(SkCanvas& canvas,
                          const DefaultPreviewSink::State& preview);

void DrawDocumentSemantics(SkCanvas& canvas, uint32_t width, uint32_t height,
                           const StrokeDocument& document,
                           const DefaultPreviewSink::State* active_preview) {
  canvas.clear(SkColorSetRGB(247, 248, 250));
  SkPaint grid;
  grid.setColor(SkColorSetARGB(255, 226, 229, 235));
  grid.setStyle(SkPaint::kFill_Style);
  grid.setAntiAlias(false);
  for (uint32_t x = 0; x < width; x += 40) {
    canvas.drawRect(SkRect::MakeXYWH(static_cast<float>(x), 0.0F, 1.0F,
                                    static_cast<float>(height)), grid);
  }
  for (uint32_t y = 0; y < height; y += 40) {
    canvas.drawRect(SkRect::MakeXYWH(0.0F, static_cast<float>(y),
                                    static_cast<float>(width), 1.0F), grid);
  }
  for (const Stroke& stroke : document.strokes()) {
    const SkColor color = StrokeColor(stroke.brush, false);
    if (stroke.brush.type == BrushType::kVector) {
      DrawVector(canvas, stroke.vector_points, color);
    } else {
      DrawDabs(canvas, stroke.dabs, color);
    }
  }
  if (active_preview != nullptr && !active_preview->committed &&
      !active_preview->visible) {
    DrawPreviewSemantics(canvas, *active_preview);
  }
}

void DrawPreviewSemantics(SkCanvas& canvas,
                          const DefaultPreviewSink::State& preview) {
  std::vector<VectorPoint> vector_points;
  std::vector<Dab> dabs;
  const auto append = [&](const auto& primitives, bool predicted) {
    for (const PreviewPrimitive& primitive : primitives) {
      const float opacity = predicted ? primitive.opacity * 0.45F
                                      : primitive.opacity;
      if (preview.brush.type == BrushType::kVector) {
        vector_points.push_back(VectorPoint{.position = primitive.position,
                                            .radius = primitive.radius});
      } else {
        dabs.push_back(Dab{.position = primitive.position,
                           .radius = primitive.radius,
                           .rotation_degrees = primitive.rotation_degrees,
                           .opacity = opacity});
      }
    }
  };
  append(preview.confirmed, false);
  append(preview.predicted, true);
  const SkColor color = StrokeColor(preview.brush, true);
  if (preview.brush.type == BrushType::kVector) DrawVector(canvas, vector_points, color);
  else DrawDabs(canvas, dabs, color);
}

}  // namespace

void InkSkiaRenderer::DrawStroke(SkCanvas& canvas,
                                 const Stroke& stroke) const {
  const SkColor color = StrokeColor(stroke.brush, false);
  if (stroke.brush.type == BrushType::kVector) {
    DrawVector(canvas, stroke.vector_points, color);
  } else {
    DrawDabs(canvas, stroke.dabs, color);
  }
}

void InkSkiaRenderer::DrawPreview(
    SkCanvas& canvas, const DefaultPreviewSink::State& preview) const {
  DrawPreviewSemantics(canvas, preview);
}

void InkSkiaRenderer::Draw(SkCanvas& canvas, uint32_t width, uint32_t height,
                           const StrokeDocument& document,
                           const DefaultPreviewSink::State* active_preview) const {
  DrawDocumentSemantics(canvas, width, height, document, active_preview);
}

bool InkSkiaRenderer::RenderRaster(
    uint32_t width, uint32_t height, const StrokeDocument& document,
    const DefaultPreviewSink::State* active_preview,
    std::vector<uint8_t>* rgba) const {
  if (rgba == nullptr || width == 0 || height == 0) return false;
  sk_sp<SkSurface> surface = SkSurfaces::Raster(RgbaInfo(width, height));
  if (surface == nullptr) return false;
  DrawDocumentSemantics(*surface->getCanvas(), width, height, document,
                        active_preview);
  return Readback(*surface, width, height, rgba);
}

bool InkSkiaRenderer::Readback(SkSurface& surface, uint32_t width,
                               uint32_t height,
                               std::vector<uint8_t>* rgba) const {
  if (rgba == nullptr || width == 0 || height == 0) return false;
  rgba->resize(static_cast<size_t>(width) * height * 4U);
  return surface.readPixels(RgbaInfo(width, height), rgba->data(),
                            static_cast<size_t>(width) * 4U, 0, 0);
}

PixelMetrics CompareRgba(std::span<const uint8_t> expected,
                         std::span<const uint8_t> actual,
                         uint8_t tolerance) {
  PixelMetrics metrics;
  if (expected.size() != actual.size() || expected.size() % 4U != 0) {
    return metrics;
  }
  metrics.total_pixels = expected.size() / 4U;
  for (size_t pixel = 0; pixel < metrics.total_pixels; ++pixel) {
    bool matches = true;
    for (size_t channel = 0; channel < 4; ++channel) {
      const size_t offset = pixel * 4U + channel;
      const int delta = std::abs(static_cast<int>(expected[offset]) -
                                 static_cast<int>(actual[offset]));
      metrics.maximum_channel_delta = static_cast<uint8_t>(std::max(
          static_cast<int>(metrics.maximum_channel_delta), delta));
      matches = matches && delta <= tolerance;
    }
    metrics.matching_pixels += matches ? 1U : 0U;
  }
  return metrics;
}

}  // namespace canvas::poc02
