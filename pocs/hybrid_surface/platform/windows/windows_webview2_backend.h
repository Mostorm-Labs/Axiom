#ifndef CANVAS_POC05_WINDOWS_WEBVIEW2_BACKEND_H_
#define CANVAS_POC05_WINDOWS_WEBVIEW2_BACKEND_H_

#ifndef NOMINMAX
#define NOMINMAX 1
#endif
#include <windows.h>

#include <cstdint>
#include <memory>
#include <string>

#include "canvas/poc05/hybrid_surface.h"

namespace canvas::poc05::windows {

struct WebView2BackendOptions {
  HWND owner_window = nullptr;
  std::wstring user_data_folder;
  std::wstring initial_html;
};

// Native overlay peer used by the RNW/Fabric component.  It owns only
// WebView2 COM objects; frame math and lifecycle policy remain in
// ExternalSurfaceRegistry.  The peer is intentionally usable by a standalone
// Win32 runner so the physical Windows gate does not require a product app.
class WebView2OverlayBackend final : public PlatformOverlayBackend {
 public:
  explicit WebView2OverlayBackend(WebView2BackendOptions options);
  ~WebView2OverlayBackend() override;
  WebView2OverlayBackend(const WebView2OverlayBackend&) = delete;
  WebView2OverlayBackend& operator=(const WebView2OverlayBackend&) = delete;

  bool initialize(std::string* error);
  [[nodiscard]] bool initialized() const;
  [[nodiscard]] std::string runtimeVersion() const;
  // Deliberately blocks JavaScript in the WebView renderer, while the native
  // frame timer continues to publish placement. Used by the physical gate.
  void runJsStallProbe(std::uint32_t milliseconds);

  bool create(ExternalSurfaceId id, SurfaceKind kind,
              std::string* error) override;
  bool apply(const PlacementCommand& command,
             std::string* error) override;
  void destroy(ExternalSurfaceId id) override;
  bool focus(ExternalSurfaceId id, std::string* error) override;
  void focusCanvas() override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace canvas::poc05::windows

#endif  // CANVAS_POC05_WINDOWS_WEBVIEW2_BACKEND_H_
