#include "skia_large_scene_renderer.h"

#include <algorithm>
#include <cstddef>
#include <optional>

#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPaint.h"
#include "include/core/SkSurface.h"

namespace canvas::poc03 {
namespace {

SkColor ColorFromRgba(uint32_t rgba) {
  return SkColorSetARGB(static_cast<uint8_t>(rgba >> 24U),
                        static_cast<uint8_t>(rgba >> 16U),
                        static_cast<uint8_t>(rgba >> 8U),
                        static_cast<uint8_t>(rgba));
}

SkRect ToSkRect(const Bounds& bounds) {
  return SkRect::MakeLTRB(bounds.left, bounds.top, bounds.right,
                          bounds.bottom);
}

}  // namespace

void DrawLargeScene(SkCanvas& canvas, const RuntimeScene& scene,
                    const ViewState& view, const FrameGraph& frame) {
  canvas.clear(SkColorSetARGB(255U, 244U, 245U, 247U));
  canvas.save();
  canvas.scale(view.zoom * view.dpr, view.zoom * view.dpr);
  canvas.translate(-view.world_viewport.left, -view.world_viewport.top);
  SkPaint paint;
  paint.setAntiAlias(false);
  for (const uint64_t id : ComposeSceneDrawList(frame)) {
    const auto slot = scene.SlotFor(id);
    const auto record = slot ? scene.RecordAt(*slot) : std::nullopt;
    if (!record) {
      continue;
    }
    paint.setColor(ColorFromRgba(record->rgba));
    paint.setStrokeWidth(2.0F / std::max(view.zoom * view.dpr, 0.01F));
    if (record->type == NodeType::kShape) {
      paint.setStyle(SkPaint::kFill_Style);
      canvas.drawRect(ToSkRect(record->bounds), paint);
    } else if (record->type == NodeType::kImage) {
      paint.setStyle(SkPaint::kFill_Style);
      canvas.drawRect(ToSkRect(record->bounds), paint);
      paint.setColor(SK_ColorWHITE);
      paint.setStyle(SkPaint::kStroke_Style);
      canvas.drawLine(record->bounds.left, record->bounds.top,
                      record->bounds.right, record->bounds.bottom, paint);
    } else if (record->type == NodeType::kVectorPath) {
      paint.setStyle(SkPaint::kStroke_Style);
      canvas.drawLine(record->bounds.left, record->bounds.bottom,
                      record->bounds.right, record->bounds.top, paint);
    } else if (record->type == NodeType::kSimpleText) {
      // POC-03 owns a read-only text render record, not shaping/editing. This
      // deterministic glyph-like mark exercises the render path without
      // stealing POC-04's SkParagraph/IME decisions.
      paint.setStyle(SkPaint::kStroke_Style);
      canvas.drawRect(ToSkRect(record->bounds), paint);
      const float middle = (record->bounds.left + record->bounds.right) * 0.5F;
      canvas.drawLine(middle, record->bounds.top, middle,
                      record->bounds.bottom, paint);
    } else {
      paint.setStyle(SkPaint::kStroke_Style);
      paint.setStrokeCap(SkPaint::kRound_Cap);
      canvas.drawLine(record->bounds.left, record->bounds.top,
                      record->bounds.right, record->bounds.bottom, paint);
    }
  }
  canvas.restore();
}

bool ReadRgba(SkSurface& surface, uint32_t width, uint32_t height,
              std::vector<uint8_t>* rgba) {
  if (rgba == nullptr || width == 0U || height == 0U) {
    return false;
  }
  rgba->resize(static_cast<size_t>(width) * height * 4U);
  const SkImageInfo info = SkImageInfo::Make(
      static_cast<int>(width), static_cast<int>(height),
      kRGBA_8888_SkColorType, kPremul_SkAlphaType,
      SkColorSpace::MakeSRGB());
  return surface.readPixels(info, rgba->data(),
                            static_cast<size_t>(width) * 4U, 0, 0);
}

std::string PixelDigest(std::span<const uint8_t> rgba) {
  return CanonicalDigest(rgba);
}

}  // namespace canvas::poc03
