#include "pch.h"
#include "AxiomHybridSurfaceView.h"

#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>


namespace AxiomPoc05Codegen {
namespace {

using canvas::poc05::RuntimeViewFrame;
using canvas::poc05::SurfaceKind;
using canvas::poc05::windows::D3D12Canvas;
using canvas::poc05::windows::WebView2BackendOptions;
using canvas::poc05::windows::WebView2OverlayBackend;
using canvas::poc05::windows::WindowsRnwFabricExternalSurfaceHost;

constexpr wchar_t kWindowClass[] = L"AxiomPoc05RnwCanvasSurface";
constexpr std::uint64_t kWebSurfaceId = 101;
constexpr std::uint64_t kVideoSurfaceId = 102;

CanvasStatus Project(CanvasViewHandle view, CanvasPointF point,
                     CanvasPointF* out) {
  if (view == CANVAS_INVALID_HANDLE || out == nullptr) return 1;
  *out = point;
  return kCanvasStatusOk;
}

std::wstring UserDataPath() {
  wchar_t buffer[MAX_PATH]{};
  const DWORD length = GetTempPathW(MAX_PATH, buffer);
  std::wstring result(buffer, length);
  result += L"AxiomPoc05RnwWebView2";
  return result;
}

std::uint32_t Pixels(float value, float scale) {
  if (!std::isfinite(value) || value <= 0.0F) return 1U;
  return std::max(1U, static_cast<std::uint32_t>(std::ceil(value * scale)));
}

}  // namespace

ATOM AxiomHybridSurfaceView::RegisterChildWindowClass() {
  static ATOM atom = [] {
    WNDCLASSW window_class{};
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.lpfnWndProc = &AxiomHybridSurfaceView::ChildWindowProc;
    window_class.lpszClassName = kWindowClass;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    return RegisterClassW(&window_class);
  }();
  return atom;
}

LRESULT CALLBACK AxiomHybridSurfaceView::ChildWindowProc(HWND window,
                                                          UINT message,
                                                          WPARAM wparam,
                                                          LPARAM lparam) {
  auto* self = reinterpret_cast<AxiomHybridSurfaceView*>(
      GetWindowLongPtrW(window, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
    self = static_cast<AxiomHybridSurfaceView*>(create->lpCreateParams);
    SetWindowLongPtrW(window, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(self));
  }
  if (self != nullptr && message == WM_TIMER && wparam == 1U) {
    self->EnsureOverlays();
    if (self->canvas_ != nullptr) {
      std::string error;
      if (!self->canvas_->render(&error)) self->ReportError(error);
      if (!self->evidence_written_ && self->overlays_mounted_ &&
          self->canvas_->info().render_count >= 10U) {
        self->WriteValidationEvidence();
      }
    }
    self->PublishFrame();
    return 0;
  }
  if (message == WM_ERASEBKGND) return 1;
  if (message == WM_NCDESTROY) {
    SetWindowLongPtrW(window, GWLP_USERDATA, 0);
  }
  return DefWindowProcW(window, message, wparam, lparam);
}

void AxiomHybridSurfaceView::Initialize(
    const winrt::Microsoft::ReactNative::ComponentView& view) noexcept {
  parent_window_ = ResolveParentWindow(view);
  frame_.view = 1;
  frame_.camera.struct_size = sizeof(CanvasCameraStateV1);
  frame_.camera.abi_version = CANVAS_RUNTIME_ABI_VERSION;
  frame_.camera.scale = 1.0F;
  frame_.camera.viewport_revision = next_viewport_revision_++;
  frame_.surface.struct_size = sizeof(CanvasSurfaceStateV1);
  frame_.surface.abi_version = CANVAS_RUNTIME_ABI_VERSION;
  frame_.surface.device_pixel_ratio = 1.0F;
  frame_.surface.target_generation = 1;
  frame_.frameRevision = 1;
  RegisterChildWindowClass();
}

HWND AxiomHybridSurfaceView::ResolveParentWindow(
    const winrt::Microsoft::ReactNative::ComponentView& view) noexcept {
  // RNW exposes the actual parenting HWND through the Composition interop
  // contract.  It is more reliable than reading the top-level window id when
  // this component is hosted below a portal or another native component.
  try {
    auto interop = view.as<
        ::Microsoft::ReactNative::Composition::Experimental::IComponentViewInterop>();
    if (interop != nullptr) {
      if (HWND hwnd = interop->GetHwndForParenting(); hwnd != nullptr) {
        return hwnd;
      }
    }
  } catch (...) {
    // Fall back to the documented top-level id for older RNW hosts.
  }
  return reinterpret_cast<HWND>(
      winrt::Microsoft::ReactNative::ReactCoreInjection::
          GetTopLevelWindowId(view.ReactContext().Properties()));
}

winrt::Windows::Foundation::Rect AxiomHybridSurfaceView::AbsoluteLayout(
    const winrt::Microsoft::ReactNative::ComponentView& view,
    const winrt::Windows::Foundation::Rect& local) noexcept {
  auto result = local;
  auto parent = view.Parent();
  while (parent != nullptr) {
    const auto parent_frame = parent.LayoutMetrics().Frame;
    result.X += parent_frame.X;
    result.Y += parent_frame.Y;
    parent = parent.Parent();
  }
  return result;
}

winrt::Microsoft::UI::Composition::Visual AxiomHybridSurfaceView::CreateVisual(
    const winrt::Microsoft::ReactNative::ComponentView& view) noexcept {
  auto visual = view.as<winrt::Microsoft::ReactNative::Composition::ComponentView>()
                    .Compositor()
                    .CreateSpriteVisual();
  visual.Opacity(0.0F);
  return visual;
}

void AxiomHybridSurfaceView::EnsureNativeSurface(
    const winrt::Microsoft::ReactNative::ComponentView& view,
    const winrt::Microsoft::ReactNative::LayoutMetrics& metrics) noexcept {
  if (native_ready_ || parent_window_ == nullptr || metrics.Frame.Width <= 0.0F ||
      metrics.Frame.Height <= 0.0F) {
    return;
  }
  const float scale = metrics.PointScaleFactor <= 0.0F
                          ? 1.0F
                          : metrics.PointScaleFactor;
  canvas_window_ = CreateWindowExW(
      0, kWindowClass, L"Axiom POC-05 CanvasSurface",
      WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
      0, 0, static_cast<int>(std::ceil(metrics.Frame.Width * scale)),
      static_cast<int>(std::ceil(metrics.Frame.Height * scale)), parent_window_, nullptr,
      GetModuleHandleW(nullptr), this);
  if (canvas_window_ == nullptr) {
    ReportError("CreateWindowExW failed for RNW CanvasSurface");
    return;
  }
  canvas_ = std::make_unique<D3D12Canvas>();
  std::string error;
  if (!canvas_->initialize(canvas_window_, Pixels(metrics.Frame.Width, scale),
                           Pixels(metrics.Frame.Height, scale), &error)) {
    ReportError(error);
    return;
  }
  WebView2BackendOptions options;
  options.owner_window = canvas_window_;
  options.user_data_folder = UserDataPath();
  backend_ = std::make_unique<WebView2OverlayBackend>(std::move(options));
  host_ = std::make_unique<WindowsRnwFabricExternalSurfaceHost>(
      &Project, *backend_);
  const bool webview_ready = backend_->initialize(&error);
  if (!webview_ready && error != "WebView2 environment is still initializing") {
    webview_failed_ = true;
    ReportError(error);
  }
  frame_.surface.width_pixels = Pixels(metrics.Frame.Width, scale);
  frame_.surface.height_pixels = Pixels(metrics.Frame.Height, scale);
  frame_.surface.device_pixel_ratio = scale;
  frame_.surface.color_space = kCanvasColorSpaceSrgb;
  frame_.surface.orientation = kCanvasSurfaceOrientationIdentity;
  native_ready_ = true;
  SetTimer(canvas_window_, 1, 16, nullptr);
  ApplyProps();
  PublishFrame();
  static_cast<void>(view);
}

void AxiomHybridSurfaceView::UpdateLayoutMetrics(
    const winrt::Microsoft::ReactNative::ComponentView& view,
    const winrt::Microsoft::ReactNative::LayoutMetrics& metrics,
    const winrt::Microsoft::ReactNative::LayoutMetrics&) noexcept {
  layout_ = metrics.Frame;
  EnsureNativeSurface(view, metrics);
  if (canvas_window_ == nullptr) return;
  const auto absolute = AbsoluteLayout(view, metrics.Frame);
  const float scale = metrics.PointScaleFactor <= 0.0F
                          ? 1.0F
                          : metrics.PointScaleFactor;
  const int x = static_cast<int>(std::lround(absolute.X * scale));
  const int y = static_cast<int>(std::lround(absolute.Y * scale));
  const int width = std::max(1, static_cast<int>(std::lround(absolute.Width * scale)));
  const int height = std::max(1, static_cast<int>(std::lround(absolute.Height * scale)));
  SetWindowPos(canvas_window_, HWND_TOP, x, y, width, height,
               SWP_NOACTIVATE | SWP_SHOWWINDOW);
  if (canvas_ != nullptr &&
      (canvas_->info().width_pixels != static_cast<std::uint32_t>(width) ||
       canvas_->info().height_pixels != static_cast<std::uint32_t>(height))) {
    canvas_.reset();
    auto replacement = std::make_unique<D3D12Canvas>();
    std::string error;
    if (replacement->initialize(canvas_window_, static_cast<std::uint32_t>(width),
                                static_cast<std::uint32_t>(height), &error)) {
      canvas_ = std::move(replacement);
    } else {
      ReportError(error);
    }
  }
  frame_.surface.width_pixels = Pixels(metrics.Frame.Width, scale);
  frame_.surface.height_pixels = Pixels(metrics.Frame.Height, scale);
  frame_.surface.device_pixel_ratio = scale;
  ++frame_.camera.viewport_revision;
  ++frame_.frameRevision;
  PublishFrame();
}

void AxiomHybridSurfaceView::UpdateProps(
    const winrt::Microsoft::ReactNative::ComponentView& view,
    const winrt::com_ptr<AxiomHybridSurfaceProps>& new_props,
    const winrt::com_ptr<AxiomHybridSurfaceProps>& old_props) noexcept {
  BaseAxiomHybridSurface<AxiomHybridSurfaceView>::UpdateProps(view, new_props,
                                                               old_props);
  ApplyProps();
  PublishFrame();
}

void AxiomHybridSurfaceView::ApplyProps() noexcept {
  const auto props = Props();
  if (props == nullptr) return;
  web_visible_ = props->webVisible;
  failure_mode_ = props->failureMode.value_or(false);
  active_page_ = std::max(1, props->activePage);
  lifecycle_generation_ = std::max(1, props->lifecycleGeneration);
  frame_.surface.target_generation = static_cast<std::uint32_t>(lifecycle_generation_);
  if (!host_) return;
  host_->setActivePage(static_cast<std::uint64_t>(active_page_));
  host_->setBackgrounded(false);
  if (!overlays_mounted_) return;
  std::string error;
  if (!host_->update(kWebSurfaceId, {80.0F, 80.0F, 360.0F, 190.0F},
                     1.0F, !web_visible_, &error)) {
    ReportError(error);
    return;
  }
  if (failure_mode_) {
    if (!host_->setFailed(kWebSurfaceId, &error)) ReportError(error);
  } else {
    if (!host_->recover(kWebSurfaceId, &error)) ReportError(error);
  }
}

void AxiomHybridSurfaceView::EnsureOverlays() noexcept {
  if (!native_ready_ || overlays_mounted_ || webview_failed_ ||
      !backend_->initialized()) return;
  std::string error;
  if (!host_->mount(kWebSurfaceId, SurfaceKind::kWebView,
                    {80.0F, 80.0F, 360.0F, 190.0F}, active_page_, &error) ||
      !host_->mount(kVideoSurfaceId, SurfaceKind::kVideo,
                    {500.0F, 300.0F, 360.0F, 190.0F}, active_page_, &error)) {
    ReportError(error);
    return;
  }
  if (!host_->setReady(kWebSurfaceId, &error) ||
      !host_->setReady(kVideoSurfaceId, &error)) {
    ReportError(error);
    return;
  }
  overlays_mounted_ = true;
  ApplyProps();
}

void AxiomHybridSurfaceView::PublishFrame() noexcept {
  if (!host_) return;
  std::string error;
  ++frame_.frameRevision;
  if (!host_->publishFrame(frame_, &error)) ReportError(error);
}

void AxiomHybridSurfaceView::WriteValidationEvidence() noexcept {
  if (canvas_ == nullptr || host_ == nullptr || evidence_written_) return;
  const auto& info = canvas_->info();
  wchar_t module_path[MAX_PATH]{};
  const DWORD module_length = GetModuleFileNameW(
      nullptr, module_path, static_cast<DWORD>(std::size(module_path)));
  std::filesystem::path report_path =
      module_length == 0U ? std::filesystem::path(L"poc05-windows-rnw-scene.json")
                          : std::filesystem::path(module_path).parent_path() /
                                L"poc05-windows-rnw-scene.json";
  std::ofstream report(report_path, std::ios::binary);
  if (!report) return;
  report << "{\n"
         << "  \"schema\": \"canvas.poc05.windows-rnw-scene.v1\",\n"
         << "  \"status\": \"physical-shell-running\",\n"
         << "  \"canvas_renderer\": \""
         << (info.skia_enabled ? "skia-ganesh-d3d12" : "unavailable")
         << "\",\n"
         << "  \"skia_content_probe\": \""
         << (info.skia_content_probe_passed ? "rgba-readback-passed" : "failed")
         << "\",\n"
         << "  \"poc03_scene_bridge\": {\n"
         << "    \"mode\": \"private-cpp-validation-bridge\",\n"
         << "    \"node_count\": " << info.poc03_scene_active_count << ",\n"
         << "    \"seed\": \"0x43414e5641533035\",\n"
         << "    \"columns\": 1000,\n"
         << "    \"cell_size\": 32.0\n"
         << "  },\n"
         << "  \"gpu\": {\n"
         << "    \"description\": \"" << info.adapter_description << "\",\n"
         << "    \"vendor_id\": " << info.vendor_id << ",\n"
         << "    \"device_id\": " << info.device_id << ",\n"
         << "    \"driver_version\": " << info.driver_version << "\n"
         << "  },\n"
         << "  \"window\": {\n"
         << "    \"dpi\": " << GetDpiForWindow(canvas_window_) << ",\n"
         << "    \"width_pixels\": " << info.width_pixels << ",\n"
         << "    \"height_pixels\": " << info.height_pixels << "\n"
         << "  },\n"
         << "  \"render_count\": " << info.render_count << ",\n"
         << "  \"js_stall_requested_ms\": 100,\n"
         << "  \"registry\": {\n"
         << "    \"create_count\": " << host_->diagnostics().createCount << ",\n"
         << "    \"placement_count\": " << host_->diagnostics().placementCount << ",\n"
         << "    \"stale_frame_count\": " << host_->diagnostics().staleFrameCount << ",\n"
         << "    \"backend_failure_count\": " << host_->diagnostics().backendFailureCount << ",\n"
         << "    \"active_surface_count\": " << host_->diagnostics().activeSurfaceCount << "\n"
         << "  }\n"
         << "}\n";
  if (report.good()) evidence_written_ = true;
}

void AxiomHybridSurfaceView::ReportError(const std::string& error) noexcept {
  if (!error.empty()) {
    std::wstring wide(error.begin(), error.end());
    OutputDebugStringW((L"AxiomPoc05 RNW CanvasSurface: " + wide + L"\n").c_str());
  }
}

AxiomHybridSurfaceView::~AxiomHybridSurfaceView() {
  if (canvas_window_ != nullptr) KillTimer(canvas_window_, 1);
  host_.reset();
  backend_.reset();
  canvas_.reset();
  if (canvas_window_ != nullptr) DestroyWindow(canvas_window_);
}

}  // namespace AxiomPoc05Codegen
