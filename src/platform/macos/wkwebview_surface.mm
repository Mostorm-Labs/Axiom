#include "platform/macos/wkwebview_surface.h"

#import <AppKit/AppKit.h>
#import <WebKit/WebKit.h>

#include <cmath>
#include <array>
#include <exception>
#include <memory>
#include <utility>

@interface CanvasWKWebView : WKWebView
@property(nonatomic) BOOL canvasInteractionEnabled;
@end

@implementation CanvasWKWebView
// CanvasCompositionView remains the authority for broader embedded routing;
// this subclass only gates the WebView's native hit test.
- (NSView*)hitTest:(NSPoint)point {
  if (!self.canvasInteractionEnabled || self.hidden ||
      !NSPointInRect(point, self.bounds)) return nil;
  NSView* hit = [super hitTest:point];
  return hit != nil ? hit : self;
}
@end

@interface CanvasWKNavigationDelegate : NSObject <WKNavigationDelegate>
@property(nonatomic, copy) void (^didFinish)(WKNavigation* navigation);
@property(nonatomic, copy) void (^didFail)(WKNavigation* navigation, NSError* error,
                                            BOOL provisional);
@property(nonatomic, copy) void (^didTerminate)();
@property(nonatomic, copy) void (^decideAction)(WKNavigationAction*, void (^)(WKNavigationActionPolicy));
@property(nonatomic, copy) void (^decideResponse)(WKNavigationResponse*, void (^)(WKNavigationResponsePolicy));
@end

@implementation CanvasWKNavigationDelegate
- (void)webView:(WKWebView*)webView didFinishNavigation:(WKNavigation*)navigation {
  if (self.didFinish) self.didFinish(navigation);
}
- (void)webView:(WKWebView*)webView didFailNavigation:(WKNavigation*)navigation
       withError:(NSError*)error {
  if (self.didFail) self.didFail(navigation, error, NO);
}
- (void)webView:(WKWebView*)webView didFailProvisionalNavigation:(WKNavigation*)navigation
       withError:(NSError*)error {
  if (self.didFail) self.didFail(navigation, error, YES);
}
- (void)webViewWebContentProcessDidTerminate:(WKWebView*)webView {
  if (self.didTerminate) self.didTerminate();
}
- (void)webView:(WKWebView*)webView decidePolicyForNavigationAction:(WKNavigationAction*)action
 decisionHandler:(void (^)(WKNavigationActionPolicy))decisionHandler {
  if (self.decideAction) self.decideAction(action, decisionHandler);
  else decisionHandler(WKNavigationActionPolicyCancel);
}
- (void)webView:(WKWebView*)webView decidePolicyForNavigationResponse:(WKNavigationResponse*)response
 decisionHandler:(void (^)(WKNavigationResponsePolicy))decisionHandler {
  if (self.decideResponse) self.decideResponse(response, decisionHandler);
  else decisionHandler(WKNavigationResponsePolicyCancel);
}
@end

namespace {
bool isMainThread() { return [NSThread isMainThread]; }
void requireMainThread() { if (!isMainThread()) std::terminate(); }
bool hasUsableBounds(canvas::core::Rect bounds) {
  return bounds.width >= 0.0F && bounds.height >= 0.0F &&
         std::isfinite(bounds.x) && std::isfinite(bounds.y) &&
         std::isfinite(bounds.width) && std::isfinite(bounds.height);
}

bool isAllowedPackagedFile(NSString* string, const std::string& root) {
  if (string == nil || root.empty()) return false;
  NSURL* url = [NSURL URLWithString:string];
  if (url == nil || !url.isFileURL) return false;
  NSString* rootString = [NSString stringWithUTF8String:root.c_str()];
  NSURL* rootUrl = rootString == nil ? nil
                                    : [NSURL fileURLWithPath:rootString isDirectory:YES];
  if (rootUrl == nil) return false;
  NSURL* canonicalRoot = [rootUrl URLByResolvingSymlinksInPath];
  NSURL* canonicalPath = [url URLByResolvingSymlinksInPath];
  NSString* rootPath = canonicalRoot.path;
  NSString* path = canonicalPath.path;
  if (rootPath == nil || path == nil) return false;
  NSString* prefix = [rootPath stringByAppendingString:@"/"];
  return [path isEqualToString:rootPath] || [path hasPrefix:prefix];
}

bool isValidHttps(NSString* string) {
  NSURLComponents* components = [NSURLComponents componentsWithString:string];
  return components != nil &&
         [components.scheme.lowercaseString isEqualToString:@"https"] &&
         components.host.length != 0U && components.user == nil &&
         components.password == nil;
}
}

