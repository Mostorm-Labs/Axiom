#pragma once

#include <dxgi1_3.h>
#include <wrl/client.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <utility>

#include "canvas/core/geometry.h"
#include "canvas/document/document.h"
#include "platform/windows/dcomp_host.h"
#include "platform/windows/skia_d3d12_context.h"

namespace canvas::windows {

class SkiaSwapChainLayer {
 public:
  using FramePresentedHandler = std::function<void(std::uint64_t, std::uint64_t)>;

  ~SkiaSwapChainLayer();

  HRESULT initialize(SkiaD3D12Context& gpu, DCompHost& host, VisualSlot slot,
                     int width, int height, bool transparent);
  HRESULT render(const document::Document& document, document::LayerClass layer,
                 const std::optional<core::Rect>& dirtyBounds = std::nullopt);
  HRESULT present(const std::optional<core::Rect>& dirtyBounds = std::nullopt);
  HRESULT resize(int width, int height);
  void cleanup() noexcept;

  DXGI_ALPHA_MODE alphaMode() const noexcept { return alphaMode_; }
  IDXGISwapChain3* swapChain() const noexcept { return swapChain_.Get(); }
  std::uint64_t frameId() const noexcept { return frameId_; }
  void setFramePresentedHandler(FramePresentedHandler handler) {
    framePresentedHandler_ = std::move(handler);
  }

 private:
  HRESULT createSwapChain();
  static bool toRect(const core::Rect& bounds, RECT* out, int width, int height);

  SkiaD3D12Context* gpu_ = nullptr;
  DCompHost* host_ = nullptr;
  VisualSlot slot_ = VisualSlot::BaseCanvas;
  int width_ = 0;
  int height_ = 0;
  bool transparent_ = false;
  DXGI_ALPHA_MODE alphaMode_ = DXGI_ALPHA_MODE_IGNORE;
  bool needsFullRedraw_ = true;
  std::uint64_t frameId_ = 0;
  FramePresentedHandler framePresentedHandler_;
  Microsoft::WRL::ComPtr<IDXGISwapChain3> swapChain_;
};

}  // namespace canvas::windows
