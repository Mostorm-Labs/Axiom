#pragma once

#include <dxgi1_3.h>
#include <wrl/client.h>

#include <cstdint>
#include <array>
#include <functional>
#include <deque>
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
  std::uint64_t mediaPresentCount() const noexcept {
    return mediaPresentCount_;
  }
  std::uint64_t mediaFrameId() const noexcept { return mediaFrameId_; }
  bool presentationStatisticsAvailable() const noexcept {
    return presentationStatisticsAvailable_;
  }
  HRESULT lastPresentationStatisticsResult() const noexcept {
    return lastPresentationStatisticsResult_;
  }
  void setClearColorArgb(std::uint32_t colorArgb) noexcept {
    clearColorArgb_ = colorArgb;
  }
  bool bufferNeedsFullRedraw(UINT index) const noexcept {
    return index < needsFullRedraw_.size() ? needsFullRedraw_[index] : true;
  }
  bool bufferHasPendingDirty(UINT index) const noexcept {
    return index < pendingDirty_.size() && pendingDirty_[index].has_value();
  }
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
  std::uint32_t clearColorArgb_ = 0;
  DXGI_ALPHA_MODE alphaMode_ = DXGI_ALPHA_MODE_IGNORE;
  std::array<bool, 2> needsFullRedraw_{{true, true}};
  std::array<std::optional<core::Rect>, 2> pendingDirty_{};
  std::optional<core::Rect> renderedInvalidation_;
  bool renderedFullInvalidation_ = false;
  UINT renderedBufferIndex_ = 0;
  std::uint64_t frameId_ = 0;
  std::uint64_t mediaPresentCount_ = 0;
  std::uint64_t mediaFrameId_ = 0;
  std::uint64_t lastCallbackFrameId_ = 0;
  bool presentationStatisticsAvailable_ = false;
  HRESULT lastPresentationStatisticsResult_ = E_PENDING;
  bool bufferRendered_ = false;
  std::deque<std::pair<std::uint64_t, std::uint64_t>> submittedFrames_;
  FramePresentedHandler framePresentedHandler_;
  Microsoft::WRL::ComPtr<IDXGISwapChain3> swapChain_;
};

}  // namespace canvas::windows
