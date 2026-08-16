#include "windows_d3d12_adapter.h"

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <algorithm>
#include <string>
#include <utility>

#include "include/core/SkColorSpace.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkSurface.h"
#include "include/gpu/ganesh/GrDirectContext.h"
#include "include/gpu/ganesh/SkSurfaceGanesh.h"
#include "include/gpu/ganesh/d3d/GrD3DBackendContext.h"
#include "include/gpu/ganesh/d3d/GrD3DDirectContext.h"
#include "scene_compiler.h"
#include "skia_scene_renderer.h"

using Microsoft::WRL::ComPtr;

namespace canvas::poc01 {
namespace {

std::string Narrow(const wchar_t* value) {
  if (value == nullptr) return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0,
                                       nullptr, nullptr);
  if (size <= 1) return {};
  std::string result(static_cast<size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), size, nullptr,
                      nullptr);
  result.pop_back();
  return result;
}

}  // namespace

struct WindowsD3d12Adapter::Impl {
  HWND window = nullptr;
  uint32_t width = 0;
  uint32_t height = 0;
  WindowsAdapterInfo info;
  ComPtr<IDXGIAdapter1> adapter;
  ComPtr<ID3D12Device> device;
  ComPtr<ID3D12CommandQueue> queue;
  sk_sp<GrDirectContext> context;
  sk_sp<SkSurface> surface;
};

WindowsD3d12Adapter::WindowsD3d12Adapter()
    : impl_(std::make_unique<Impl>()) {}

WindowsD3d12Adapter::~WindowsD3d12Adapter() {
  if (impl_->context != nullptr) {
    impl_->context->abandonContext();
  }
}

canvas_poc_status_t WindowsD3d12Adapter::Initialize(
    HWND window, bool use_warp, uint32_t width, uint32_t height) {
  if (width == 0 || height == 0) {
    SetLastError("D3D12 target dimensions must be positive");
    return CANVAS_POC_STATUS_INVALID_ARGUMENT;
  }
  UINT factory_flags = 0;
#if defined(_DEBUG)
  ComPtr<ID3D12Debug> debug;
  if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
    debug->EnableDebugLayer();
    factory_flags |= DXGI_CREATE_FACTORY_DEBUG;
  }
#endif
  ComPtr<IDXGIFactory6> factory;
  if (FAILED(CreateDXGIFactory2(factory_flags, IID_PPV_ARGS(&factory)))) {
    SetLastError("CreateDXGIFactory2 failed");
    return CANVAS_POC_STATUS_PLATFORM_ERROR;
  }
  if (use_warp) {
    if (FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&impl_->adapter)))) {
      SetLastError("DXGI WARP adapter is unavailable");
      return CANVAS_POC_STATUS_PLATFORM_ERROR;
    }
  } else {
    for (UINT index = 0;; ++index) {
      ComPtr<IDXGIAdapter1> candidate;
      if (factory->EnumAdapterByGpuPreference(
              index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
              IID_PPV_ARGS(&candidate)) == DXGI_ERROR_NOT_FOUND) {
        break;
      }
      DXGI_ADAPTER_DESC1 description{};
      candidate->GetDesc1(&description);
      if ((description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0 &&
          SUCCEEDED(D3D12CreateDevice(candidate.Get(),
                                      D3D_FEATURE_LEVEL_12_0,
                                      __uuidof(ID3D12Device), nullptr))) {
        impl_->adapter = candidate;
        break;
      }
    }
    if (impl_->adapter == nullptr) {
      SetLastError("no high-performance D3D12 adapter found");
      return CANVAS_POC_STATUS_PLATFORM_ERROR;
    }
  }
  if (FAILED(D3D12CreateDevice(impl_->adapter.Get(), D3D_FEATURE_LEVEL_12_0,
                               IID_PPV_ARGS(&impl_->device)))) {
    SetLastError("D3D12CreateDevice failed");
    return CANVAS_POC_STATUS_PLATFORM_ERROR;
  }
  D3D12_COMMAND_QUEUE_DESC queue_description{};
  queue_description.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  if (FAILED(impl_->device->CreateCommandQueue(
          &queue_description, IID_PPV_ARGS(&impl_->queue)))) {
    SetLastError("D3D12 command queue creation failed");
    return CANVAS_POC_STATUS_PLATFORM_ERROR;
  }
  GrD3DBackendContext backend;
  backend.fAdapter.retain(impl_->adapter.Get());
  backend.fDevice.retain(impl_->device.Get());
  backend.fQueue.retain(impl_->queue.Get());
  impl_->context = GrDirectContexts::MakeD3D(backend);
  if (impl_->context == nullptr) {
    SetLastError("Skia failed to create Ganesh D3D12 context");
    return CANVAS_POC_STATUS_PLATFORM_ERROR;
  }
  const SkImageInfo image_info = SkImageInfo::Make(
      static_cast<int>(width), static_cast<int>(height),
      kRGBA_8888_SkColorType, kPremul_SkAlphaType, SkColorSpace::MakeSRGB());
  impl_->surface = SkSurfaces::RenderTarget(
      impl_->context.get(), skgpu::Budgeted::kNo, image_info, 0,
      kTopLeft_GrSurfaceOrigin, nullptr);
  if (impl_->surface == nullptr) {
    SetLastError("Skia failed to create D3D12 render target");
    return CANVAS_POC_STATUS_RENDER_ERROR;
  }
  DXGI_ADAPTER_DESC1 description{};
  impl_->adapter->GetDesc1(&description);
  LARGE_INTEGER driver{};
  impl_->adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &driver);
  impl_->info.description = Narrow(description.Description);
  impl_->info.vendor_id = description.VendorId;
  impl_->info.device_id = description.DeviceId;
  impl_->info.driver_version = static_cast<uint64_t>(driver.QuadPart);
  impl_->info.warp = use_warp;
  impl_->window = window;
  impl_->width = width;
  impl_->height = height;
  return CANVAS_POC_STATUS_OK;
}

