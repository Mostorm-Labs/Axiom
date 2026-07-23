#pragma once

#include "canvas/embed/embedded_surface.h"

#include <windows.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace canvas::windows {

class DCompHost;

// A composition-hosted WebView2 adapter. WebView2 SDK types stay behind the
// PImpl boundary so consumers of canvas::windows_platform do not need the SDK
// include directory as a public usage requirement.
//
// Thread contract: construct, mutate, inspect, and destroy on the creating STA.
// HRESULT-returning calls report RPC_E_WRONG_THREAD. The void EmbeddedSurface
// overrides, accessors, and destructor terminate on a contract violation so
// apartment-bound COM interfaces are never silently released cross-thread.
class WebView2Surface final : public embed::EmbeddedSurface {
 public:
  enum class State { Created, Initializing, Ready, Failed, Closed };

  enum class NavigationClass {
    Denied,
    Https,
    LocalVirtualHost,
    TestData,
  };

  struct Options {
    // Disabled by default. Only deterministic integration/diagnostic pages
    // opt in; production content must use HTTPS or a mapped virtual host.
    bool allowTestDataUrls = false;
    std::optional<std::wstring> canvasLocalFolder;
    std::optional<std::wstring> mediaCanvasLocalFolder;
  };

  WebView2Surface(DCompHost& host, HWND hostWindow);
  WebView2Surface(DCompHost& host, HWND hostWindow, Options options);
  ~WebView2Surface() override;

  WebView2Surface(const WebView2Surface&) = delete;
  WebView2Surface& operator=(const WebView2Surface&) = delete;
  WebView2Surface(WebView2Surface&&) = delete;
  WebView2Surface& operator=(WebView2Surface&&) = delete;

  HRESULT initialize();
  // Must run on the creating STA. Wrong-thread close leaves ownership intact.
  HRESULT close();
  HRESULT navigate(std::wstring_view uri);

  // These entry points are called only after InputRouter selected an embedded
  // surface. A disabled interaction gate returns S_FALSE without synthesizing
  // input into WebView2. Pen input deliberately has no forwarding API.
  HRESULT forwardMouseMessage(UINT message, WPARAM wParam, LPARAM lParam);
  HRESULT forwardTouchMessage(UINT message, UINT32 pointerId);

  void setBounds(core::Rect bounds) override;
  void setInteractive(bool interactive) override;
  void setVisible(bool visible) override;

  State state() const noexcept;
  HRESULT lastResult() const noexcept;
  bool interactive() const noexcept;
  const std::vector<std::wstring>& capturedMessages() const noexcept;

  static NavigationClass classifyNavigation(std::wstring_view uri,
                                            bool allowTestDataUrls);

 private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};

}  // namespace canvas::windows
