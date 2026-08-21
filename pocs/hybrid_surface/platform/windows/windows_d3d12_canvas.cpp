#include "windows_d3d12_canvas.h"

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <sstream>
#include <array>
#include <exception>
#include <optional>

#if defined(CANVAS_POC05_HAS_SKIA)
#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkFont.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPaint.h"
#include "include/core/SkSurface.h"
#include "include/gpu/ganesh/GrBackendSurface.h"
#include "include/gpu/ganesh/GrDirectContext.h"
#include "include/gpu/ganesh/GrTypes.h"
#include "include/gpu/ganesh/SkSurfaceGanesh.h"
#include "include/gpu/ganesh/d3d/GrD3DBackendContext.h"
#include "include/gpu/ganesh/d3d/GrD3DBackendSurface.h"
#include "include/gpu/ganesh/d3d/GrD3DDirectContext.h"
#include "include/gpu/ganesh/d3d/GrD3DTypes.h"
#if defined(CANVAS_POC05_HAS_POC03_SCENE)
#include "canvas/poc03/large_scene.h"
#include "skia_large_scene_renderer.h"
#endif
#endif

using Microsoft::WRL::ComPtr;

namespace canvas::poc05::windows {
namespace {
std::string HrError(HRESULT result, const char* action) {
  std::ostringstream stream;
  stream << action << " failed (0x" << std::hex
         << static_cast<unsigned long>(result) << ")";
  return stream.str();
}
std::string Narrow(const wchar_t* value) {
  if (value == nullptr) return {};
  const int required = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0,
                                           nullptr, nullptr);
  if (required <= 1) return {};
  std::string result(static_cast<std::size_t>(required), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), required, nullptr,
                      nullptr);
  result.resize(static_cast<std::size_t>(required - 1));
  return result;
}
}  // namespace

struct D3D12Canvas::Impl {
  HWND window = nullptr;
  D3D12CanvasInfo info;
  ComPtr<IDXGISwapChain3> swap_chain;
  ComPtr<ID3D12Device> device;
  ComPtr<ID3D12CommandQueue> queue;
  ComPtr<ID3D12DescriptorHeap> rtv_heap;
  ComPtr<ID3D12CommandAllocator> allocator;
  ComPtr<ID3D12GraphicsCommandList> list;
  ComPtr<ID3D12Fence> fence;
  HANDLE fence_event = nullptr;
  std::uint64_t fence_value = 0;
  std::uint64_t frame_index = 0;
  UINT rtv_stride = 0;
  ComPtr<ID3D12Resource> buffers[2];
#if defined(CANVAS_POC05_HAS_SKIA)
  sk_sp<GrDirectContext> skia_context;
  sk_sp<SkSurface> skia_surfaces[2];
#if defined(CANVAS_POC05_HAS_POC03_SCENE)
  std::unique_ptr<canvas::poc03::Document> document;
  std::unique_ptr<canvas::poc03::RuntimeScene> scene;
#endif
#endif
};

D3D12Canvas::D3D12Canvas() : impl_(std::make_unique<Impl>()) {}
D3D12Canvas::~D3D12Canvas() {
#if defined(CANVAS_POC05_HAS_SKIA)
  for (auto& surface : impl_->skia_surfaces) surface.reset();
  if (impl_->skia_context != nullptr) impl_->skia_context->abandonContext();
#endif
  if (impl_->fence_event != nullptr) CloseHandle(impl_->fence_event);
}

