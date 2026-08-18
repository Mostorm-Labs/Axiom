#ifndef CANVAS_POC_SKIA_SCENE_RENDERER_H_
#define CANVAS_POC_SKIA_SCENE_RENDERER_H_

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>

#include "document.h"
#include "include/core/SkRefCnt.h"
#include "scene_compiler.h"

class SkCanvas;
class SkImage;
class SkSurface;
class SkTypeface;

namespace canvas::poc01 {

struct VisualMetrics {
  uint64_t total_pixels = 0;
  uint64_t matching_pixels = 0;
  uint8_t maximum_channel_delta = 0;
};

class SkiaSceneRenderer {
 public:
  SkiaSceneRenderer();
  ~SkiaSceneRenderer();
  SkiaSceneRenderer(const SkiaSceneRenderer&) = delete;
  SkiaSceneRenderer& operator=(const SkiaSceneRenderer&) = delete;

  canvas_poc_status_t Draw(SkCanvas& canvas, const RuntimeScene& scene,
                           const AssetRegistry& assets);
  canvas_poc_status_t RenderRaster(const RuntimeScene& scene,
                                   const AssetRegistry& assets,
                                   std::vector<uint8_t>* rgba);
  canvas_poc_status_t Readback(SkSurface& surface, uint32_t width,
                               uint32_t height,
                               std::vector<uint8_t>* rgba) const;
  void ResetCaches();

 private:
  struct CachedImage {
    Hash128 content_hash;
    sk_sp<SkImage> image;
  };
  struct CachedTypeface {
    Hash128 content_hash;
    sk_sp<SkTypeface> typeface;
  };
  std::map<std::string, CachedImage, std::less<>> images_;
  std::map<std::string, CachedTypeface, std::less<>> typefaces_;
};

VisualMetrics CompareRgba(std::span<const uint8_t> expected,
                          std::span<const uint8_t> actual,
                          uint8_t tolerance);

}  // namespace canvas::poc01

#endif  // CANVAS_POC_SKIA_SCENE_RENDERER_H_
