#ifndef CANVAS_POC_SKIA_SCENE_RENDERER_H_
#define CANVAS_POC_SKIA_SCENE_RENDERER_H_

#include <cstdint>
#include <span>
#include <vector>

#include "document.h"
#include "scene_compiler.h"

class SkCanvas;
class SkSurface;

namespace canvas::poc01 {

struct VisualMetrics {
  uint64_t total_pixels = 0;
  uint64_t matching_pixels = 0;
  uint8_t maximum_channel_delta = 0;
};

class SkiaSceneRenderer {
 public:
  canvas_poc_status_t Draw(SkCanvas& canvas, const RuntimeScene& scene,
                           const AssetRegistry& assets) const;
  canvas_poc_status_t RenderRaster(const RuntimeScene& scene,
                                   const AssetRegistry& assets,
                                   std::vector<uint8_t>* rgba) const;
  canvas_poc_status_t Readback(SkSurface& surface, uint32_t width,
                               uint32_t height,
                               std::vector<uint8_t>* rgba) const;
};

VisualMetrics CompareRgba(std::span<const uint8_t> expected,
                          std::span<const uint8_t> actual,
                          uint8_t tolerance);

}  // namespace canvas::poc01

#endif  // CANVAS_POC_SKIA_SCENE_RENDERER_H_