bool D3D12Canvas::initialize(HWND window, std::uint32_t width,
                             std::uint32_t height, std::string* error) {
  if (error == nullptr || window == nullptr || width == 0U || height == 0U) {
    if (error) *error = "invalid D3D12 canvas target";
    return false;
  }
  ComPtr<IDXGIFactory6> factory;
  HRESULT result = CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));
  if (FAILED(result)) { *error = HrError(result, "CreateDXGIFactory2"); return false; }
  ComPtr<IDXGIAdapter1> adapter;
  for (UINT index = 0;; ++index) {
    ComPtr<IDXGIAdapter1> candidate;
    result = factory->EnumAdapterByGpuPreference(index,
        DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&candidate));
    if (result == DXGI_ERROR_NOT_FOUND) break;
    if (FAILED(result)) continue;
    DXGI_ADAPTER_DESC1 description{};
    candidate->GetDesc1(&description);
    if ((description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) continue;
    if (SUCCEEDED(D3D12CreateDevice(candidate.Get(), D3D_FEATURE_LEVEL_11_0,
                                    IID_PPV_ARGS(&impl_->device)))) {
      adapter = candidate;
      impl_->info.adapter_description = Narrow(description.Description);
      impl_->info.vendor_id = description.VendorId;
      impl_->info.device_id = description.DeviceId;
      LARGE_INTEGER driver{};
      if (SUCCEEDED(candidate->CheckInterfaceSupport(__uuidof(IDXGIDevice),
                                                     &driver))) {
        impl_->info.driver_version = static_cast<std::uint64_t>(driver.QuadPart);
      }
      break;
    }
  }
  if (adapter == nullptr || impl_->device == nullptr) {
    *error = "no hardware D3D12 adapter found";
    return false;
  }
  D3D12_COMMAND_QUEUE_DESC queue_description{};
  queue_description.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  result = impl_->device->CreateCommandQueue(&queue_description,
                                              IID_PPV_ARGS(&impl_->queue));
  if (FAILED(result)) { *error = HrError(result, "CreateCommandQueue"); return false; }
  DXGI_SWAP_CHAIN_DESC1 swap_description{};
  swap_description.Width = width; swap_description.Height = height;
  swap_description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  swap_description.BufferCount = 2; swap_description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swap_description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  swap_description.SampleDesc.Count = 1;
  ComPtr<IDXGISwapChain1> swap_chain;
  result = factory->CreateSwapChainForHwnd(impl_->queue.Get(), window,
      &swap_description, nullptr, nullptr, &swap_chain);
  if (FAILED(result)) { *error = HrError(result, "CreateSwapChainForHwnd"); return false; }
  result = swap_chain.As(&impl_->swap_chain);
  if (FAILED(result)) { *error = HrError(result, "Query IDXGISwapChain3"); return false; }
  D3D12_DESCRIPTOR_HEAP_DESC heap_description{};
  heap_description.NumDescriptors = 2; heap_description.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  result = impl_->device->CreateDescriptorHeap(&heap_description,
                                                IID_PPV_ARGS(&impl_->rtv_heap));
  if (FAILED(result)) { *error = HrError(result, "CreateDescriptorHeap"); return false; }
  impl_->rtv_stride = impl_->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  D3D12_CPU_DESCRIPTOR_HANDLE handle = impl_->rtv_heap->GetCPUDescriptorHandleForHeapStart();
  for (UINT index = 0; index < 2; ++index) {
    result = impl_->swap_chain->GetBuffer(index, IID_PPV_ARGS(&impl_->buffers[index]));
    if (FAILED(result)) { *error = HrError(result, "GetSwapChainBuffer"); return false; }
    impl_->device->CreateRenderTargetView(impl_->buffers[index].Get(), nullptr, handle);
    handle.ptr += impl_->rtv_stride;
  }
#if defined(CANVAS_POC05_HAS_SKIA)
  GrD3DBackendContext backend;
  backend.fAdapter.retain(adapter.Get());
  backend.fDevice.retain(impl_->device.Get());
  backend.fQueue.retain(impl_->queue.Get());
  impl_->skia_context = GrDirectContexts::MakeD3D(backend);
  if (impl_->skia_context == nullptr) {
    *error = "Skia failed to create Ganesh D3D12 context";
    return false;
  }
  for (UINT index = 0; index < 2; ++index) {
    GrD3DTextureResourceInfo resource_info(
        impl_->buffers[index].Get(), nullptr, D3D12_RESOURCE_STATE_PRESENT,
        DXGI_FORMAT_B8G8R8A8_UNORM, 1U, 1U,
        DXGI_STANDARD_MULTISAMPLE_QUALITY_PATTERN);
    const GrBackendRenderTarget target = GrBackendRenderTargets::MakeD3D(
        static_cast<int>(width), static_cast<int>(height), resource_info);
    impl_->skia_surfaces[index] = SkSurfaces::WrapBackendRenderTarget(
        impl_->skia_context.get(), target, kTopLeft_GrSurfaceOrigin,
        kBGRA_8888_SkColorType, SkColorSpace::MakeSRGB(), nullptr);
    if (impl_->skia_surfaces[index] == nullptr) {
      *error = "Skia failed to wrap D3D12 swap-chain back buffer";
      return false;
    }
  }
  impl_->info.skia_enabled = true;
