#pragma once

#include "canvas/embed/embedded_surface.h"
#include "platform/windows/embedded_mouse_session.h"

#include <windows.h>

#include <functional>
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
// overrides ignore setter failures for interface compatibility; accessors
// terminate on a contract violation. The destructor terminates only on that
// wrong-thread violation; owner-STA cleanup errors are returned by explicit
// close() and never leave local ownership behind.
class WebView2Surface final : public embed::EmbeddedSurface {
 public:
  enum class State { Created, Initializing, Ready, Failed, Closed };

  // Controller readiness is intentionally separate from the first document
  // being ready to receive queued host messages.
  enum class InitialLoadState { NotRequested, Pending, Ready, Failed };

  struct InitialLoadCompletion {
    InitialLoadState state = InitialLoadState::NotRequested;
    HRESULT result = S_OK;
  };

  using InitialLoadCompletionHandler =
      std::function<void(InitialLoadCompletion completion)>;

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
  // Owner-thread cleanup is best effort, returns its first failure, and always
  // transitions the object to Closed.
  HRESULT close();
  HRESULT navigate(std::wstring_view uri);
  // Must run on the creating STA, before the first navigate request. The
  // handler is consumed at the sole Ready or Failed terminal completion.
  HRESULT setInitialLoadCompletionHandler(InitialLoadCompletionHandler handler);
  // Queues a protocol JSON string until the current navigation completes, so
  // page listeners cannot miss an initialization message.
  HRESULT postMessage(std::wstring_view message);
  // Gives the composition controller keyboard/IME focus after native routing
  // selects this surface in Interact mode.
  HRESULT focus();

  // These entry points are called only after InputRouter selected an embedded
  // surface. A disabled interaction gate returns S_FALSE without synthesizing
  // input into WebView2. Pen input deliberately has no forwarding API.
  HRESULT forwardMouseMessage(UINT message, WPARAM wParam, LPARAM lParam);
  // Sends LEAVE followed by an outside UP for every active button. This is
  // used only to balance WebView state after native capture is lost.
  HRESULT cancelMouseButtons(EmbeddedMouseButtons buttons);
  HRESULT forwardTouchMessage(UINT message, UINT32 pointerId);

  // Return the current WebView2 setter result. A previous failure remains
  // available through lastResult(), so callers that need this operation's
  // result must use these checked entry points.
  HRESULT setBoundsChecked(core::Rect bounds);
  HRESULT setVisibleChecked(bool visible);

  void setBounds(core::Rect bounds) override;
  void setInteractive(bool interactive) override;
  void setVisible(bool visible) override;

  State state() const noexcept;
  InitialLoadState initialLoadState() const noexcept;
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