canvas_poc_status_t WindowsD3d12Adapter::Render(
    const Document& document, std::vector<uint8_t>* rgba) {
  if (impl_->surface == nullptr || rgba == nullptr) {
    SetLastError("D3D12 adapter is not initialized");
    return CANVAS_POC_STATUS_INVALID_ARGUMENT;
  }
  const RuntimeScene scene = SceneCompiler().Compile(document);
  SkiaSceneRenderer renderer;
  canvas_poc_status_t status =
      renderer.Draw(*impl_->surface->getCanvas(), scene, document.assets());
  if (status != CANVAS_POC_STATUS_OK) return status;
  impl_->context->flushAndSubmit(impl_->surface.get(), GrSyncCpu::kYes);
  return renderer.Readback(*impl_->surface, impl_->width, impl_->height, rgba);
}

canvas_poc_status_t WindowsD3d12Adapter::PresentToWindow(
    std::span<const uint8_t> rgba) {
  if (impl_->window == nullptr) return CANVAS_POC_STATUS_OK;
  if (rgba.size() != static_cast<size_t>(impl_->width) * impl_->height * 4U) {
    SetLastError("D3D12 presentation readback size is invalid");
    return CANVAS_POC_STATUS_INVALID_ARGUMENT;
  }
  std::vector<uint8_t> bgra(rgba.size());
  for (size_t offset = 0; offset < rgba.size(); offset += 4) {
    bgra[offset] = rgba[offset + 2];
    bgra[offset + 1] = rgba[offset + 1];
    bgra[offset + 2] = rgba[offset];
    bgra[offset + 3] = rgba[offset + 3];
  }
  BITMAPINFO bitmap{};
  bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bitmap.bmiHeader.biWidth = static_cast<LONG>(impl_->width);
  bitmap.bmiHeader.biHeight = -static_cast<LONG>(impl_->height);
  bitmap.bmiHeader.biPlanes = 1;
  bitmap.bmiHeader.biBitCount = 32;
  bitmap.bmiHeader.biCompression = BI_RGB;
  HDC device_context = GetDC(impl_->window);
  StretchDIBits(device_context, 0, 0, static_cast<int>(impl_->width),
                static_cast<int>(impl_->height), 0, 0,
                static_cast<int>(impl_->width), static_cast<int>(impl_->height),
                bgra.data(), &bitmap, DIB_RGB_COLORS, SRCCOPY);
  ReleaseDC(impl_->window, device_context);
  return CANVAS_POC_STATUS_OK;
}

const WindowsAdapterInfo& WindowsD3d12Adapter::info() const {
  return impl_->info;
}

}  // namespace canvas::poc01