#if defined(CANVAS_POC05_HAS_POC03_SCENE)
  try {
    // Temporary POC-05 Windows validation bridge.  Match the Android RN/Fabric
    // runner exactly while the product Runtime C ABI scene bridge is pending.
    impl_->document = std::make_unique<canvas::poc03::Document>(
        canvas::poc03::GenerateDocument(
            {100000U, UINT64_C(0x43414e5641533035), 1000U, 32.0F}));
    impl_->scene = std::make_unique<canvas::poc03::RuntimeScene>(
        canvas::poc03::SceneCompiler().CompileFull(*impl_->document));
    impl_->info.poc03_scene_bridge_enabled = true;
    impl_->info.poc03_scene_active_count = static_cast<std::uint32_t>(
        impl_->scene->active_count());
  } catch (const std::exception& exception) {
    *error = std::string("POC-03 100K scene initialization failed: ") +
             exception.what();
    return false;
  }
#endif
#endif
  result = impl_->device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                  IID_PPV_ARGS(&impl_->allocator));
  if (FAILED(result)) { *error = HrError(result, "CreateCommandAllocator"); return false; }
  result = impl_->device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
      impl_->allocator.Get(), nullptr, IID_PPV_ARGS(&impl_->list));
  if (FAILED(result)) { *error = HrError(result, "CreateCommandList"); return false; }
  impl_->list->Close();
  result = impl_->device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&impl_->fence));
  if (FAILED(result)) { *error = HrError(result, "CreateFence"); return false; }
  impl_->fence_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (impl_->fence_event == nullptr) { *error = "CreateEventW failed for D3D12 fence"; return false; }
  impl_->window = window; impl_->info.width_pixels = width; impl_->info.height_pixels = height;
  return true;
}

bool D3D12Canvas::render(std::string* error) {
  if (error == nullptr || impl_->swap_chain == nullptr) {
    if (error) *error = "D3D12 canvas is not initialized";
    return false;
  }
  HRESULT result = S_OK;
  const UINT index = impl_->swap_chain->GetCurrentBackBufferIndex();
  ++impl_->info.render_count;
#if defined(CANVAS_POC05_HAS_SKIA)
  if (impl_->skia_surfaces[index] == nullptr) {
    *error = "Skia swap-chain surface is unavailable";
    return false;
  }
  SkCanvas* canvas = impl_->skia_surfaces[index]->getCanvas();
#if defined(CANVAS_POC05_HAS_POC03_SCENE)
  if (impl_->scene == nullptr) {
    *error = "POC-03 100K RuntimeScene is unavailable";
    return false;
  }
  const std::uint64_t view_revision = ++impl_->frame_index;
  const float dpr = 1.0F;
  const canvas::poc03::ViewState view{
      1U,
      view_revision,
      1U,
      canvas::poc03::Bounds{
          0.0F, 0.0F,
          static_cast<float>(impl_->info.width_pixels) / dpr,
          static_cast<float>(impl_->info.height_pixels) / dpr},
      1.0F,
      dpr,
      impl_->info.width_pixels,
      impl_->info.height_pixels};
  const canvas::poc03::ViewQueryResult query =
      canvas::poc03::QueryView(*impl_->scene, view, std::nullopt);
  const canvas::poc03::FrameGraph frame =
      canvas::poc03::BuildFrame(*impl_->scene, query, {});
  canvas::poc03::DrawLargeScene(*canvas, *impl_->scene, view, frame);

  // Keep a deterministic readback marker outside the generated scene so the
  // physical run proves that Skia rendered into the D3D12 swap-chain buffer.
  SkPaint probe_paint;
  probe_paint.setAntiAlias(false);
  probe_paint.setColor(SkColorSetARGB(255, 37, 199, 166));
  canvas->drawRect(SkRect::MakeXYWH(0.0F, 0.0F, 4.0F, 4.0F), probe_paint);
#else
  // Diagnostic fallback used only by builds that deliberately omit the
  // temporary private POC-03 validation bridge.
  canvas->clear(SkColorSetARGB(255, 8, 18, 40));
  SkPaint paint;
  paint.setAntiAlias(false);
  for (int x = 0; x < static_cast<int>(impl_->info.width_pixels); x += 40) {
    paint.setColor(SkColorSetARGB(255, 18, 42, 78));
    canvas->drawRect(SkRect::MakeXYWH(static_cast<float>(x), 0.0F, 1.0F,
                                      static_cast<float>(impl_->info.height_pixels)),
                     paint);
  }
  for (int y = 0; y < static_cast<int>(impl_->info.height_pixels); y += 40) {
    paint.setColor(SkColorSetARGB(255, 18, 42, 78));
    canvas->drawRect(SkRect::MakeXYWH(0.0F, static_cast<float>(y),
                                      static_cast<float>(impl_->info.width_pixels),
                                      1.0F),
                     paint);
  }
  paint.setColor(SkColorSetARGB(255, 37, 199, 166));
  canvas->drawRect(SkRect::MakeXYWH(24.0F, 24.0F, 320.0F, 6.0F), paint);
  paint.setColor(SkColorSetARGB(255, 255, 184, 77));
  canvas->drawRect(SkRect::MakeXYWH(24.0F, 44.0F, 180.0F, 4.0F), paint);
  paint.setAntiAlias(true);
  paint.setColor(SkColorSetARGB(255, 37, 199, 166));
  canvas->drawCircle(72.0F, 176.0F, 34.0F, paint);
  paint.setColor(SkColorSetARGB(255, 255, 184, 77));
  canvas->drawCircle(136.0F, 176.0F, 22.0F, paint);
  paint.setColor(SkColorSetARGB(255, 255, 92, 122));
  const float marker_x =
      220.0F + static_cast<float>((impl_->frame_index++ * 3U) % 240U);
  canvas->drawCircle(marker_x, 176.0F, 12.0F, paint);
  paint.setColor(SkColorSetARGB(255, 230, 242, 255));
  SkFont font;
  font.setSize(28.0F);
  canvas->drawString("Skia Ganesh / D3D12", 24.0F, 92.0F, font, paint);
  paint.setColor(SkColorSetARGB(255, 144, 190, 255));
  font.setSize(16.0F);
  canvas->drawString("RNW Fabric CanvasSurface", 24.0F, 120.0F, font, paint);
#endif
  if (!impl_->info.skia_content_probe_passed) {
    std::array<std::uint8_t, 4> pixel{};
    const SkImageInfo probe_info = SkImageInfo::Make(
        1, 1, kRGBA_8888_SkColorType, kPremul_SkAlphaType,
        SkColorSpace::MakeSRGB());
    if (!impl_->skia_surfaces[index]->readPixels(probe_info, pixel.data(),
                                                  pixel.size(),
#if defined(CANVAS_POC05_HAS_POC03_SCENE)
                                                  1, 1) ||
#else
                                                  24, 24) ||
#endif
        pixel[0] != 37U || pixel[1] != 199U || pixel[2] != 166U ||
        pixel[3] != 255U) {
      *error = "Skia D3D12 content readback probe failed";
      return false;
    }
    impl_->info.skia_content_probe_passed = true;
  }
  GrFlushInfo flush_info{};
  impl_->skia_context->flush(impl_->skia_surfaces[index].get(),
                             SkSurfaces::BackendSurfaceAccess::kPresent,
                             flush_info);
  impl_->skia_context->submit();
