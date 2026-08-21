#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <stdexcept>
#include <sstream>
#include <string>
#include <vector>

#include "windows_rnw_fabric.h"
#include "windows_d3d12_canvas.h"
#include "windows_webview2_backend.h"

namespace {

using canvas::poc05::RuntimeViewFrame;
using canvas::poc05::SurfaceKind;
using canvas::poc05::windows::WebView2BackendOptions;
using canvas::poc05::windows::WebView2OverlayBackend;
using canvas::poc05::windows::WindowsRnwFabricExternalSurfaceHost;
using canvas::poc05::windows::D3D12Canvas;

HWND g_window = nullptr;
WindowsRnwFabricExternalSurfaceHost* g_host = nullptr;
D3D12Canvas* g_canvas = nullptr;
RuntimeViewFrame g_frame{};
std::vector<double> g_frame_ms;
std::chrono::steady_clock::time_point g_last_tick;
bool g_quit_requested = false;
std::string g_render_error;

CanvasStatus Project(CanvasViewHandle view, CanvasPointF point,
                     CanvasPointF* out) {
  if (out == nullptr || view == CANVAS_INVALID_HANDLE) return 1;
  out->x = point.x;
  out->y = point.y;
  return kCanvasStatusOk;
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam,
                            LPARAM lparam) {
  switch (message) {
    case WM_PAINT:
      ValidateRect(window, nullptr);
      return 0;
    case WM_TIMER: {
      if (g_host == nullptr) return 0;
      const auto now = std::chrono::steady_clock::now();
      if (g_last_tick.time_since_epoch().count() != 0) {
        g_frame_ms.push_back(std::chrono::duration<double, std::milli>(
                                 now - g_last_tick)
                                 .count());
      }
      g_last_tick = now;
      ++g_frame.frameRevision;
      std::string error;
      g_host->publishFrame(g_frame, &error);
      if (g_canvas != nullptr && !g_canvas->render(&error)) {
        g_render_error = error;
        DestroyWindow(window);
      }
      return 0;
    }
    case WM_SIZE:
      g_frame.surface.width_pixels = LOWORD(lparam);
      g_frame.surface.height_pixels = HIWORD(lparam);
      return 0;
    case WM_DESTROY:
      KillTimer(window, 1);
      g_quit_requested = true;
      PostQuitMessage(0);
      return 0;
    default:
      return DefWindowProcW(window, message, wparam, lparam);
  }
}

HWND CreateWindowForRunner() {
  WNDCLASSW klass{};
  klass.hInstance = GetModuleHandleW(nullptr);
  klass.lpfnWndProc = WindowProc;
  klass.lpszClassName = L"CanvasPoc05WindowsNativePeer";
  klass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  RegisterClassW(&klass);
  return CreateWindowExW(0, klass.lpszClassName,
                        L"Canvas POC-05 Windows RNW/Fabric + WebView2",
                        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                        1024, 720, nullptr, nullptr, klass.hInstance, nullptr);
}

std::string JsonNumber(double value) {
  std::ostringstream stream;
  stream << std::fixed << std::setprecision(3) << value;
  return stream.str();
}

double Percentile(std::vector<double> samples, double percentile) {
  if (samples.empty()) return 0.0;
  std::sort(samples.begin(), samples.end());
  const size_t index = static_cast<size_t>(
      std::ceil(percentile * static_cast<double>(samples.size()))) - 1U;
  return samples[std::min(index, samples.size() - 1U)];
}

void WriteEvidence(const std::filesystem::path& output,
                   const WebView2OverlayBackend& backend,
                   const WindowsRnwFabricExternalSurfaceHost& host,
                   const canvas::poc05::windows::D3D12CanvasInfo& canvas_info) {
  std::ofstream stream(output, std::ios::binary);
  if (!stream) throw std::runtime_error("cannot write evidence output");
  MEMORYSTATUSEX memory{};
  memory.dwLength = sizeof(memory);
  GlobalMemoryStatusEx(&memory);
  OSVERSIONINFOEXW os{};
  os.dwOSVersionInfoSize = sizeof(os);
  using RtlGetVersionFunction = LONG(WINAPI*)(OSVERSIONINFOEXW*);
  const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  const auto get_version = ntdll == nullptr
                               ? nullptr
                               : reinterpret_cast<RtlGetVersionFunction>(
                                     GetProcAddress(ntdll, "RtlGetVersion"));
  if (get_version != nullptr) get_version(&os);
  stream << "{\n"
         << "  \"adapter\": \"windows-rnw-fabric-webview2-peer\",\n"
         << "  \"webview2_runtime\": \"" << backend.runtimeVersion()
         << "\",\n"
         << "  \"webview2_sdk\": \"1.0.2592.51\",\n"
         << "  \"gpu\": {\n"
         << "    \"description\": \"" << canvas_info.adapter_description
         << "\",\n    \"vendor_id\": " << canvas_info.vendor_id
         << ",\n    \"device_id\": " << canvas_info.device_id
         << ",\n    \"driver_version\": " << canvas_info.driver_version
         << "\n  },\n"
         << "  \"window_dpi\": " << GetDpiForWindow(g_window) << ",\n"
         << "  \"canvas_renderer\": \""
         << (canvas_info.skia_enabled ? "skia-ganesh-d3d12" : "d3d12-clear-fallback")
         << "\",\n"
         << "  \"skia_visible\": "
         << (canvas_info.skia_content_probe_passed ? "true" : "false")
         << ",\n"
         << "  \"skia_content_probe\": \""
         << (canvas_info.skia_content_probe_passed ? "rgba-readback-passed"
                                                   : "not-run")
         << "\",\n"
         << "  \"windows_version\": \"" << os.dwMajorVersion << "."
         << os.dwMinorVersion << "." << os.dwBuildNumber << "\",\n"
         << "  \"memory_load_percent\": " << memory.dwMemoryLoad << ",\n"
         << "  \"frames\": " << g_frame_ms.size() << ",\n"
         << "  \"frame_interval_ms\": {\n"
         << "    \"p50\": " << JsonNumber(Percentile(g_frame_ms, 0.50))
         << ",\n    \"p95\": "
         << JsonNumber(Percentile(g_frame_ms, 0.95)) << ",\n    \"p99\": "
         << JsonNumber(Percentile(g_frame_ms, 0.99)) << ",\n    \"max\": "
         << JsonNumber(g_frame_ms.empty()
                           ? 0.0
                           : *std::max_element(g_frame_ms.begin(),
                                               g_frame_ms.end()))
         << "\n  },\n"
         << "  \"registry\": {\n"
         << "    \"create_count\": " << host.diagnostics().createCount
         << ",\n    \"placement_count\": "
         << host.diagnostics().placementCount
         << ",\n    \"stale_frame_count\": "
         << host.diagnostics().staleFrameCount
         << ",\n    \"destroy_count\": "
         << host.diagnostics().destroyCount
         << ",\n    \"backend_failure_count\": "
         << host.diagnostics().backendFailureCount
         << ",\n    \"active_surface_count\": "
         << host.diagnostics().activeSurfaceCount << "\n  },\n"
         << "  \"js_stall_probe_ms\": 100,\n"
         << "  \"status\": \"physical-runner-ready\"\n}\n";
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  try {
    g_window = CreateWindowForRunner();
    if (g_window == nullptr) throw std::runtime_error("CreateWindow failed");
    ShowWindow(g_window, SW_SHOW);
    UpdateWindow(g_window);

    WebView2BackendOptions options;
    options.owner_window = g_window;
    options.user_data_folder =
        (std::filesystem::temp_directory_path() / L"canvas-poc05-webview2").wstring();
    WebView2OverlayBackend backend(std::move(options));
    std::string error;
    D3D12Canvas canvas;
    if (!canvas.initialize(g_window, 1000, 680, &error))
      throw std::runtime_error(error);
    g_canvas = &canvas;
    MSG message{};
    backend.initialize(&error);
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(10);
    while (!backend.initialized() && std::chrono::steady_clock::now() < deadline) {
      while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
      }
      Sleep(1);
    }
    if (!backend.initialized())
      throw std::runtime_error(error.empty() ? "WebView2 initialization timed out" : error);

    WindowsRnwFabricExternalSurfaceHost host(&Project, backend);
    g_host = &host;
    g_frame.view = 1;
    g_frame.frameRevision = 1;
    g_frame.camera.struct_size = sizeof(CanvasCameraStateV1);
    g_frame.camera.abi_version = CANVAS_RUNTIME_ABI_VERSION;
    g_frame.camera.scale = 1.0F;
    g_frame.camera.viewport_revision = 1;
    g_frame.surface.struct_size = sizeof(CanvasSurfaceStateV1);
    g_frame.surface.abi_version = CANVAS_RUNTIME_ABI_VERSION;
    g_frame.surface.width_pixels = 1000;
    g_frame.surface.height_pixels = 680;
    g_frame.surface.device_pixel_ratio = 1.0F;
    g_frame.surface.target_generation = 1;
    if (!host.mount(101, SurfaceKind::kWebView, CanvasRectF{80, 110, 360, 200}, 1,
                    &error) ||
        !host.mount(102, SurfaceKind::kVideo, CanvasRectF{500, 340, 360, 200}, 1,
                    &error)) {
      throw std::runtime_error(error);
    }
    host.setReady(101, &error);
    host.setReady(102, &error);
    host.publishFrame(g_frame, &error);
    SetTimer(g_window, 1, 16, nullptr);
    const auto run_until = std::chrono::steady_clock::now() +
                           std::chrono::seconds(8);
    bool stall_sent = false;
    while (!g_quit_requested && std::chrono::steady_clock::now() < run_until) {
      while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT) break;
        TranslateMessage(&message);
        DispatchMessageW(&message);
      }
      Sleep(1);
    if (!stall_sent && std::chrono::steady_clock::now() +
                             std::chrono::seconds(6) > run_until) {
        backend.runJsStallProbe(100);
        stall_sent = true;
      }
    }
    if (!g_render_error.empty()) throw std::runtime_error(g_render_error);
    // Exercise failure placeholder/recovery and target-generation reset on
    // the same native owner thread used by the RNW component.
    host.setFailed(101, &error);
    host.publishFrame(g_frame, &error);
    host.recover(101, &error);
    host.publishFrame(g_frame, &error);
    g_frame.surface.target_generation = 2;
    ++g_frame.frameRevision;
    host.publishFrame(g_frame, &error);
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    g_host = nullptr;
    g_canvas = nullptr;
    WriteEvidence(L"poc05-windows-rnw-webview2.json", backend, host,
                  canvas.info());
    DestroyWindow(g_window);
    return 0;
  } catch (const std::exception& exception) {
    std::ofstream error_file("poc05-windows-rnw-webview2-error.txt",
                             std::ios::binary);
    error_file << exception.what() << "\n";
    return 1;
  }
}
