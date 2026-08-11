#pragma once

#include "canvas/embed/embedded_surface.h"
#include "platform/macos/wkwebview_navigation_seam.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace canvas::macos {

// Minimal AppKit-hosted WebKit adapter. WebKit and AppKit object types remain
// behind the PImpl boundary so platform consumers do not inherit WebKit header
// requirements. The nativeContainer arguments are opaque NSView pointers and
// must be supplied only from Objective-C++ code on the AppKit main thread.
//
// Thread contract: construct, mutate, inspect, detach, close, and destroy on
// the AppKit main thread. close() is idempotent; detach() retains the WebView
// for a later attach(), while close() releases it permanently.
class WKWebViewSurface final : public embed::EmbeddedSurface {
 public:
  enum class State { Ready, Failed, Closed };
  enum class InitialLoadState { NotRequested, Pending, Ready, Failed };
  enum class NavigationFailure {
    None,
    InvalidUri,
    PolicyDenied,
    ProvisionalLoad,
    CommittedLoad,
    ProcessTerminated,
    Closed,
  };
  struct InitialLoadCompletion {
    InitialLoadState state = InitialLoadState::NotRequested;
    NavigationFailure failure = NavigationFailure::None;
    std::string uri;
  };
  using InitialLoadCompletionHandler =
      std::function<void(InitialLoadCompletion)>;
  struct Options {
    std::optional<std::string> packagedContentRoot;
    bool allowTestDataUrls = false;
  };
  // A null container creates a detached surface that can later be attached.
  explicit WKWebViewSurface(void* nativeContainer = nullptr);
  WKWebViewSurface(void* nativeContainer, Options options);
  ~WKWebViewSurface() override;

  WKWebViewSurface(const WKWebViewSurface&) = delete;
  WKWebViewSurface& operator=(const WKWebViewSurface&) = delete;
  WKWebViewSurface(WKWebViewSurface&&) = delete;
  WKWebViewSurface& operator=(WKWebViewSurface&&) = delete;

  // Reparents the retained WKWebView into an NSView container. A null
  // container detaches the view. Returns false only after close().
  bool attach(void* nativeContainer);
  void detach();
  void close();

  // Loads the first document and subsequent replacements. Production accepts
  // only HTTPS and a configured package-root file URL. data:text/html is for
  // bounded deterministic tests and is disabled by default.
  bool navigate(std::string_view uri);
  bool setInitialLoadCompletionHandler(InitialLoadCompletionHandler handler);

  // Bounds map directly to AppKit points in the embedded container's local
  // coordinate space. Non-finite or negative-size bounds suppress the view
  // until a valid, visible geometry is supplied.
  void setBounds(core::Rect bounds) override;
  // This is a native hit-test gate. It does not enable the composition's
  // middle-container routing policy; callers must opt into that separately.
  void setInteractive(bool interactive) override;
  void setVisible(bool visible) override;

  bool isAttached() const noexcept;
  bool isClosed() const noexcept;
  bool isInteractive() const noexcept;
  State state() const noexcept;
  InitialLoadState initialLoadState() const noexcept;
  NavigationFailure lastNavigationFailure() const noexcept;
  static detail::NavigationClass classifyNavigation(
      std::string_view uri, const Options& options);

 public:
  struct Impl;

 private:
  std::shared_ptr<Impl> impl_;
};

}  // namespace canvas::macos
