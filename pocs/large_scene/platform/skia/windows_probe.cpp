#include <windows.h>

#include <psapi.h>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "include/core/SkColorSpace.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkSurface.h"
#include "include/gpu/ganesh/GrBackendSurface.h"
#include "include/gpu/ganesh/GrDirectContext.h"
#include "include/gpu/ganesh/GrTypes.h"
#include "include/gpu/ganesh/SkSurfaceGanesh.h"
#include "include/gpu/ganesh/d3d/GrD3DBackendContext.h"
#include "include/gpu/ganesh/d3d/GrD3DBackendSurface.h"
#include "include/gpu/ganesh/d3d/GrD3DDirectContext.h"
#include "include/gpu/ganesh/d3d/GrD3DTypes.h"
#include "skia_large_scene_renderer.h"
#include "canvas/poc03/ink_integration.h"

using Microsoft::WRL::ComPtr;

namespace {

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
constexpr uint32_t kWidth = 1280U;
constexpr uint32_t kHeight = 720U;
constexpr uint32_t kTraceFrames = 600U;
constexpr uint32_t kWarmupFrames = 60U;

struct Options {
  bool hardware = false;
  bool interactive = false;
  uint32_t seconds = 0U;
  uint32_t nodes = 100000U;
  fs::path output;
  fs::path trace_output;
};

struct FrameTrace {
  uint64_t frame = 0U;
  uint32_t trace_frame = 0U;
  uint64_t request_us = 0U;
  uint64_t callback_us = 0U;
  uint64_t render_submit_us = 0U;
  uint64_t present_us = 0U;
  uint64_t visible_us = 0U;
  double render_ms = 0.0;
  uint32_t dxgi_present_count = 0U;
  uint32_t dxgi_present_refresh_count = 0U;
  uint32_t dxgi_sync_refresh_count = 0U;
  uint64_t dxgi_sync_qpc = 0U;
  uint32_t dxgi_composition_mode = 0U;
  bool dxgi_statistics_valid = false;
};

struct DisplayInfo {
  uint32_t width = 0U;
  uint32_t height = 0U;
  uint32_t refresh_rate_hz = 0U;
  double dpr = 1.0;
};

struct SceneState {
  canvas::poc03::Document document;
  canvas::poc03::RuntimeScene scene;
  canvas::poc03::RuntimeScene oracle;
  std::unique_ptr<canvas::poc03::InkGeometryStore> ink_geometry;
  canvas::poc03::TileCache* interaction_cache = nullptr;
  bool oracle_dirty = false;
};

uint64_t ParseUnsigned(std::string_view value, std::string_view name) {
  size_t parsed = 0U;
  const uint64_t result = std::stoull(std::string(value), &parsed, 0);
  if (value.empty() || parsed != value.size()) {
    throw std::invalid_argument(std::string(name) + " is not an integer");
  }
  return result;
}

Options ParseOptions(int argc, char **argv) {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    const size_t split = argument.find('=');
    const std::string_view key = argument.substr(0U, split);
    const std::string_view value = split == std::string_view::npos
                                       ? std::string_view{}
                                       : argument.substr(split + 1U);
    if (key == "--hardware" && split == std::string_view::npos) {
      options.hardware = true;
    } else if (key == "--interactive" && split == std::string_view::npos) {
      options.interactive = true;
    } else if (key == "--seconds") {
      options.seconds = static_cast<uint32_t>(ParseUnsigned(value, key));
    } else if (key == "--nodes") {
      options.nodes = static_cast<uint32_t>(ParseUnsigned(value, key));
    } else if (key == "--output") {
      options.output = fs::path(value);
    } else if (key == "--trace-output") {
      options.trace_output = fs::path(value);
    } else {
      throw std::invalid_argument("unknown argument: " + std::string(argument));
    }
  }
  if (!options.hardware && options.seconds != 0U) {
    throw std::invalid_argument("--seconds requires --hardware");
  }
  if (options.hardware && options.seconds == 0U) {
    throw std::invalid_argument("physical hardware mode requires --seconds");
  }
  if (options.interactive && !options.hardware) {
    throw std::invalid_argument("--interactive requires --hardware");
  }
  if (options.nodes != 1000U && options.nodes != 10000U &&
      options.nodes != 50000U && options.nodes != 100000U) {
    throw std::invalid_argument("--nodes must be 1000, 10000, 50000, or 100000");
  }
  return options;
}

void Check(HRESULT result, std::string_view action) {
  if (FAILED(result)) {
    std::ostringstream message;
    message << action << " failed: 0x" << std::hex
            << static_cast<uint32_t>(result);
    throw std::runtime_error(message.str());
  }
}

std::string Narrow(const wchar_t *value) {
  if (value == nullptr)
    return {};
  const int required =
      WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
  if (required <= 1)
    return {};
  std::string result(static_cast<size_t>(required), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), required, nullptr,
                      nullptr);
  result.pop_back();
  return result;
}

std::string JsonEscape(std::string_view value) {
  std::string result;
  for (const char character : value) {
    switch (character) {
    case '\\':
      result += "\\\\";
      break;
    case '"':
      result += "\\\"";
      break;
    case '\n':
      result += "\\n";
      break;
    case '\r':
      result += "\\r";
      break;
    case '\t':
      result += "\\t";
      break;
    default:
      result.push_back(character);
      break;
    }
  }
  return result;
}

