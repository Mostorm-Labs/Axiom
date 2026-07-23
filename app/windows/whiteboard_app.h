#pragma once

#include "platform/windows/dcomp_host.h"
#include "platform/windows/embedded_mouse_session.h"

#include "canvas/input/pointer_sample.h"
#include "canvas/document/document.h"
#include "canvas/input/input_router.h"
#include "canvas/stroke/stroke_builder.h"
#include "platform/windows/skia_d3d12_context.h"
#include "platform/windows/skia_swap_chain_layer.h"
#include "platform/windows/webview2_surface.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace canvas::windows {

struct WhiteboardRunOptions {
  bool selfTestLayers = false;
  bool selfTestEmbedded = false;
  std::optional<std::wstring> videoPath;
};

class WhiteboardApp {
 public:
  int run(HINSTANCE instance, int commandShow,
          const WhiteboardRunOptions& options = {});

  static LRESULT CALLBACK windowProc(HWND window, UINT message,
                                     WPARAM wParam, LPARAM lParam);

 private:
  // Task 10 seam: samples stay native and are consumed by the future stroke
  // pipeline. No Electron IPC or rendering work belongs on this path.
  HRESULT onPointerSample(const input::PointerSample& sample);
  HRESULT onPointerSamples(std::vector<input::PointerSample> samples);
  HRESULT cancelActivePointer(std::uint64_t pointerId);
  HRESULT forwardMouseToEmbedded(HWND window, UINT message, WPARAM wParam,
                                 LPARAM lParam);
  HRESULT forwardTouchToEmbedded(UINT message, UINT32 pointerId,
                                 const input::PointerSample& sample);
  HRESULT cancelEmbeddedTouch(UINT32 pointerId);
  std::optional<document::NodeId> hitEmbedded(core::Vec2 point) const;
  WebView2Surface* embeddedWebView(
      const std::optional<document::NodeId>& nodeId) const;
  document::LayerClass activeDocumentLayer() const;
  SkiaSwapChainLayer& activeSwapChainLayer();
  bool populateSelfTestDocument();
  bool populateEmbeddedSelfTestDocument();

  struct HostedWebView {
    document::NodeId nodeId;
    std::unique_ptr<WebView2Surface> surface;
  };

  DCompHost composition_;
  SkiaD3D12Context gpu_;
  SkiaSwapChainLayer baseLayer_;
  SkiaSwapChainLayer embeddedLayer_;
  SkiaSwapChainLayer annotationLayer_;
  SkiaSwapChainLayer chromeLayer_;
  std::vector<HostedWebView> embeddedWebViews_;
  input::InputRouter inputRouter_;
  std::optional<stroke::StrokeBuilder> activeStroke_;
  std::optional<std::uint64_t> activePointerId_;
  std::optional<UINT32> activeEmbeddedPointerId_;
  std::optional<document::NodeId> activeEmbeddedTouchNodeId_;
  EmbeddedMouseSession embeddedMouseSession_;
  std::optional<document::NodeId> embeddedMouseNodeId_;
  std::optional<input::PointerSample> lastPointerSample_;
  input::RouteResult activeRoute_;
  document::NodeId activeStrokeId_;
  document::Document document_;
  std::uint64_t strokeSerial_ = 0;
  HRESULT lastError_ = S_OK;
  bool batchingPointerSamples_ = false;
  std::optional<core::Rect> batchedDirtyBounds_;
  bool batchedFullRedraw_ = false;
  document::LayerClass batchedLayer_ = document::LayerClass::Annotation;
};

}  // namespace canvas::windows