#else
  result = impl_->allocator->Reset();
  if (FAILED(result)) { *error = HrError(result, "ResetCommandAllocator"); return false; }
  result = impl_->list->Reset(impl_->allocator.Get(), nullptr);
  if (FAILED(result)) { *error = HrError(result, "ResetCommandList"); return false; }
  D3D12_RESOURCE_BARRIER barrier{};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = impl_->buffers[index].Get();
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  impl_->list->ResourceBarrier(1, &barrier);
  D3D12_CPU_DESCRIPTOR_HANDLE handle =
      impl_->rtv_heap->GetCPUDescriptorHandleForHeapStart();
  handle.ptr += static_cast<SIZE_T>(index) * impl_->rtv_stride;
  const float clear[] = {0.035F, 0.075F, 0.14F, 1.0F};
  impl_->list->ClearRenderTargetView(handle, clear, 0, nullptr);
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
  impl_->list->ResourceBarrier(1, &barrier);
  result = impl_->list->Close();
  if (FAILED(result)) { *error = HrError(result, "CloseCommandList"); return false; }
  ID3D12CommandList* lists[] = {impl_->list.Get()}; impl_->queue->ExecuteCommandLists(1, lists);
#endif
  result = impl_->swap_chain->Present(1, 0);
  if (FAILED(result)) { *error = HrError(result, "Present"); return false; }
  ++impl_->fence_value; result = impl_->queue->Signal(impl_->fence.Get(), impl_->fence_value);
  if (FAILED(result)) { *error = HrError(result, "Signal"); return false; }
  if (impl_->fence->GetCompletedValue() < impl_->fence_value) {
    impl_->fence->SetEventOnCompletion(impl_->fence_value, impl_->fence_event);
    WaitForSingleObject(impl_->fence_event, INFINITE);
  }
  return true;
}

const D3D12CanvasInfo& D3D12Canvas::info() const { return impl_->info; }

bool D3D12Canvas::skia_enabled() const {
#if defined(CANVAS_POC05_HAS_SKIA)
  return impl_->skia_context != nullptr && impl_->skia_surfaces[0] != nullptr;
#else
  return false;
#endif
}
}  // namespace canvas::poc05::windows
