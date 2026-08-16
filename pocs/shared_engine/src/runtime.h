#ifndef CANVAS_POC_RUNTIME_H_
#define CANVAS_POC_RUNTIME_H_

#include <cstdint>
#include <memory>
#include <span>
#include <string>

#include "canvas_poc/canvas_poc.h"
#include "document.h"
#include "scene_compiler.h"
#include "software_probe_renderer.h"

namespace canvas::poc01 {

class Runtime {
 public:
  Runtime(void* user_data, canvas_poc_log_fn log);

  [[nodiscard]] std::shared_ptr<AssetRegistry> assets() const { return assets_; }
  void Log(uint32_t level, std::string_view message) const;

 private:
  std::shared_ptr<AssetRegistry> assets_ = std::make_shared<AssetRegistry>();
  void* user_data_ = nullptr;
  canvas_poc_log_fn log_ = nullptr;
};

class View {
 public:
  View(std::shared_ptr<Document> document, uint32_t width, uint32_t height,
       float device_pixel_ratio);

  canvas_poc_status_t Resize(uint32_t width, uint32_t height,
                             float device_pixel_ratio);
  canvas_poc_status_t Render();
  [[nodiscard]] std::span<const uint8_t> pixels() const {
    return renderer_.pixels();
  }

 private:
  std::shared_ptr<Document> document_;
  uint32_t width_ = 0;
  uint32_t height_ = 0;
  float device_pixel_ratio_ = 1.0F;
  SceneCompiler compiler_;
  SoftwareProbeRenderer renderer_;
};

}  // namespace canvas::poc01

#endif  // CANVAS_POC_RUNTIME_H_
