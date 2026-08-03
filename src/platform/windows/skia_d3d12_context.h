#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include "include/core/SkRefCnt.h"
#include "include/gpu/ganesh/GrDirectContext.h"

namespace canvas::windows {

class SkiaD3D12Context {
 public:
  SkiaD3D12Context() = default;
  SkiaD3D12Context(const SkiaD3D12Context&) = delete;
  SkiaD3D12Context& operator=(const SkiaD3D12Context&) = delete;
  SkiaD3D12Context(SkiaD3D12Context&&) = delete;
  SkiaD3D12Context& operator=(SkiaD3D12Context&&) = delete;

  HRESULT initialize(bool allowSoftwareFallback = false);

  IDXGIFactory4* factory() const noexcept { return factory_.Get(); }
  IDXGIAdapter1* adapter() const noexcept { return adapter_.Get(); }
  ID3D12Device* device() const noexcept { return device_.Get(); }
  ID3D12CommandQueue* queue() const noexcept { return queue_.Get(); }
  GrDirectContext* context() const noexcept { return context_.get(); }
  const sk_sp<GrDirectContext>& skiaContext() const noexcept { return context_; }
  bool usingSoftwareAdapter() const noexcept { return usingSoftwareAdapter_; }

 private:
  Microsoft::WRL::ComPtr<IDXGIFactory4> factory_;
  Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter_;
  Microsoft::WRL::ComPtr<ID3D12Device> device_;
  Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue_;
  sk_sp<GrDirectContext> context_;
  bool usingSoftwareAdapter_ = false;
};

}  // namespace canvas::windows
