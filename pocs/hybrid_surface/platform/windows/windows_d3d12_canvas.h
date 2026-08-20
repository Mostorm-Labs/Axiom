#ifndef CANVAS_POC05_WINDOWS_D3D12_CANVAS_H_
#define CANVAS_POC05_WINDOWS_D3D12_CANVAS_H_

#ifndef NOMINMAX
#define NOMINMAX 1
#endif
#include <windows.h>

#include <cstdint>
#include <memory>
#include <string>

namespace canvas::poc05::windows {

struct D3D12CanvasInfo {
  std::string adapter_description;
  std::uint32_t vendor_id = 0;
  std::uint32_t device_id = 0;
  std::uint64_t driver_version = 0;
  std::uint32_t width_pixels = 0;
  std::uint32_t height_pixels = 0;
  bool skia_enabled = false;
  bool skia_content_probe_passed = false;
  bool poc03_scene_bridge_enabled = false;
  std::uint32_t poc03_scene_active_count = 0;
  std::uint64_t render_count = 0;
};

class D3D12Canvas final {
 public:
  D3D12Canvas();
  ~D3D12Canvas();
  D3D12Canvas(const D3D12Canvas&) = delete;
  D3D12Canvas& operator=(const D3D12Canvas&) = delete;
  bool initialize(HWND window, std::uint32_t width, std::uint32_t height,
                  std::string* error);
  bool render(std::string* error);
  [[nodiscard]] bool skia_enabled() const;
  [[nodiscard]] const D3D12CanvasInfo& info() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace canvas::poc05::windows

#endif  // CANVAS_POC05_WINDOWS_D3D12_CANVAS_H_
