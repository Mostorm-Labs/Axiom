#ifndef CANVAS_POC02_INK_SKIA_RENDERER_H_
#define CANVAS_POC02_INK_SKIA_RENDERER_H_

#include <cstdint>
#include <span>
#include <vector>

#include "canvas_poc02/ink_engine.h"

class SkCanvas;
class SkSurface;

namespace canvas::poc02 {

class InkSkiaRenderer {
 public:
  // Compositor-facing entry points. These never clear the destination and are
  // shared by POC-02's standalone shell and POC-03's Ink pass.
  void DrawStroke(SkCanvas& canvas, const Stroke& stroke) const;
  void DrawPreview(SkCanvas& canvas,
                   const DefaultPreviewSink::State& preview) const;
  void Draw(SkCanvas& canvas, uint32_t width, uint32_t height,
            const StrokeDocument& document,
            const DefaultPreviewSink::State* active_preview = nullptr) const;
  bool RenderRaster(uint32_t width, uint32_t height,
                    const StrokeDocument& document,
                    const DefaultPreviewSink::State* active_preview,
                    std::vector<uint8_t>* rgba) const;
  bool Readback(SkSurface& surface, uint32_t width, uint32_t height,
                std::vector<uint8_t>* rgba) const;
};

struct PixelMetrics {
  uint64_t total_pixels = 0;
  uint64_t matching_pixels = 0;
  uint8_t maximum_channel_delta = 0;
};

PixelMetrics CompareRgba(std::span<const uint8_t> expected,
                         std::span<const uint8_t> actual,
                         uint8_t tolerance);

}  // namespace canvas::poc02

#endif  // CANVAS_POC02_INK_SKIA_RENDERER_H_
