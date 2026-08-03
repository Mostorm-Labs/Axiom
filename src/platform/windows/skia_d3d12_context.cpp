#include "platform/windows/skia_d3d12_context.h"

#include <spdlog/spdlog.h>

#include <windows.h>

#include "include/gpu/ganesh/GrDirectContext.h"
#include "include/gpu/ganesh/d3d/GrD3DBackendContext.h"
#include "include/gpu/ganesh/d3d/GrD3DDirectContext.h"

namespace canvas::windows {

HRESULT SkiaD3D12Context::initialize(bool allowSoftwareFallback) {
  context_.reset();
  queue_.Reset();
  device_.Reset();
  adapter_.Reset();
  factory_.Reset();
  usingSoftwareAdapter_ = false;

  HRESULT hr = CreateDXGIFactory2(0, IID_PPV_ARGS(factory_.ReleaseAndGetAddressOf()));
  if (FAILED(hr)) return hr;

  HRESULT firstFailure = E_FAIL;
  for (UINT index = 0;; ++index) {
    Microsoft::WRL::ComPtr<IDXGIAdapter1> candidate;
    hr = factory_->EnumAdapters1(index, candidate.ReleaseAndGetAddressOf());
    if (hr == DXGI_ERROR_NOT_FOUND) break;
    if (FAILED(hr)) {
      if (firstFailure == E_FAIL) firstFailure = hr;
      continue;
    }
    DXGI_ADAPTER_DESC1 desc{};
    if (FAILED(candidate->GetDesc1(&desc))) continue;
    if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) continue;
    Microsoft::WRL::ComPtr<ID3D12Device> candidateDevice;
    hr = D3D12CreateDevice(candidate.Get(), D3D_FEATURE_LEVEL_11_0,
                            IID_PPV_ARGS(candidateDevice.ReleaseAndGetAddressOf()));
    if (FAILED(hr)) {
      if (firstFailure == E_FAIL) firstFailure = hr;
      continue;
    }
    adapter_ = candidate;
    device_ = candidateDevice;
    char adapterName[256]{};
    const int converted = WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1,
                                              adapterName, sizeof(adapterName),
                                              nullptr, nullptr);
    spdlog::info("canvas D3D12 adapter: {}",
                 converted > 0 ? adapterName : "<unknown>");
    break;
  }
  if (!device_ && allowSoftwareFallback) {
    Microsoft::WRL::ComPtr<IDXGIAdapter1> warp;
    hr = factory_->EnumWarpAdapter(IID_PPV_ARGS(warp.ReleaseAndGetAddressOf()));
    if (SUCCEEDED(hr)) {
      Microsoft::WRL::ComPtr<ID3D12Device> warpDevice;
      hr = D3D12CreateDevice(
          warp.Get(), D3D_FEATURE_LEVEL_11_0,
          IID_PPV_ARGS(warpDevice.ReleaseAndGetAddressOf()));
      if (SUCCEEDED(hr)) {
        adapter_ = warp;
        device_ = warpDevice;
        usingSoftwareAdapter_ = true;
        spdlog::warn(
            "canvas D3D12 adapter: explicitly enabled WARP software fallback");
      } else if (firstFailure == E_FAIL) {
        firstFailure = hr;
      }
    } else if (firstFailure == E_FAIL) {
      firstFailure = hr;
    }
  }
  if (!device_ || !adapter_) return firstFailure;

  D3D12_COMMAND_QUEUE_DESC queueDesc{};
  queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  hr = device_->CreateCommandQueue(&queueDesc,
                                   IID_PPV_ARGS(queue_.ReleaseAndGetAddressOf()));
  if (FAILED(hr)) return hr;

  GrD3DBackendContext backend{};
  backend.fAdapter.retain(adapter_.Get());
  backend.fDevice.retain(device_.Get());
  backend.fQueue.retain(queue_.Get());
  context_ = GrDirectContexts::MakeD3D(backend);
  if (!context_) {
    return E_FAIL;
  }
  return S_OK;
}

}  // namespace canvas::windows