namespace canvas::macos {

struct CallbackToken {
  std::weak_ptr<WKWebViewSurface::Impl> target;
  std::uint64_t epoch = 0U;
};

struct WKWebViewSurface::Impl {
  struct PendingTerminal {
    __strong WKNavigation* navigation = nil;
    bool success = false;
    NavigationFailure failure = NavigationFailure::None;
  };
  __strong CanvasWKWebView* webView = nil;
  __strong CanvasWKNavigationDelegate* delegate = nil;
  __weak NSView* container = nil;
  bool visible = true;
  bool interactive = false;
  bool geometryIsUsable = true;
  bool closed = false;
  std::uint64_t epoch = 1U;
  std::uint64_t navigationEpoch = 1U;
  detail::NavigationTracker tracker;
  detail::NavigationTracker::Generation activeGeneration = 0U;
  __strong WKNavigation* activeNavigation = nil;
  std::string activeUri;
  std::string queuedUri;
  std::string packagedContentRoot;
  bool allowTestDataUrls = false;
  InitialLoadCompletionHandler completion;
  bool completionDelivered = false;
  detail::InitialLoadTerminalRecord terminalRecord;
  State state = State::Ready;
  NavigationFailure lastFailure = NavigationFailure::None;
  bool pumping = false;
  bool pumpRequested = false;
  bool navigationDispatching = false;
  std::array<PendingTerminal, 2U> dispatchTerminals{};
  std::size_t dispatchTerminalCount = 0U;
  std::shared_ptr<CallbackToken> callbackToken;

