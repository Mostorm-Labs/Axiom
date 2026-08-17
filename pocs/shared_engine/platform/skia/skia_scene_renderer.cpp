#include "skia_scene_renderer.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkData.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkImage.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkSamplingOptions.h"
#include "include/core/SkSurface.h"
#include "include/ports/SkFontMgr_data.h"

namespace canvas::poc01 {
namespace {

SkColor ToSkColor(Color color) {
  return SkColorSetARGB(color.a, color.r, color.g, color.b);
}

SkImageInfo RgbaInfo(uint32_t width, uint32_t height) {
  return SkImageInfo::Make(static_cast<int>(width), static_cast<int>(height),
                           kRGBA_8888_SkColorType, kPremul_SkAlphaType,
                           SkColorSpace::MakeSRGB());
}

}  // namespace

SkiaSceneRenderer::SkiaSceneRenderer() = default;
SkiaSceneRenderer::~SkiaSceneRenderer() = default;

canvas_poc_status_t SkiaSceneRenderer::Draw(
    SkCanvas& canvas, const RuntimeScene& scene,
    const AssetRegistry& assets) {
  canvas.clear(ToSkColor(scene.background));
  for (const Node& node : scene.draw_items) {
    canvas_poc_status_t status = CANVAS_POC_STATUS_OK;
    std::visit(
        [&](const auto& value) {
          using T = std::decay_t<decltype(value)>;
          SkPaint paint;
          paint.setAntiAlias(true);
          canvas.save();
          canvas.translate(value.header.translation_x,
                           value.header.translation_y);
          if constexpr (std::is_same_v<T, RectNode>) {
            paint.setColor(ToSkColor(value.color));
            canvas.drawRect(
                SkRect::MakeXYWH(value.x, value.y, value.width, value.height),
                paint);
          } else if constexpr (std::is_same_v<T, ImageNode>) {
            const Asset* asset = assets.Find(value.asset_key);
            if (asset == nullptr) {
              SetLastError("Skia image asset disappeared: " + value.asset_key);
              status = CANVAS_POC_STATUS_ASSET_ERROR;
            } else {
              CachedImage& cached = images_[value.asset_key];
              if (cached.image == nullptr ||
                  cached.content_hash != asset->content_hash) {
                sk_sp<SkData> data = SkData::MakeWithCopy(
                    asset->bytes.data(), asset->bytes.size());
                cached.image = SkImages::DeferredFromEncodedData(data);
                cached.content_hash = asset->content_hash;
              }
              if (cached.image == nullptr) {
                SetLastError("Skia failed to decode image asset: " +
                             value.asset_key);
                status = CANVAS_POC_STATUS_ASSET_ERROR;
              } else {
                canvas.drawImageRect(
                    cached.image,
                    SkRect::MakeXYWH(value.x, value.y, value.width,
                                     value.height),
                    SkSamplingOptions(SkFilterMode::kNearest), nullptr);
              }
            }
          } else if constexpr (std::is_same_v<T, VectorPathNode>) {
            // The POC golden compares CPU raster with four GPU backends. Keep
            // the fixed path coverage binary so backend-specific AA kernels do
            // not consume the entire ±2/99.9% visual tolerance budget.
            paint.setAntiAlias(false);
            SkPathBuilder builder;
            for (const PathCommand& command : value.commands) {
              if (command.verb == PathVerb::kMove) {
                builder.moveTo(command.points[0], command.points[1]);
              } else if (command.verb == PathVerb::kLine) {
                builder.lineTo(command.points[0], command.points[1]);
              } else if (command.verb == PathVerb::kCubic) {
                builder.cubicTo(command.points[0], command.points[1],
                                command.points[2], command.points[3],
                                command.points[4], command.points[5]);
              } else if (command.verb == PathVerb::kClose) {
                builder.close();
              }
            }
            const SkPath path = builder.detach();
            paint.setColor(ToSkColor(value.color));
            paint.setStyle(SkPaint::kStroke_Style);
            paint.setStrokeWidth(value.stroke_width);
            paint.setStrokeCap(SkPaint::kRound_Cap);
            paint.setStrokeJoin(SkPaint::kRound_Join);
            canvas.drawPath(path, paint);
          } else if constexpr (std::is_same_v<T, TextNode>) {
            paint.setColor(ToSkColor(value.color));
            const Asset* asset = assets.Find(value.font_asset_key);
            if (asset == nullptr) {
              SetLastError("Skia font asset disappeared: " +
                           value.font_asset_key);
              status = CANVAS_POC_STATUS_ASSET_ERROR;
            } else {
              CachedTypeface& cached = typefaces_[value.font_asset_key];
              if (cached.typeface == nullptr ||
                  cached.content_hash != asset->content_hash) {
                sk_sp<SkData> data = SkData::MakeWithCopy(
                    asset->bytes.data(), asset->bytes.size());
                sk_sp<SkData> fonts[] = {data};
                sk_sp<SkFontMgr> manager = SkFontMgr_New_Custom_Data(fonts);
                cached.typeface = manager->makeFromData(data);
                cached.content_hash = asset->content_hash;
              }
              if (cached.typeface == nullptr) {
                SetLastError("Skia failed to load font asset: " +
                             value.font_asset_key);
                status = CANVAS_POC_STATUS_ASSET_ERROR;
              } else {
                // The cross-backend golden is intentionally coverage-binary.
                // This removes platform FreeType/GPU AA-kernel variance while
                // still exercising the pinned Roboto Text node end to end.
                paint.setAntiAlias(false);
                SkFont font(cached.typeface, value.font_size);
                font.setEdging(SkFont::Edging::kAlias);
                font.setHinting(SkFontHinting::kNone);
                font.setLinearMetrics(true);
                font.setSubpixel(false);
                canvas.drawString(value.text.c_str(), value.x, value.y, font,
                                  paint);
              }
            }
          }
          canvas.restore();
        },
        node);
    if (status != CANVAS_POC_STATUS_OK) {
      return status;
    }
  }
  return CANVAS_POC_STATUS_OK;
}

canvas_poc_status_t SkiaSceneRenderer::RenderRaster(
    const RuntimeScene& scene, const AssetRegistry& assets,
    std::vector<uint8_t>* rgba) {
  if (rgba == nullptr) {
    SetLastError("raster output must not be null");
    return CANVAS_POC_STATUS_INVALID_ARGUMENT;
  }
  sk_sp<SkSurface> surface = SkSurfaces::Raster(
      RgbaInfo(scene.page_width, scene.page_height), 0, nullptr);
  if (surface == nullptr) {
    SetLastError("Skia failed to create raster surface");
    return CANVAS_POC_STATUS_RENDER_ERROR;
  }
  const canvas_poc_status_t status = Draw(*surface->getCanvas(), scene, assets);
  return status == CANVAS_POC_STATUS_OK
             ? Readback(*surface, scene.page_width, scene.page_height, rgba)
             : status;
}

canvas_poc_status_t SkiaSceneRenderer::Readback(
    SkSurface& surface, uint32_t width, uint32_t height,
    std::vector<uint8_t>* rgba) const {
  if (rgba == nullptr || width == 0 || height == 0) {
    SetLastError("invalid Skia readback request");
    return CANVAS_POC_STATUS_INVALID_ARGUMENT;
  }
  rgba->resize(static_cast<size_t>(width) * height * 4U);
  const SkImageInfo info = RgbaInfo(width, height);
  if (!surface.readPixels(info, rgba->data(), static_cast<size_t>(width) * 4U,
                          0, 0)) {
    SetLastError("Skia RGBA8888/sRGB readback failed");
    return CANVAS_POC_STATUS_RENDER_ERROR;
  }
  return CANVAS_POC_STATUS_OK;
}

VisualMetrics CompareRgba(std::span<const uint8_t> expected,
                          std::span<const uint8_t> actual,
                          uint8_t tolerance) {
  VisualMetrics metrics;
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
      metrics.maximum_channel_delta = static_cast<uint8_t>(
          std::max(static_cast<int>(metrics.maximum_channel_delta), delta));
      matches = matches && delta <= tolerance;
    }
    metrics.matching_pixels += matches ? 1U : 0U;
  }
  return metrics;
}

}  // namespace canvas::poc01
