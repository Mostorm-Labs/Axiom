#include "platform/windows/skia_swap_chain_layer.h"
#include "platform/windows/qpc_clock.h"

#include "canvas/render/skia_renderer.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkSurface.h"
#include "include/gpu/ganesh/GrBackendSurface.h"
#include "include/gpu/ganesh/GrDirectContext.h"
#include "include/gpu/ganesh/d3d/GrD3DBackendSurface.h"
#include "include/gpu/ganesh/SkSurfaceGanesh.h"

#include <algorithm>
#include <cmath>
namespace canvas::windows {

SkiaSwapChainLayer::~SkiaSwapChainLayer() { cleanup(); }

HRESULT SkiaSwapChainLayer::initialize(SkiaD3D12Context& gpu, DCompHost& host,
                                       VisualSlot slot, int width, int height,
                                       bool transparent) {
  if (width <= 0 || height <= 0 || gpu.factory() == nullptr ||
      gpu.queue() == nullptr || gpu.context() == nullptr) {
    return E_INVALIDARG;
  }
  cleanup();
  gpu_ = &gpu;
  host_ = &host;
  slot_ = slot;
  width_ = width;
  height_ = height;
  transparent_ = transparent;
  clearColorArgb_ = transparent ? SK_ColorTRANSPARENT : SK_ColorWHITE;
  alphaMode_ = transparent ? DXGI_ALPHA_MODE_PREMULTIPLIED : DXGI_ALPHA_MODE_IGNORE;
  frameId_ = 0;
  mediaPresentCount_ = 0;
  mediaFrameId_ = 0;
  lastCallbackFrameId_ = 0;
  const HRESULT hr = createSwapChain();
  if (FAILED(hr)) cleanup();
  return hr;
}

HRESULT SkiaSwapChainLayer::createSwapChain() {
  DXGI_SWAP_CHAIN_DESC1 desc{};
  desc.Width = static_cast<UINT>(width_);
  desc.Height = static_cast<UINT>(height_);
  desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  desc.Stereo = FALSE;
  desc.SampleDesc = {1, 0};
  desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  desc.BufferCount = 2;
  desc.Scaling = DXGI_SCALING_STRETCH;
  desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
  desc.AlphaMode = alphaMode_;
  desc.Flags = 0;

  Microsoft::WRL::ComPtr<IDXGISwapChain1> chain;
  HRESULT hr = gpu_->factory()->CreateSwapChainForComposition(
      gpu_->queue(), &desc, nullptr, chain.ReleaseAndGetAddressOf());
  if (FAILED(hr)) return hr;
  hr = chain.As(&swapChain_);
  if (FAILED(hr)) return hr;
  hr = host_->setContent(slot_, swapChain_.Get());
  if (FAILED(hr)) return hr;
  needsFullRedraw_.fill(true);
  return S_OK;
}

bool SkiaSwapChainLayer::toRect(const core::Rect& bounds, RECT* out, int width,
                                int height) {
  if (out == nullptr || !std::isfinite(bounds.x) || !std::isfinite(bounds.y) ||
      !std::isfinite(bounds.width) || !std::isfinite(bounds.height) ||
      bounds.width <= 0.0F || bounds.height <= 0.0F) {
    return false;
  }
  const float left = std::clamp(bounds.x, 0.0F, static_cast<float>(width));
  const float top = std::clamp(bounds.y, 0.0F, static_cast<float>(height));
  const float right = std::clamp(bounds.x + bounds.width, 0.0F, static_cast<float>(width));
  const float bottom = std::clamp(bounds.y + bounds.height, 0.0F, static_cast<float>(height));
  if (right <= left || bottom <= top) return false;
  out->left = static_cast<LONG>(left);
  out->top = static_cast<LONG>(top);
  out->right = static_cast<LONG>(std::ceil(right));
  out->bottom = static_cast<LONG>(std::ceil(bottom));
  return out->right > out->left && out->bottom > out->top;
}

HRESULT SkiaSwapChainLayer::render(const document::Document& document,
                                   document::LayerClass layer,
                                   const std::optional<core::Rect>& dirtyBounds) {
  if (!swapChain_ || gpu_ == nullptr || gpu_->context() == nullptr) return E_UNEXPECTED;
  Microsoft::WRL::ComPtr<ID3D12Resource> resource;
  renderedBufferIndex_ = swapChain_->GetCurrentBackBufferIndex();
  if (renderedBufferIndex_ >= needsFullRedraw_.size()) return E_FAIL;
  renderedFullInvalidation_ = !dirtyBounds.has_value();
  std::optional<core::Rect> normalizedDirty = dirtyBounds;
  RECT validatedDirty{};
  if (normalizedDirty &&
      !toRect(*normalizedDirty, &validatedDirty, width_, height_)) {
    renderedFullInvalidation_ = false;
    normalizedDirty.reset();
    if (!needsFullRedraw_[renderedBufferIndex_] &&
        !pendingDirty_[renderedBufferIndex_]) {
      return S_FALSE;
    }
  }
  renderedInvalidation_ = normalizedDirty;
  std::optional<core::Rect> effectiveDirty;
  if (!needsFullRedraw_[renderedBufferIndex_]) {
    effectiveDirty = normalizedDirty;
    if (pendingDirty_[renderedBufferIndex_]) {
      effectiveDirty = effectiveDirty
                           ? effectiveDirty->united(
                                 *pendingDirty_[renderedBufferIndex_])
                           : pendingDirty_[renderedBufferIndex_];
    }
  }
  HRESULT hr = swapChain_->GetBuffer(renderedBufferIndex_,
                                     IID_PPV_ARGS(resource.ReleaseAndGetAddressOf()));
  if (FAILED(hr)) return hr;

  GrD3DTextureResourceInfo info{};
  info.fResource.retain(resource.Get());
  info.fResourceState = D3D12_RESOURCE_STATE_PRESENT;
  info.fFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
  info.fSampleCount = 1;
  info.fLevelCount = 1;
  const GrBackendRenderTarget target =
      GrBackendRenderTargets::MakeD3D(width_, height_, info);
  if (!target.isValid()) return E_FAIL;
  auto surface = SkSurfaces::WrapBackendRenderTarget(
      gpu_->context(), target, kTopLeft_GrSurfaceOrigin,
      kBGRA_8888_SkColorType, nullptr, nullptr);
  if (!surface) return E_FAIL;

  SkCanvas* canvas = surface->getCanvas();
  canvas->save();
  if (effectiveDirty.has_value()) {
    RECT ignored{};
    if (toRect(*effectiveDirty, &ignored, width_, height_)) {
      canvas->clipRect(SkRect::MakeLTRB(static_cast<float>(ignored.left),
                                        static_cast<float>(ignored.top),
                                        static_cast<float>(ignored.right),
                                        static_cast<float>(ignored.bottom)));
    }
  }
  canvas->clear(static_cast<SkColor>(clearColorArgb_));
  canvas::render::SkiaRenderer renderer;
  renderer.drawLayer(*canvas, document, layer, effectiveDirty);
  canvas->restore();
  gpu_->context()->flushAndSubmit(GrSyncCpu::kNo);
  bufferRendered_ = true;
  return present(effectiveDirty);
}

HRESULT SkiaSwapChainLayer::present(const std::optional<core::Rect>& dirtyBounds) {
  if (!swapChain_) return E_UNEXPECTED;
  RECT dirty{};
  HRESULT hr = S_OK;
  if (dirtyBounds.has_value() && toRect(*dirtyBounds, &dirty, width_, height_)) {
    DXGI_PRESENT_PARAMETERS parameters{};
    parameters.DirtyRectsCount = 1;
    parameters.pDirtyRects = &dirty;
    hr = swapChain_->Present1(1, 0, &parameters);
  } else {
    hr = swapChain_->Present(1, 0);
  }
  if (SUCCEEDED(hr)) {
    ++frameId_;
    if (bufferRendered_ && renderedBufferIndex_ < needsFullRedraw_.size()) {
      needsFullRedraw_[renderedBufferIndex_] = false;
      pendingDirty_[renderedBufferIndex_].reset();
      for (UINT index = 0; index < needsFullRedraw_.size(); ++index) {
        if (index == renderedBufferIndex_) continue;
        if (renderedFullInvalidation_) {
          needsFullRedraw_[index] = true;
          pendingDirty_[index].reset();
        } else if (renderedInvalidation_ && !needsFullRedraw_[index]) {
          pendingDirty_[index] = pendingDirty_[index]
                                     ? pendingDirty_[index]->united(
                                           *renderedInvalidation_)
                                     : renderedInvalidation_;
        }
      }
      bufferRendered_ = false;
    }
    UINT lastPresentCount = 0;
    if (SUCCEEDED(swapChain_->GetLastPresentCount(&lastPresentCount))) {
      submittedFrames_.emplace_back(lastPresentCount, frameId_);
      while (submittedFrames_.size() > 16) submittedFrames_.pop_front();
    }
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    std::uint64_t micros = 0;
    std::uint64_t callbackFrameId = 0;
    Microsoft::WRL::ComPtr<IDXGISwapChainMedia> media;
    DXGI_FRAME_STATISTICS_MEDIA stats{};
    const HRESULT mediaResult = swapChain_.As(&media);
    const HRESULT statsResult = SUCCEEDED(mediaResult)
                                    ? media->GetFrameStatisticsMedia(&stats)
                                    : mediaResult;
    if (SUCCEEDED(statsResult) &&
        stats.SyncQPCTime.QuadPart > 0 &&
        stats.PresentCount > mediaPresentCount_) {
      const auto match = std::find_if(
          submittedFrames_.begin(), submittedFrames_.end(),
          [&stats](const auto& submitted) {
            return submitted.first == stats.PresentCount;
          });
      if (match != submittedFrames_.end()) {
        mediaPresentCount_ = stats.PresentCount;
        mediaFrameId_ = match->second;
        callbackFrameId = mediaFrameId_;
        micros = qpcTicksToMicros(
            static_cast<std::uint64_t>(stats.SyncQPCTime.QuadPart),
            static_cast<std::uint64_t>(frequency.QuadPart));
      }
    }
    // Unsupported/disjoint media statistics fall back to the local QPC for
    // the frame just submitted. Stale media statistics do not emit a second
    // callback for a frame already observed.
    if (FAILED(statsResult)) {
      callbackFrameId = frameId_;
      LARGE_INTEGER now{};
      if (QueryPerformanceCounter(&now)) {
        micros = qpcTicksToMicros(
            static_cast<std::uint64_t>(now.QuadPart),
            static_cast<std::uint64_t>(frequency.QuadPart));
      }
    }
    if (framePresentedHandler_ && callbackFrameId > lastCallbackFrameId_) {
      lastCallbackFrameId_ = callbackFrameId;
      framePresentedHandler_(callbackFrameId, micros);
    }
  } else if (bufferRendered_ &&
             renderedBufferIndex_ < needsFullRedraw_.size()) {
    needsFullRedraw_[renderedBufferIndex_] = true;
  }
  return hr;
}

HRESULT SkiaSwapChainLayer::resize(int width, int height) {
  if (!swapChain_ || width <= 0 || height <= 0) return E_INVALIDARG;
  if (gpu_ == nullptr || gpu_->context() == nullptr) return E_UNEXPECTED;
  gpu_->context()->flushAndSubmit(GrSyncCpu::kYes);
  const HRESULT hr = swapChain_->ResizeBuffers(2, static_cast<UINT>(width),
                                               static_cast<UINT>(height),
                                               DXGI_FORMAT_B8G8R8A8_UNORM, 0);
  if (SUCCEEDED(hr)) {
    width_ = width;
    height_ = height;
    needsFullRedraw_.fill(true);
    pendingDirty_.fill(std::nullopt);
    bufferRendered_ = false;
    mediaPresentCount_ = 0;
    mediaFrameId_ = 0;
    submittedFrames_.clear();
  }
  return hr;
}

void SkiaSwapChainLayer::cleanup() noexcept {
  if (host_ != nullptr) {
    (void)host_->setContent(slot_, nullptr);
  }
  swapChain_.Reset();
  gpu_ = nullptr;
  host_ = nullptr;
  needsFullRedraw_.fill(true);
  pendingDirty_.fill(std::nullopt);
  renderedInvalidation_.reset();
  renderedFullInvalidation_ = false;
  bufferRendered_ = false;
  submittedFrames_.clear();
}

}  // namespace canvas::windows
