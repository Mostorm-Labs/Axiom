#ifndef CANVAS_POC_SOFTWARE_PROBE_RENDERER_H_
#define CANVAS_POC_SOFTWARE_PROBE_RENDERER_H_

#include <cstdint>
#include <span>
#include <vector>

#include "scene_compiler.h"

namespace canvas::poc01 {

// Deterministic dependency-free probe used only by host lifecycle and model
// tests. Platform visual acceptance always uses Skia raster/D3D12/WebGL2.
class SoftwareProbeRenderer {
 public:
  canvas_poc_status_t Render(const RuntimeScene& scene,
                             const AssetRegistry& assets, uint32_t width,
                             uint32_t height, float device_pixel_ratio);
  [[nodiscard]] std::span<const uint8_t> pixels() const { return pixels_; }

 private:
  uint32_t width_ = 0;
  uint32_t height_ = 0;
  std::vector<uint8_t> pixels_;

  void BlendPixel(int x, int y, Color color);
  void FillRect(float x, float y, float width, float height, Color color);
  void DrawLine(float x0, float y0, float x1, float y1, float width,
                Color color);
};

}  // namespace canvas::poc01

#endif  // CANVAS_POC_SOFTWARE_PROBE_RENDERER_H_
