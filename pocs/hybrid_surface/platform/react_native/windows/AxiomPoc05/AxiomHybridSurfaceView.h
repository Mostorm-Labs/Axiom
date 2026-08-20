#pragma once

#include <memory>
#include <string>

#include "AxiomHybridSurface.g.h"
#include <ComponentView.Experimental.interop.h>
#include "windows_d3d12_canvas.h"
#include "windows_rnw_fabric.h"
#include "windows_webview2_backend.h"

namespace AxiomPoc05Codegen {

class AxiomHybridSurfaceView
    : public winrt::implements<AxiomHybridSurfaceView,
                               winrt::Windows::Foundation::IInspectable>,
      public BaseAxiomHybridSurface<AxiomHybridSurfaceView> {
 public:
  AxiomHybridSurfaceView() = default;
  ~AxiomHybridSurfaceView();

  void Initialize(
      const winrt::Microsoft::ReactNative::ComponentView& view) noexcept;
  winrt::Microsoft::UI::Composition::Visual CreateVisual(
      const winrt::Microsoft::ReactNative::ComponentView& view) noexcept;
  void UpdateProps(
      const winrt::Microsoft::ReactNative::ComponentView& view,
      const winrt::com_ptr<AxiomHybridSurfaceProps>& new_props,
      const winrt::com_ptr<AxiomHybridSurfaceProps>& old_props) noexcept;
  void UpdateLayoutMetrics(
      const winrt::Microsoft::ReactNative::ComponentView& view,
      const winrt::Microsoft::ReactNative::LayoutMetrics& new_metrics,
      const winrt::Microsoft::ReactNative::LayoutMetrics& old_metrics) noexcept;

 private:
  static LRESULT CALLBACK ChildWindowProc(HWND window, UINT message,
                                           WPARAM wparam, LPARAM lparam);
  static ATOM RegisterChildWindowClass();

  void EnsureNativeSurface(
      const winrt::Microsoft::ReactNative::ComponentView& view,
      const winrt::Microsoft::ReactNative::LayoutMetrics& metrics) noexcept;
  void PublishFrame() noexcept;
  void ApplyProps() noexcept;
  void EnsureOverlays() noexcept;
  void WriteValidationEvidence() noexcept;
  HWND ResolveParentWindow(
      const winrt::Microsoft::ReactNative::ComponentView& view) noexcept;
  winrt::Windows::Foundation::Rect AbsoluteLayout(
      const winrt::Microsoft::ReactNative::ComponentView& view,
      const winrt::Windows::Foundation::Rect& local) noexcept;
  void ReportError(const std::string& error) noexcept;

  HWND parent_window_ = nullptr;
  HWND canvas_window_ = nullptr;
  std::unique_ptr<canvas::poc05::windows::D3D12Canvas> canvas_;
  std::unique_ptr<canvas::poc05::windows::WebView2OverlayBackend> backend_;
  std::unique_ptr<canvas::poc05::windows::WindowsRnwFabricExternalSurfaceHost>
      host_;
  canvas::poc05::RuntimeViewFrame frame_{};
  winrt::Windows::Foundation::Rect layout_{};
  bool native_ready_ = false;
  bool overlays_mounted_ = false;
  bool webview_failed_ = false;
  bool web_visible_ = true;
  bool failure_mode_ = false;
  std::int32_t active_page_ = 1;
  std::int32_t lifecycle_generation_ = 1;
  std::uint64_t next_viewport_revision_ = 1;
  bool evidence_written_ = false;
};

}  // namespace AxiomPoc05Codegen
