#ifndef CANVAS_POC_WINDOWS_D3D12_ADAPTER_H_
#define CANVAS_POC_WINDOWS_D3D12_ADAPTER_H_

#include <windows.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "document.h"

namespace canvas::poc01 {

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

  canvas_poc_status_t Initialize(HWND window, bool use_warp, uint32_t width,
                                 uint32_t height);
  canvas_poc_status_t Render(const Document& document,
                             std::vector<uint8_t>* rgba);
  canvas_poc_status_t PresentToWindow(std::span<const uint8_t> rgba);
  [[nodiscard]] const WindowsAdapterInfo& info() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace canvas::poc01

#endif  // CANVAS_POC_WINDOWS_D3D12_ADAPTER_H_
