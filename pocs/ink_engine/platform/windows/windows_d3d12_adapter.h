#ifndef CANVAS_POC02_WINDOWS_D3D12_ADAPTER_H_
#define CANVAS_POC02_WINDOWS_D3D12_ADAPTER_H_

#include <windows.h>

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "canvas_poc02/ink_engine.h"

namespace canvas::poc02 {

struct WindowsAdapterInfo {
  std::string description;
  uint32_t vendor_id = 0;
  uint32_t device_id = 0;
  uint64_t driver_version = 0;
  bool warp = false;
};

class WindowsD3d12Adapter {
 public:
  WindowsD3d12Adapter();
  ~WindowsD3d12Adapter();
  WindowsD3d12Adapter(const WindowsD3d12Adapter&) = delete;
  WindowsD3d12Adapter& operator=(const WindowsD3d12Adapter&) = delete;

  Status Initialize(HWND window, bool use_warp, uint32_t width, uint32_t height);
  Status Render(const StrokeDocument& document,
                const DefaultPreviewSink::State* preview,
                std::vector<uint8_t>* rgba = nullptr);
  Status PresentToWindow(std::span<const uint8_t> rgba);
  [[nodiscard]] const WindowsAdapterInfo& info() const;
  [[nodiscard]] const std::string& error() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace canvas::poc02

#endif  // CANVAS_POC02_WINDOWS_D3D12_ADAPTER_H_