  void finish(WKNavigation* navigation, bool success, NavigationFailure failure);
  void pump();
  void emitCompletion(InitialLoadState loadState, NavigationFailure failure,
                      std::string uri);
  bool allowsURL(NSURL* url, bool targetFrame) const;
};

void WKWebViewSurface::Impl::emitCompletion(InitialLoadState loadState,
                                             NavigationFailure failure,
                                             std::string uri) {
  if (completionDelivered) return;
  terminalRecord.record(static_cast<detail::InitialLoadState>(loadState),
                        std::move(uri));
  if (!completion) return;
  completionDelivered = true;
  InitialLoadCompletionHandler callback = std::move(completion);
  try {
    callback(InitialLoadCompletion{loadState, failure, terminalRecord.uri()});
  } catch (...) {
    // Completion is a notification boundary; an application callback must not
    // unwind through WebKit's Objective-C delegate stack.
  }
}

bool WKWebViewSurface::Impl::allowsURL(NSURL* url, bool targetFrame) const {
  if (closed || url == nil || !targetFrame) return false;
  NSString* absolute = url.absoluteString;
  if (absolute == nil) return false;
  const char* bytes = absolute.UTF8String;
  if (bytes == nullptr) return false;
  const std::string value(bytes);
  const Options options{packagedContentRoot, allowTestDataUrls};
  const auto category = WKWebViewSurface::classifyNavigation(value, options);
  if (category == detail::NavigationClass::Denied) return false;
  if (category == detail::NavigationClass::Https) return isValidHttps(absolute);
  if (category == detail::NavigationClass::PackagedFile)
    return isAllowedPackagedFile(absolute, packagedContentRoot);
  return true;
}

void WKWebViewSurface::Impl::finish(WKNavigation* navigation, bool success,
                                    NavigationFailure failure) {
  requireMainThread();
  if (closed || navigation == nil ||
      (navigation != activeNavigation &&
       !(navigationDispatching && activeNavigation == nil)) ||
      !tracker.isActive(activeGeneration)) return;
  if (navigationDispatching && activeNavigation == nil) {
    for (std::size_t i = 0; i < dispatchTerminalCount; ++i) {
      if (dispatchTerminals[i].navigation == navigation) return;
    }
    if (dispatchTerminalCount < dispatchTerminals.size()) {
      auto& pending = dispatchTerminals[dispatchTerminalCount++];
      pending.navigation = navigation;
      pending.success = success;
      pending.failure = failure;
    }
    return;
  }
  const auto generation = activeGeneration;
  const auto uri = activeUri;
  activeNavigation = nil;
  navigationDispatching = false;
  activeGeneration = 0U;
  activeUri.clear();
  const auto action = tracker.complete(generation, success);
  if (action == detail::NavigationEventAction::PromoteNext) {
    pumpRequested = true;
    pump();
    return;
  }
  state = success ? State::Ready : State::Failed;
  lastFailure = success ? NavigationFailure::None : failure;
  emitCompletion(success ? InitialLoadState::Ready : InitialLoadState::Failed,
                 success ? NavigationFailure::None : failure, uri);
}

void WKWebViewSurface::Impl::pump() {
  requireMainThread();
  pumpRequested = true;
  if (pumping || closed) return;
  pumping = true;
  do {
    pumpRequested = false;
    if (activeNavigation != nil || !tracker.hasQueued()) continue;
    const auto generation = tracker.issueNext();
    if (!generation) continue;
    activeGeneration = *generation;
    if (!queuedUri.empty()) {
      activeUri = std::move(queuedUri);
      queuedUri.clear();
    }
    // The URI is attached by navigate() immediately before pump().
    NSString* string = [NSString stringWithUTF8String:activeUri.c_str()];
    NSURL* url = string == nil ? nil : [NSURL URLWithString:string];
    if (url == nil) {
      activeGeneration = 0U;
      const auto action = tracker.complete(*generation, false);
      if (action == detail::NavigationEventAction::PromoteNext) pumpRequested = true;
      else {
        state = State::Failed;
        lastFailure = NavigationFailure::InvalidUri;
        emitCompletion(InitialLoadState::Failed, lastFailure, activeUri);
      }
      continue;
    }
    const auto launchEpoch = navigationEpoch;
    dispatchTerminalCount = 0U;
    navigationDispatching = true;
    WKNavigation* navigation = [webView loadRequest:[NSURLRequest requestWithURL:url]];
    navigationDispatching = false;
    std::size_t matchingTerminal = dispatchTerminalCount;
    for (std::size_t i = 0; i < dispatchTerminalCount; ++i) {
      if (dispatchTerminals[i].navigation == navigation) {
        matchingTerminal = i;
        break;
      }
    }
    if (navigation != nil && matchingTerminal < dispatchTerminalCount &&
        activeGeneration == *generation) {
      const bool terminalSuccess = dispatchTerminals[matchingTerminal].success;
      const auto terminalFailure = dispatchTerminals[matchingTerminal].failure;
      dispatchTerminalCount = 0U;
      activeNavigation = navigation;
      finish(navigation, terminalSuccess, terminalFailure);
      continue;
    }
    dispatchTerminalCount = 0U;
    if (closed || launchEpoch != navigationEpoch || activeGeneration != *generation) {
      if (navigation != nil) [webView stopLoading];
      // A synchronous re-entrant navigate() can replace the deferred URI
      // before loadRequest returns. Retire this issued generation explicitly:
      // no WebKit terminal callback is required for a stale return, and
      // otherwise NavigationTracker would keep it active forever and starve
      // the latest queued request.
      if (!closed && tracker.isActive(*generation)) {
        activeNavigation = nil;
        activeGeneration = 0U;
        activeUri.clear();
        const auto action = tracker.complete(*generation, false);
        if (action == detail::NavigationEventAction::PromoteNext) {
          pumpRequested = true;
        } else {
          state = State::Failed;
          lastFailure = NavigationFailure::CommittedLoad;
          emitCompletion(InitialLoadState::Failed, lastFailure, "");
        }
      }
      continue;
    }
    if (navigation == nil) {
      activeGeneration = 0U;
      const auto failedAction = tracker.complete(*generation, false);
      if (failedAction == detail::NavigationEventAction::PromoteNext) pumpRequested = true;
      else {
        state = State::Failed;
        lastFailure = NavigationFailure::CommittedLoad;
        emitCompletion(InitialLoadState::Failed, lastFailure, activeUri);
      }
      continue;
    }
    activeNavigation = navigation;
  } while (pumpRequested);
  pumping = false;
}

WKWebViewSurface::WKWebViewSurface(void* nativeContainer)
    : WKWebViewSurface(nativeContainer, Options{}) {}

WKWebViewSurface::WKWebViewSurface(void* nativeContainer, Options options)
    : impl_(std::make_shared<Impl>()) {
  requireMainThread();
  impl_->delegate = [CanvasWKNavigationDelegate new];
  impl_->callbackToken = std::make_shared<CallbackToken>(
      CallbackToken{impl_, impl_->epoch});
  const std::weak_ptr<CallbackToken> weakToken = impl_->callbackToken;
  impl_->delegate.didFinish = ^(WKNavigation* navigation) {
    if (const auto token = weakToken.lock()) {
      if (const auto target = token->target.lock(); target &&
          token->epoch == target->epoch)
        target->finish(navigation, true, NavigationFailure::None);
    }
  };
  impl_->delegate.didFail = ^(WKNavigation* navigation, NSError* error, BOOL provisional) {
    if (const auto token = weakToken.lock()) {
      if (const auto target = token->target.lock(); target &&
          token->epoch == target->epoch)
        target->finish(navigation, false, provisional ? NavigationFailure::ProvisionalLoad : NavigationFailure::CommittedLoad);
    }
  };
  impl_->delegate.didTerminate = ^{
    if (const auto token = weakToken.lock()) {
      if (const auto target = token->target.lock(); target &&
          token->epoch == target->epoch)
        target->finish(target->activeNavigation, false, NavigationFailure::ProcessTerminated);
    }
  };
  impl_->delegate.decideAction = ^(WKNavigationAction* action,
                                   void (^decision)(WKNavigationActionPolicy)) {
    const auto token = weakToken.lock();
    const auto target = token ? token->target.lock() : nullptr;
    NSURLRequest* request = action.request;
    const bool frame = action.targetFrame != nil && action.targetFrame.isMainFrame;
    const bool allowed = target != nullptr && token->epoch == target->epoch &&
                         target->allowsURL(request.URL, frame);
    decision(allowed ? WKNavigationActionPolicyAllow : WKNavigationActionPolicyCancel);
  };
  impl_->delegate.decideResponse = ^(WKNavigationResponse* response,
                                     void (^decision)(WKNavigationResponsePolicy)) {
    const auto token = weakToken.lock();
    const auto target = token ? token->target.lock() : nullptr;
    const bool frame = response.forMainFrame;
    const bool allowed = target != nullptr && token->epoch == target->epoch &&
                         target->allowsURL(response.response.URL, frame);
    decision(allowed ? WKNavigationResponsePolicyAllow : WKNavigationResponsePolicyCancel);
  };
  WKWebViewConfiguration* configuration = [[WKWebViewConfiguration alloc] init];
  impl_->webView = [[CanvasWKWebView alloc] initWithFrame:NSZeroRect configuration:configuration];
  impl_->webView.navigationDelegate = impl_->delegate; // WKWebView retains this weakly.
  impl_->webView.canvasInteractionEnabled = NO;
  impl_->packagedContentRoot = options.packagedContentRoot.value_or("");
  impl_->allowTestDataUrls = options.allowTestDataUrls;
  if (nativeContainer != nullptr) (void)attach(nativeContainer);
}

WKWebViewSurface::~WKWebViewSurface() { requireMainThread(); close(); }

bool WKWebViewSurface::attach(void* nativeContainer) {
  requireMainThread();
  if (impl_->closed) return false;
  if (nativeContainer == nullptr) { detach(); return true; }
  NSView* container = (__bridge NSView*)nativeContainer;
  if (impl_->container == container && impl_->webView.superview == container) return true;
  [impl_->webView removeFromSuperview];
  impl_->container = container;
  [container addSubview:impl_->webView];
  impl_->webView.hidden = !impl_->visible || !impl_->geometryIsUsable;
  return true;
}

void WKWebViewSurface::detach() {
  requireMainThread();
  if (impl_->closed) return;
  [impl_->webView removeFromSuperview]; impl_->container = nil;
}

void WKWebViewSurface::close() {
  requireMainThread();
  if (impl_->closed) return;
  ++impl_->epoch; if (impl_->epoch == 0U) ++impl_->epoch;
  if (impl_->callbackToken) {
    impl_->callbackToken->target.reset();
    impl_->callbackToken->epoch = impl_->epoch;
  }
  impl_->tracker.cancel(); impl_->activeNavigation = nil;
  impl_->activeGeneration = 0U; impl_->completion = nullptr;
  impl_->state = State::Closed;
  impl_->lastFailure = NavigationFailure::Closed;
  impl_->webView.navigationDelegate = nil;
  [impl_->webView stopLoading];
  [impl_->webView removeFromSuperview]; impl_->container = nil;
  impl_->closed = true; impl_->delegate = nil; impl_->webView = nil;
}

bool WKWebViewSurface::navigate(std::string_view uri) {
  requireMainThread();
  if (impl_->closed || uri.empty() || uri.size() > 256U * 1024U) return false;
  Options options;
  options.packagedContentRoot = impl_->packagedContentRoot;
  options.allowTestDataUrls = impl_->allowTestDataUrls;
  const auto category = classifyNavigation(uri, options);
  if (category == detail::NavigationClass::Denied) {
    impl_->lastFailure = NavigationFailure::PolicyDenied;
    return false;
  }
  NSString* urlString = [[NSString alloc] initWithBytes:uri.data()
                                                  length:uri.size()
                                                encoding:NSUTF8StringEncoding];
  if (urlString == nil ||
      (category == detail::NavigationClass::Https && !isValidHttps(urlString)) ||
      (category == detail::NavigationClass::PackagedFile &&
       !isAllowedPackagedFile(urlString, impl_->packagedContentRoot))) {
    impl_->lastFailure = NavigationFailure::InvalidUri;
    return false;
  }
  const auto generation = impl_->tracker.submit();
  if (!generation) return false;
  ++impl_->navigationEpoch;
  if (impl_->navigationEpoch == 0U) ++impl_->navigationEpoch;
  impl_->queuedUri = std::string(uri);
  if (impl_->activeGeneration != 0U && impl_->activeNavigation != nil) {
    const auto superseded = impl_->activeGeneration;
    WKNavigation* supersededNavigation = impl_->activeNavigation;
    [impl_->webView stopLoading];
    if (impl_->activeGeneration != superseded ||
        impl_->activeNavigation != supersededNavigation) {
      // stopLoading synchronously delivered the old terminal event and the
      // pump may already have promoted the replacement. Never retire the new
      // generation using stale pre-stop state.
    } else {
      impl_->activeNavigation = nil;
      impl_->activeGeneration = 0U;
      impl_->activeUri.clear();
      // stopLoading is not guaranteed to produce didFailNavigation. Retire
      // the active generation ourselves so the latest deferred request is
      // promptly issued; late callbacks keep the old WKNavigation identity
      // and are ignored after the replacement is active.
      if (impl_->tracker.isActive(superseded) &&
          impl_->tracker.complete(superseded, false) ==
              detail::NavigationEventAction::PromoteNext) {
        impl_->pumpRequested = true;
      }
    }
  }
  if (impl_->activeGeneration == 0U) {
    impl_->activeUri = impl_->queuedUri;
    impl_->queuedUri.clear();
  }
  impl_->pump();
  return true;
}

bool WKWebViewSurface::setInitialLoadCompletionHandler(InitialLoadCompletionHandler handler) {
  requireMainThread();
  if (impl_->closed || impl_->completionDelivered || !handler) return false;
  impl_->completion = std::move(handler);
  if (impl_->tracker.state() == detail::InitialLoadState::Ready ||
      impl_->tracker.state() == detail::InitialLoadState::Failed)
    impl_->emitCompletion(
        impl_->tracker.state() == detail::InitialLoadState::Ready
            ? InitialLoadState::Ready
            : InitialLoadState::Failed,
        impl_->lastFailure, impl_->terminalRecord.uri());
  return true;
}

void WKWebViewSurface::setBounds(core::Rect bounds) {
  requireMainThread(); if (impl_->closed) return;
  impl_->geometryIsUsable = hasUsableBounds(bounds);
  if (!impl_->geometryIsUsable) { impl_->webView.frame = NSZeroRect; impl_->webView.hidden = YES; return; }
  impl_->webView.frame = NSMakeRect(bounds.x, bounds.y, bounds.width, bounds.height);
  impl_->webView.hidden = !impl_->visible;
}
void WKWebViewSurface::setInteractive(bool interactive) {
  requireMainThread(); if (impl_->closed) return;
  impl_->interactive = interactive; impl_->webView.canvasInteractionEnabled = interactive ? YES : NO;
}
void WKWebViewSurface::setVisible(bool visible) {
  requireMainThread(); if (impl_->closed) return;
  impl_->visible = visible; impl_->webView.hidden = !visible || !impl_->geometryIsUsable;
}
bool WKWebViewSurface::isAttached() const noexcept { requireMainThread(); return !impl_->closed && impl_->container != nil && impl_->webView.superview == impl_->container; }
bool WKWebViewSurface::isClosed() const noexcept { requireMainThread(); return impl_->closed; }
bool WKWebViewSurface::isInteractive() const noexcept { requireMainThread(); return !impl_->closed && impl_->interactive; }
WKWebViewSurface::State WKWebViewSurface::state() const noexcept { requireMainThread(); return impl_->state; }
WKWebViewSurface::InitialLoadState WKWebViewSurface::initialLoadState() const noexcept { requireMainThread(); return static_cast<InitialLoadState>(impl_->tracker.state()); }
WKWebViewSurface::NavigationFailure WKWebViewSurface::lastNavigationFailure() const noexcept { requireMainThread(); return impl_->lastFailure; }
detail::NavigationClass WKWebViewSurface::classifyNavigation(std::string_view uri, const Options& options) {
  return detail::classifyNavigation(uri, detail::NavigationPolicyOptions{options.packagedContentRoot.value_or(""), options.allowTestDataUrls});
}

}  // namespace canvas::macos
