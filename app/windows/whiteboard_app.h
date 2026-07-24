#pragma once

#include "platform/windows/dcomp_host.h"
#include "platform/windows/embedded_mouse_session.h"

#include "canvas/input/pointer_sample.h"
#include "canvas/document/document.h"
#include "canvas/input/input_router.h"
#include "canvas/stroke/stroke_builder.h"
#include "platform/windows/skia_d3d12_context.h"
#include "platform/windows/skia_swap_chain_layer.h"
#include "platform/windows/named_pipe_server.h"
#include "platform/windows/webview2_surface.h"
#include "canvas/ipc/protocol.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace canvas::windows {

struct WhiteboardRunOptions {
  bool selfTestLayers = false;
  bool selfTestEmbedded = false;
  bool selfTestDocument = false;
  std::optional<std::wstring> openPath;
  std::optional<std::wstring> savePath;
  std::optional<std::wstring> videoPath;
  std::optional<std::wstring> ipcPipe;
  std::optional<std::string> sessionToken;
};

class WhiteboardApp {
 public:
  int run(HINSTANCE instance, int commandShow,
          const WhiteboardRunOptions& options = {});

  static LRESULT CALLBACK windowProc(HWND window, UINT message,
                                       WPARAM wParam, LPARAM lParam);

 private:
  struct HostedWebView {
    document::NodeId nodeId;
    std::unique_ptr<WebView2Surface> surface;
  };

  struct QueuedIpcMessage {
    ipc::Message message;
    NamedPipeServer::ConnectionId connectionId =
        NamedPipeServer::kInvalidConnectionId;
  };

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
  HRESULT openDocument(const std::wstring& path,
                       std::string_view requestId = {},
                       NamedPipeServer::ConnectionId connectionId =
                           NamedPipeServer::kInvalidConnectionId);
  HRESULT saveDocument(const std::wstring& path) const;
  HRESULT restoreEmbeddedSurfaces(
      const document::Document& source,
      std::vector<HostedWebView>& destinations, bool visible);
  HRESULT createEmbeddedSurface(const document::Node& node,
                                std::vector<HostedWebView>& destinations,
                                bool visible);
  HRESULT setSurfaceBounds(WebView2Surface& surface, core::Rect bounds);
  HRESULT setSurfaceVisible(WebView2Surface& surface, bool visible);
  bool restorePreviousRender(const document::Document& previous,
                             std::string_view requestId,
                             std::string_view context,
                             NamedPipeServer::ConnectionId connectionId);
  void reportFatalFailure(HRESULT result, std::string_view requestId,
                          std::string_view context,
                          NamedPipeServer::ConnectionId connectionId);
  void handleIpcMessages();
  void handleIpcMessage(const ipc::Message& message,
                        NamedPipeServer::ConnectionId connectionId);
  void sendIpc(const ipc::Message& message,
               NamedPipeServer::ConnectionId connectionId);
  HRESULT renderIpcDocument();
  HRESULT renderDocument(const document::Document& source);

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
  HWND window_ = nullptr;
  std::unique_ptr<NamedPipeServer> ipcServer_;
  std::mutex ipcMessagesMutex_;
  std::deque<QueuedIpcMessage> ipcMessages_;
  std::size_t ipcQueuedBytes_ = 0;
  bool ipcMessagePosted_ = false;
  bool ipcQueueOverflowed_ = false;
  std::optional<std::wstring> videoPath_;
  std::uint64_t strokeSerial_ = 0;
  HRESULT lastError_ = S_OK;
  bool batchingPointerSamples_ = false;
  std::optional<core::Rect> batchedDirtyBounds_;
  bool batchedFullRedraw_ = false;
  document::LayerClass batchedLayer_ = document::LayerClass::Annotation;
};

}  // namespace canvas::windows