uint64_t MicrosSince(Clock::time_point origin) {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() -
                                                            origin)
          .count());
}

double Percentile(std::vector<double> values, double percentile) {
  if (values.empty())
    return 0.0;
  std::sort(values.begin(), values.end());
  const size_t rank = static_cast<size_t>(
      std::ceil(percentile * static_cast<double>(values.size())));
  return values[(std::min)(values.size() - 1U,
                           rank == 0U ? size_t{0U} : rank - 1U)];
}

uint64_t PeakWorkingSetBytes() {
  PROCESS_MEMORY_COUNTERS_EX counters{};
  counters.cb = sizeof(counters);
  if (!GetProcessMemoryInfo(
          GetCurrentProcess(),
          reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters),
          sizeof(counters))) {
    return 0U;
  }
  return static_cast<uint64_t>(counters.PeakWorkingSetSize);
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam,
                            LPARAM lparam);

struct InteractiveInput {
  bool selecting = false;
  uint64_t selected_id = 0U;
  UINT pointer_id = 0U;
  float offset_x = 0.0F;
  float offset_y = 0.0F;
};

HWND g_input_window = nullptr;
SceneState* g_input_scene = nullptr;
InteractiveInput* g_input_state = nullptr;

void ApplyWindowToolInput(HWND window, UINT message, WPARAM wparam,
                          LPARAM lparam, SceneState* state,
                          InteractiveInput* input) {
  static_cast<void>(window);
  if (state == nullptr || input == nullptr ||
      (message != WM_LBUTTONDOWN && message != WM_MOUSEMOVE &&
       message != WM_LBUTTONUP && message != WM_POINTERDOWN &&
       message != WM_POINTERUPDATE && message != WM_POINTERUP &&
       message != WM_POINTERCAPTURECHANGED && message != WM_KEYDOWN)) return;
  if (message == WM_KEYDOWN) {
    // The current physical shell only exposes Select/Drag. Keep the tool
    // switch explicit so later Vector/Dab input cannot be mistaken for a
    // normal-node mutation.
    if (wparam == '1' || wparam == VK_ESCAPE) {
      input->selecting = false;
      input->selected_id = 0U;
    }
    return;
  }
  const float x = static_cast<float>(static_cast<int16_t>(LOWORD(lparam)));
  const float y = static_cast<float>(static_cast<int16_t>(HIWORD(lparam)));
  const canvas::poc03::ViewState view{
      1U, 1U, 1U, canvas::poc03::Bounds{0.0F, 0.0F, kWidth, kHeight},
      1.0F, 1.0F, kWidth, kHeight};
  const bool pointer_message = message == WM_POINTERDOWN ||
                               message == WM_POINTERUPDATE ||
                               message == WM_POINTERUP ||
                               message == WM_POINTERCAPTURECHANGED;
  const bool pointer_begin = message == WM_POINTERDOWN;
  const bool pointer_move = message == WM_POINTERUPDATE;
  const bool pointer_end = message == WM_POINTERUP ||
                           message == WM_POINTERCAPTURECHANGED;
  if (message == WM_LBUTTONDOWN || pointer_begin) {
    if (pointer_message) {
      input->pointer_id = GET_POINTERID_WPARAM(wparam);
      SetCapture(window);
    }
    const auto hits = canvas::poc03::HitTest(state->scene, view, x, y, 12.0F);
    std::optional<uint64_t> selected;
    for (const uint64_t id : hits) {
      const auto candidate = state->scene.SlotFor(id);
      const auto node = candidate ? state->scene.RecordAt(*candidate)
                                  : std::nullopt;
      if (node && !node->locked && node->type != canvas::poc03::NodeType::kStroke) {
        selected = id;
        break;
      }
    }
    if (!selected) return;
    const auto node = state->document.Find(*selected);
    if (!node) return;
    input->selecting = true;
    input->selected_id = *selected;
    input->offset_x = x - node->bounds.left;
    input->offset_y = y - node->bounds.top;
  } else if ((message == WM_MOUSEMOVE && input->selecting &&
              (wparam & MK_LBUTTON) != 0) ||
             (pointer_move && input->selecting &&
              GET_POINTERID_WPARAM(wparam) == input->pointer_id)) {
    const auto current = state->document.Find(input->selected_id);
    if (!current) return;
    canvas::poc03::NodeRecord moved = *current;
    const float width = moved.bounds.right - moved.bounds.left;
    const float height = moved.bounds.bottom - moved.bounds.top;
    moved.bounds = {x - input->offset_x, y - input->offset_y,
                    x - input->offset_x + width, y - input->offset_y + height};
    canvas::poc03::ChangeSet changes;
    canvas::poc03::CompileDiagnostics diagnostics;
    std::string error;
    const canvas::poc03::SceneCompiler compiler;
    if (!state->document.Apply({canvas::poc03::OperationKind::kUpdate,
                                moved.id, moved},
                               &changes, &error) ||
        !compiler.ApplyIncremental(state->document, changes, &state->scene,
                                   &diagnostics, &error)) {
      OutputDebugStringA(("POC03 select/drag failed: " + error + "\n").c_str());
      // Document::Apply is authoritative. If compilation fails, repair the
      // runtime scene immediately so the next input cannot render stale data.
      state->scene = compiler.CompileFull(state->document);
    } else {
      state->oracle_dirty = true;
      if (state->interaction_cache != nullptr) {
        state->interaction_cache->InvalidateWorld(
            1U, diagnostics.authoritative_world_dirty, 256.0F);
      }
    }
  } else if (message == WM_LBUTTONUP || pointer_end) {
    if (pointer_end && input->selecting &&
        GET_POINTERID_WPARAM(wparam) != input->pointer_id) {
      return;
    }
    input->selecting = false;
    input->selected_id = 0U;
    input->pointer_id = 0U;
    if (GetCapture() == window) {
      ReleaseCapture();
    }
  }
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam,
                            LPARAM lparam) {
  if (window == g_input_window && g_input_scene != nullptr &&
      g_input_state != nullptr) {
    ApplyWindowToolInput(window, message, wparam, lparam, g_input_scene,
                         g_input_state);
  }
  if (message == WM_DESTROY) {
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProcW(window, message, wparam, lparam);
}

HWND CreateBenchmarkWindow() {
  const wchar_t *class_name = L"CanvasPoc03PhysicalWindow";
  WNDCLASSW window_class{};
  window_class.lpfnWndProc = WindowProc;
  window_class.hInstance = GetModuleHandleW(nullptr);
  window_class.lpszClassName = class_name;
  window_class.hCursor = LoadCursor(nullptr, IDC_ARROW);
  if (RegisterClassW(&window_class) == 0U &&
      GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    throw std::runtime_error("RegisterClassW failed");
  }
  RECT bounds{0, 0, static_cast<LONG>(kWidth), static_cast<LONG>(kHeight)};
  AdjustWindowRect(&bounds, WS_OVERLAPPEDWINDOW, FALSE);
  HWND window = CreateWindowExW(
      0U, class_name, L"Canvas POC-03 100K D3D12 Physical Benchmark",
      WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
      bounds.right - bounds.left, bounds.bottom - bounds.top, nullptr, nullptr,
      window_class.hInstance, nullptr);
  if (window == nullptr) {
    throw std::runtime_error("CreateWindowExW failed");
  }
  ShowWindow(window, SW_SHOW);
  UpdateWindow(window);
  return window;
}

bool PumpMessages() {
  MSG message{};
  while (PeekMessageW(&message, nullptr, 0U, 0U, PM_REMOVE)) {
    if (message.message == WM_QUIT)
      return false;
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  return true;
}

DisplayInfo QueryDisplayInfo(HWND window) {
  DisplayInfo result;
  const HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
  MONITORINFOEXW monitor_info{};
  monitor_info.cbSize = sizeof(monitor_info);
  if (GetMonitorInfoW(monitor, &monitor_info)) {
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    if (EnumDisplaySettingsW(monitor_info.szDevice, ENUM_CURRENT_SETTINGS,
                             &mode)) {
      result.width = mode.dmPelsWidth;
      result.height = mode.dmPelsHeight;
      result.refresh_rate_hz = mode.dmDisplayFrequency;
    }
  }
  result.dpr = static_cast<double>(GetDpiForWindow(window)) / 96.0;
  return result;
}

SceneState BuildScene(uint32_t base_nodes) {
  using namespace canvas::poc03;
  SceneState state;
  state.ink_geometry = std::make_unique<InkGeometryStore>();
  TileCache cache(64U * 1024U * 1024U);
  DeterministicFrameScheduler scheduler;
  IntegratedScaleReport scale;
  std::string error;
  const uint32_t historical_strokes = base_nodes / 5U;
  if (!BuildIntegratedScale({base_nodes, historical_strokes,
                             0x43414e5641533033ULL},
                            &state.document, &state.scene,
                            state.ink_geometry.get(), &cache, &scheduler,
                            &scale, &error)) {
    throw std::runtime_error("integrated scale failed: " + error);
  }
  IntegratedActionReport actions;
  if (!RunIntegratedActionCycle(base_nodes, historical_strokes, &state.document,
                                &state.scene, state.ink_geometry.get(), &cache,
                                &scheduler, &actions, &error)) {
    throw std::runtime_error("integrated action cycle failed: " + error);
  }
  SceneCompiler compiler;
  state.oracle = compiler.CompileFull(state.document);
  if (state.scene.Digest() != state.oracle.Digest()) {
    throw std::runtime_error("incremental/full scene digest differs");
  }
  return state;
}

canvas::poc03::ViewState TraceView(uint32_t frame) {
  using namespace canvas::poc03;
  const uint32_t trace_frame = frame % kTraceFrames;
  const float zoom = 0.75F + static_cast<float>(trace_frame % 8U) * 0.125F;
  const float pan_x =
      static_cast<float>((static_cast<uint64_t>(trace_frame) * 37U) % 28000U);
  const float pan_y =
      static_cast<float>((static_cast<uint64_t>(trace_frame) * 17U) % 2200U);
  return ViewState{1U,
                   static_cast<uint64_t>(frame) + 1U,
                   1U,
                   Bounds{pan_x, pan_y,
                          pan_x + static_cast<float>(kWidth) / zoom,
                          pan_y + static_cast<float>(kHeight) / zoom},
                   zoom,
                   1.0F,
                   kWidth,
                   kHeight};
}

class D3D12Presenter {
public:
  D3D12Presenter(HWND window, bool hardware)
      : window_(window), hardware_(hardware) {
    Check(CreateDXGIFactory2(0U, IID_PPV_ARGS(&factory_)),
          "CreateDXGIFactory2");
    SelectAdapter();
    Check(D3D12CreateDevice(adapter_.Get(), D3D_FEATURE_LEVEL_12_0,
                            IID_PPV_ARGS(&device_)),
          "D3D12CreateDevice");
    D3D12_COMMAND_QUEUE_DESC queue_description{};
    queue_description.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    Check(
        device_->CreateCommandQueue(&queue_description, IID_PPV_ARGS(&queue_)),
        "CreateCommandQueue");
    GrD3DBackendContext backend;
    backend.fAdapter.retain(adapter_.Get());
    backend.fDevice.retain(device_.Get());
    backend.fQueue.retain(queue_.Get());
    context_ = GrDirectContexts::MakeD3D(backend);
    if (!context_)
      throw std::runtime_error("Skia D3D12 context creation failed");
    if (hardware_) {
      CreateSwapChain();
    } else {
      const SkImageInfo info =
          SkImageInfo::Make(static_cast<int>(kWidth), static_cast<int>(kHeight),
                            kRGBA_8888_SkColorType, kPremul_SkAlphaType,
                            SkColorSpace::MakeSRGB());
      offscreen_ =
          SkSurfaces::RenderTarget(context_.get(), skgpu::Budgeted::kNo, info,
                                   0, kTopLeft_GrSurfaceOrigin, nullptr);
      if (!offscreen_) {
        throw std::runtime_error(
            "Skia offscreen D3D12 surface creation failed");
      }
    }
    adapter_->GetDesc1(&description_);
    LARGE_INTEGER driver{};
    if (SUCCEEDED(
            adapter_->CheckInterfaceSupport(__uuidof(IDXGIDevice), &driver))) {
      driver_version_ = static_cast<uint64_t>(driver.QuadPart);
    }
  }

  ~D3D12Presenter() {
    WaitForGpu();
    surfaces_.clear();
    buffers_.clear();
    offscreen_.reset();
    if (context_) {
      context_->abandonContext();
      context_.reset();
    }
    if (frame_latency_waitable_ != nullptr)
      CloseHandle(frame_latency_waitable_);
    if (fence_event_ != nullptr)
      CloseHandle(fence_event_);
  }

  SkSurface *Acquire() {
    if (!hardware_)
      return offscreen_.get();
    if (WaitForSingleObjectEx(frame_latency_waitable_, 10000U, FALSE) !=
        WAIT_OBJECT_0) {
      throw std::runtime_error("DXGI frame-latency wait timed out");
    }
    buffer_index_ = swap_chain_->GetCurrentBackBufferIndex();
    if (fence_->GetCompletedValue() < fence_values_[buffer_index_]) {
      Check(fence_->SetEventOnCompletion(fence_values_[buffer_index_],
                                         fence_event_),
            "SetEventOnCompletion");
      if (WaitForSingleObjectEx(fence_event_, 10000U, FALSE) != WAIT_OBJECT_0) {
        throw std::runtime_error("D3D12 backbuffer fence wait timed out");
      }
    }
    return surfaces_[buffer_index_].get();
  }

  void SubmitAndPresent(SkSurface *surface) {
    if (!hardware_) {
      context_->flushAndSubmit(surface, GrSyncCpu::kYes);
      return;
    }
    GrFlushInfo flush_info{};
    context_->flush(surface, SkSurfaces::BackendSurfaceAccess::kPresent,
                    flush_info);
    context_->submit();
    Check(swap_chain_->Present(1U, 0U), "IDXGISwapChain::Present");
    const uint64_t signal = ++next_fence_value_;
    Check(queue_->Signal(fence_.Get(), signal), "ID3D12CommandQueue::Signal");
    fence_values_[buffer_index_] = signal;
  }

  bool GetFrameStatistics(DXGI_FRAME_STATISTICS *statistics) const {
    return hardware_ && swap_chain_ && statistics != nullptr &&
           SUCCEEDED(swap_chain_->GetFrameStatistics(statistics));
  }

  void FlushForReadback(SkSurface *surface) {
    context_->flushAndSubmit(surface, GrSyncCpu::kYes);
  }

  [[nodiscard]] const DXGI_ADAPTER_DESC1 &description() const {
    return description_;
  }
  [[nodiscard]] uint64_t driver_version() const { return driver_version_; }

private:
  void SelectAdapter() {
    if (!hardware_) {
      Check(factory_->EnumWarpAdapter(IID_PPV_ARGS(&adapter_)),
            "EnumWarpAdapter");
      return;
    }
    for (UINT index = 0U;; ++index) {
      ComPtr<IDXGIAdapter1> candidate;
      const HRESULT result = factory_->EnumAdapterByGpuPreference(
          index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
          IID_PPV_ARGS(&candidate));
      if (result == DXGI_ERROR_NOT_FOUND)
        break;
      Check(result, "EnumAdapterByGpuPreference");
      DXGI_ADAPTER_DESC1 description{};
      candidate->GetDesc1(&description);
      if ((description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0U &&
          SUCCEEDED(D3D12CreateDevice(candidate.Get(), D3D_FEATURE_LEVEL_12_0,
                                      __uuidof(ID3D12Device), nullptr))) {
        adapter_ = candidate;
        break;
      }
    }
    if (!adapter_) {
      throw std::runtime_error(
          "no physical D3D12 feature-level 12 adapter found");
    }
  }

  void CreateSwapChain() {
    DXGI_SWAP_CHAIN_DESC1 description{};
    description.Width = kWidth;
    description.Height = kHeight;
    description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.SampleDesc.Count = 1U;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.BufferCount = 2U;
    description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    description.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
    ComPtr<IDXGISwapChain1> swap_chain;
    Check(factory_->CreateSwapChainForHwnd(queue_.Get(), window_, &description,
                                           nullptr, nullptr, &swap_chain),
          "CreateSwapChainForHwnd");
    Check(factory_->MakeWindowAssociation(window_, DXGI_MWA_NO_ALT_ENTER),
          "MakeWindowAssociation");
    Check(swap_chain.As(&swap_chain_), "IDXGISwapChain3 query");
    ComPtr<IDXGISwapChain2> swap_chain2;
    Check(swap_chain_.As(&swap_chain2), "IDXGISwapChain2 query");
    Check(swap_chain2->SetMaximumFrameLatency(1U), "SetMaximumFrameLatency");
    frame_latency_waitable_ = swap_chain2->GetFrameLatencyWaitableObject();
    if (frame_latency_waitable_ == nullptr) {
      throw std::runtime_error(
          "DXGI frame-latency waitable object unavailable");
    }
    buffers_.resize(2U);
    surfaces_.resize(2U);
    for (UINT index = 0U; index < 2U; ++index) {
      Check(swap_chain_->GetBuffer(index, IID_PPV_ARGS(&buffers_[index])),
            "IDXGISwapChain::GetBuffer");
      GrD3DTextureResourceInfo resource_info(
          buffers_[index].Get(), nullptr, D3D12_RESOURCE_STATE_PRESENT,
          DXGI_FORMAT_R8G8B8A8_UNORM, 1U, 1U,
          DXGI_STANDARD_MULTISAMPLE_QUALITY_PATTERN);
      const GrBackendRenderTarget target = GrBackendRenderTargets::MakeD3D(
          static_cast<int>(kWidth), static_cast<int>(kHeight), resource_info);
      surfaces_[index] = SkSurfaces::WrapBackendRenderTarget(
          context_.get(), target, kTopLeft_GrSurfaceOrigin,
          kRGBA_8888_SkColorType, SkColorSpace::MakeSRGB(), nullptr);
      if (!surfaces_[index]) {
        throw std::runtime_error("Skia swapchain surface wrapping failed");
      }
    }
    Check(
        device_->CreateFence(0U, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)),
        "CreateFence");
    fence_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (fence_event_ == nullptr)
      throw std::runtime_error("CreateEventW failed");
  }

  void WaitForGpu() noexcept {
    if (!queue_ || !fence_ || fence_event_ == nullptr)
      return;
    const uint64_t signal = ++next_fence_value_;
    if (FAILED(queue_->Signal(fence_.Get(), signal)))
      return;
    if (fence_->GetCompletedValue() < signal &&
        SUCCEEDED(fence_->SetEventOnCompletion(signal, fence_event_))) {
      WaitForSingleObjectEx(fence_event_, 10000U, FALSE);
    }
  }

  HWND window_ = nullptr;
  bool hardware_ = false;
  ComPtr<IDXGIFactory6> factory_;
  ComPtr<IDXGIAdapter1> adapter_;
  ComPtr<ID3D12Device> device_;
  ComPtr<ID3D12CommandQueue> queue_;
  ComPtr<IDXGISwapChain3> swap_chain_;
  std::vector<ComPtr<ID3D12Resource>> buffers_;
  std::vector<sk_sp<SkSurface>> surfaces_;
  sk_sp<GrDirectContext> context_;
  sk_sp<SkSurface> offscreen_;
  ComPtr<ID3D12Fence> fence_;
  HANDLE fence_event_ = nullptr;
  HANDLE frame_latency_waitable_ = nullptr;
  std::vector<uint64_t> fence_values_{0U, 0U};
  uint64_t next_fence_value_ = 0U;
  UINT buffer_index_ = 0U;
  DXGI_ADAPTER_DESC1 description_{};
  uint64_t driver_version_ = 0U;
};

void DrawTraceFrame(const canvas::poc03::RuntimeScene &scene, uint32_t frame,
                    canvas::poc03::TileCache *cache, size_t *maximum_candidates,
                    size_t *maximum_visible, SkSurface *surface,
                    const canvas::poc03::InkGeometryStore *ink_geometry) {
  using namespace canvas::poc03;
  const ViewState view = TraceView(frame);
  const ViewQueryResult query = QueryView(scene, view, std::nullopt);
  *maximum_candidates =
      (std::max)(*maximum_candidates, query.candidates.size());
  *maximum_visible = (std::max)(*maximum_visible, query.visible.size());
  if (cache != nullptr) {
    const TileKey key{view.view_id,
                      scene.source_revision(),
                      1U,
                      1U,
                      query.scale_bucket,
                      1U,
                      static_cast<int32_t>(frame % 64U),
                      0};
    if (!cache->Find(key))
      cache->Put(key, 256U * 256U * 4U);
  }
  FrameGraph graph = BuildFrame(scene, query, {});
  const std::string visual_digest = graph.VisualDigest();
  OptimizeFrameGraph(&graph);
  if (graph.VisualDigest() != visual_digest) {
    throw std::runtime_error("frame graph optimization changed visual digest");
  }
  DrawLargeScene(*surface->getCanvas(), scene, view, graph, ink_geometry);
}

void WriteTrace(const fs::path &path, const std::vector<FrameTrace> &traces) {
  if (path.empty())
    return;
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output)
    throw std::runtime_error("could not create native trace output");
  output << std::fixed << std::setprecision(3);
  for (const FrameTrace &trace : traces) {
    output << "{\"frame\":" << trace.frame
           << ",\"trace_frame\":" << trace.trace_frame
           << ",\"request_us\":" << trace.request_us
           << ",\"callback_us\":" << trace.callback_us
           << ",\"render_submit_us\":" << trace.render_submit_us
           << ",\"present_us\":" << trace.present_us
           << ",\"visible_us\":" << trace.visible_us
           << ",\"render_ms\":" << trace.render_ms
           << ",\"dxgi_statistics_valid\":"
           << (trace.dxgi_statistics_valid ? "true" : "false")
           << ",\"dxgi_present_count\":" << trace.dxgi_present_count
           << ",\"dxgi_present_refresh_count\":"
           << trace.dxgi_present_refresh_count
           << ",\"dxgi_sync_refresh_count\":" << trace.dxgi_sync_refresh_count
           << ",\"dxgi_sync_qpc\":" << trace.dxgi_sync_qpc
           << ",\"dxgi_composition_mode\":" << trace.dxgi_composition_mode
           << "}\n";
  }
}

int Run(const Options &options) {
  using namespace canvas::poc03;
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  HWND window = options.hardware ? CreateBenchmarkWindow() : nullptr;
  const DisplayInfo display =
      options.hardware ? QueryDisplayInfo(window) : DisplayInfo{};
  D3D12Presenter presenter(window, options.hardware);
  SceneState state = BuildScene(options.nodes);
  InteractiveInput input_state;
  g_input_window = options.interactive ? window : nullptr;
  g_input_scene = options.interactive ? &state : nullptr;
  g_input_state = options.interactive ? &input_state : nullptr;
  TileCache cache(64U * 1024U * 1024U);
  state.interaction_cache = &cache;
  size_t maximum_candidates = 0U;
  size_t maximum_visible = 0U;
  std::vector<FrameTrace> traces;
  std::vector<double> render_times;

  for (uint32_t warmup = 0U; warmup < (options.hardware ? kWarmupFrames : 0U);
       ++warmup) {
    if (!PumpMessages()) {
      throw std::runtime_error("benchmark window closed during warmup");
    }
    SkSurface *surface = presenter.Acquire();
    DrawTraceFrame(state.scene, warmup, &cache, &maximum_candidates,
                   &maximum_visible, surface, state.ink_geometry.get());
    presenter.SubmitAndPresent(surface);
  }

  const auto trace_origin = Clock::now();
  const auto deadline = trace_origin + std::chrono::seconds(options.seconds);
  uint64_t frame = 0U;
  while (options.hardware && Clock::now() < deadline) {
    if (!PumpMessages()) {
      throw std::runtime_error("benchmark window closed before duration gate");
    }
    FrameTrace trace;
    trace.frame = frame;
    trace.trace_frame = static_cast<uint32_t>(frame % kTraceFrames);
    trace.request_us = MicrosSince(trace_origin);
    SkSurface *surface = presenter.Acquire();
    trace.callback_us = MicrosSince(trace_origin);
    if (!traces.empty())
      traces.back().visible_us = trace.callback_us;
    const auto render_start = Clock::now();
    DrawTraceFrame(state.scene, trace.trace_frame, &cache, &maximum_candidates,
                   &maximum_visible, surface, state.ink_geometry.get());
    presenter.SubmitAndPresent(surface);
    trace.render_submit_us = MicrosSince(trace_origin);
    trace.present_us = trace.render_submit_us;
    DXGI_FRAME_STATISTICS statistics{};
    if (presenter.GetFrameStatistics(&statistics)) {
      trace.dxgi_statistics_valid = true;
      trace.dxgi_present_count = statistics.PresentCount;
      trace.dxgi_present_refresh_count = statistics.PresentRefreshCount;
      trace.dxgi_sync_refresh_count = statistics.SyncRefreshCount;
      trace.dxgi_sync_qpc =
          static_cast<uint64_t>(statistics.SyncQPCTime.QuadPart);
      trace.dxgi_composition_mode = 0U;
    }
    trace.render_ms =
        std::chrono::duration<double, std::milli>(Clock::now() - render_start)
            .count();
    render_times.push_back(trace.render_ms);
    traces.push_back(trace);
    ++frame;
  }
  const uint32_t oracle_frame = kTraceFrames - 1U;
  if (state.oracle_dirty) {
    state.oracle = SceneCompiler().CompileFull(state.document);
    state.oracle_dirty = false;
  }
  SkSurface *validation_surface = presenter.Acquire();
  if (options.hardware && !traces.empty()) {
    traces.back().visible_us = MicrosSince(trace_origin);
  }
  DrawTraceFrame(state.scene, oracle_frame, nullptr, &maximum_candidates,
                 &maximum_visible, validation_surface,
                 state.ink_geometry.get());
  presenter.FlushForReadback(validation_surface);
  std::vector<uint8_t> incremental_rgba;
  if (!ReadRgba(*validation_surface, kWidth, kHeight, &incremental_rgba)) {
    throw std::runtime_error("incremental D3D12 readback failed");
  }
  const std::string pixel_digest = PixelDigest(incremental_rgba);
  DrawTraceFrame(state.oracle, oracle_frame, nullptr, &maximum_candidates,
                 &maximum_visible, validation_surface,
                 state.ink_geometry.get());
  presenter.FlushForReadback(validation_surface);
  std::vector<uint8_t> oracle_rgba;
  if (!ReadRgba(*validation_surface, kWidth, kHeight, &oracle_rgba)) {
    throw std::runtime_error("oracle D3D12 readback failed");
  }
  const bool visual_equivalent = incremental_rgba == oracle_rgba;

  std::vector<double> intervals;
  std::vector<double> presentation_intervals;
  uint64_t missed_presentations = 0U;
  uint64_t dxgi_missed_presentations = 0U;
  LARGE_INTEGER qpc_frequency{};
  QueryPerformanceFrequency(&qpc_frequency);
  const double nominal_interval =
      display.refresh_rate_hz == 0U ? 0.0 : 1000.0 / display.refresh_rate_hz;
  for (size_t index = 1U; index < traces.size(); ++index) {
    const double interval =
        static_cast<double>(traces[index].callback_us -
                            traces[index - 1U].callback_us) /
        1000.0;
    intervals.push_back(interval);
    if (nominal_interval > 0.0 && interval > nominal_interval * 1.5) {
      const double missed =
          (std::max)(1.0, std::round(interval / nominal_interval)) - 1.0;
      missed_presentations += static_cast<uint64_t>(missed);
    }
  }
  for (size_t index = 1U; index < traces.size(); ++index) {
    const FrameTrace &previous = traces[index - 1U];
    const FrameTrace &current = traces[index];
    if (!previous.dxgi_statistics_valid || !current.dxgi_statistics_valid ||
        current.dxgi_present_count <= previous.dxgi_present_count ||
        current.dxgi_sync_qpc <= previous.dxgi_sync_qpc ||
        qpc_frequency.QuadPart <= 0) {
      continue;
    }
    presentation_intervals.push_back(
        static_cast<double>(current.dxgi_sync_qpc - previous.dxgi_sync_qpc) *
        1000.0 / static_cast<double>(qpc_frequency.QuadPart));
    if (current.dxgi_present_refresh_count >
        previous.dxgi_present_refresh_count + 1U) {
      dxgi_missed_presentations += current.dxgi_present_refresh_count -
                                   previous.dxgi_present_refresh_count - 1U;
    }
  }
  const double frame_p50 = Percentile(intervals, 0.50);
  const double frame_p95 = Percentile(intervals, 0.95);
  const double frame_p99 = Percentile(intervals, 0.99);
  const double frame_max =
      intervals.empty() ? 0.0
                        : *std::max_element(intervals.begin(), intervals.end());
  const double render_p50 = Percentile(render_times, 0.50);
  const double render_p95 = Percentile(render_times, 0.95);
  const double render_p99 = Percentile(render_times, 0.99);
  const double render_max =
      render_times.empty()
          ? 0.0
          : *std::max_element(render_times.begin(), render_times.end());
  const double present_p50 = Percentile(presentation_intervals, 0.50);
  const double present_p95 = Percentile(presentation_intervals, 0.95);
  const double present_p99 = Percentile(presentation_intervals, 0.99);
  const double present_max =
      presentation_intervals.empty()
          ? 0.0
          : *std::max_element(presentation_intervals.begin(),
                              presentation_intervals.end());
  const double measured_refresh_rate_hz =
      present_p50 > 0.0 ? 1000.0 / present_p50 : 0.0;
  const uint64_t document_bytes = state.document.EstimatedBytes();
  const uint64_t scene_bytes = state.scene.EstimatedBytes();
  const uint64_t cache_bytes = cache.stats().bytes;
  const uint64_t runtime_bytes = document_bytes + scene_bytes + cache_bytes;
  const DXGI_ADAPTER_DESC1 &adapter = presenter.description();

  std::ostringstream result;
  result << std::fixed << std::setprecision(3) << "{\n"
         << "  \"schema_version\": 1,\n"
         << "  \"platform\": \"windows\",\n"
         << "  \"backend\": \"ganesh-d3d12-"
         << (options.hardware ? "hardware" : "warp") << "\",\n"
         << "  \"hardware\": " << (options.hardware ? "true" : "false") << ",\n"
         << "  \"generator_algorithm_version\": 1,\n"
         << "  \"seed_hex\": \"0x43414e5641533033\",\n"
         << "  \"nodes\": " << options.nodes << ",\n"
         << "  \"historical_strokes\": " << options.nodes / 5U << ",\n"
         << "  \"ink_document_digest\": \""
         << state.ink_geometry->document().Digest() << "\",\n"
         << "  \"columns\": 1000,\n"
         << "  \"cell_size\": 32.0,\n"
         << "  \"document_digest\": \"" << state.document.Digest() << "\",\n"
         << "  \"scene_digest\": \"" << state.scene.Digest() << "\",\n"
         << "  \"oracle_scene_digest\": \"" << state.oracle.Digest() << "\",\n"
         << "  \"full_incremental_equivalent\": "
         << (state.scene.Digest() == state.oracle.Digest() ? "true" : "false")
         << ",\n"
         << "  \"pixel_digest\": \"" << pixel_digest << "\",\n"
         << "  \"visual_equivalent\": "
         << (visual_equivalent ? "true" : "false") << ",\n"
         << "  \"duration_seconds\": " << options.seconds << ",\n"
         << "  \"warmup_frames\": " << (options.hardware ? kWarmupFrames : 0U)
         << ",\n"
         << "  \"frames\": " << traces.size() << ",\n"
         << "  \"frame_p50_ms\": " << render_p50 << ",\n"
         << "  \"frame_p95_ms\": " << render_p95 << ",\n"
         << "  \"frame_p99_ms\": " << render_p99 << ",\n"
         << "  \"frame_max_ms\": " << render_max << ",\n"
         << "  \"callback_interval_p50_ms\": " << frame_p50 << ",\n"
         << "  \"callback_interval_p95_ms\": " << frame_p95 << ",\n"
         << "  \"callback_interval_p99_ms\": " << frame_p99 << ",\n"
         << "  \"callback_interval_max_ms\": " << frame_max << ",\n"
         << "  \"presentation_interval_sample_count\": "
         << presentation_intervals.size() << ",\n"
         << "  \"presentation_p50_ms\": " << present_p50 << ",\n"
         << "  \"presentation_p95_ms\": " << present_p95 << ",\n"
         << "  \"presentation_p99_ms\": " << present_p99 << ",\n"
         << "  \"presentation_max_ms\": " << present_max << ",\n"
         << "  \"render_p50_ms\": " << render_p50 << ",\n"
         << "  \"render_p95_ms\": " << render_p95 << ",\n"
         << "  \"render_p99_ms\": " << render_p99 << ",\n"
         << "  \"render_max_ms\": " << render_max << ",\n"
         << "  \"refresh_rate_hz\": " << display.refresh_rate_hz << ",\n"
         << "  \"measured_refresh_rate_hz\": " << measured_refresh_rate_hz
         << ",\n"
         << "  \"missed_presentations\": " << dxgi_missed_presentations << ",\n"
         << "  \"callback_missed_presentations\": " << missed_presentations
         << ",\n"
         << "  \"dxgi_missed_presentations\": " << dxgi_missed_presentations
         << ",\n"
         << "  \"maximum_candidates\": " << maximum_candidates << ",\n"
         << "  \"maximum_visible\": " << maximum_visible << ",\n"
         << "  \"surface_width_px\": " << kWidth << ",\n"
         << "  \"surface_height_px\": " << kHeight << ",\n"
         << "  \"dpr\": 1.0,\n"
         << "  \"display_width_px\": " << display.width << ",\n"
         << "  \"display_height_px\": " << display.height << ",\n"
         << "  \"display_dpr\": " << display.dpr << ",\n"
         << "  \"document_bytes\": " << document_bytes << ",\n"
         << "  \"scene_bytes\": " << scene_bytes << ",\n"
         << "  \"cache_peak_bytes\": " << cache_bytes << ",\n"
         << "  \"runtime_scene_cache_peak_bytes\": " << runtime_bytes << ",\n"
         << "  \"source_asset_bytes\": 0,\n"
         << "  \"process_peak_working_set_bytes\": " << PeakWorkingSetBytes()
         << ",\n"
         << "  \"adapter_description\": \""
         << JsonEscape(Narrow(adapter.Description)) << "\",\n"
         << "  \"vendor_id\": " << adapter.VendorId << ",\n"
         << "  \"device_id\": " << adapter.DeviceId << ",\n"
         << "  \"driver_version_raw\": " << presenter.driver_version() << ",\n"
         << "  \"timing_source\": "
            "\"dxgi-frame-latency-waitable-object-qpc-vsync\",\n"
         << "  \"present_sampling_method\": \"IDXGISwapChain "
            "GetFrameStatistics SyncQPCTime and PresentRefreshCount\"\n"
         << "}\n";

  WriteTrace(options.trace_output, traces);
  if (!options.output.empty()) {
    std::ofstream output(options.output, std::ios::binary | std::ios::trunc);
    if (!output)
      throw std::runtime_error("could not create native result output");
    output << result.str();
  }
  std::cout << result.str();
  if (window != nullptr)
    DestroyWindow(window);
  g_input_window = nullptr;
  g_input_scene = nullptr;
  g_input_state = nullptr;
  const bool performance_pass =
      !options.hardware || (render_p95 <= 16.7 && render_p99 <= 33.3);
  return visual_equivalent && state.scene.Digest() == state.oracle.Digest() &&
                 maximum_candidates <= 5000U && performance_pass
             ? EXIT_SUCCESS
             : EXIT_FAILURE;
}

} // namespace

int main(int argc, char **argv) {
  try {
    return Run(ParseOptions(argc, argv));
  } catch (const std::exception &error) {
    std::cerr << "canvas_poc03_windows_probe: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
