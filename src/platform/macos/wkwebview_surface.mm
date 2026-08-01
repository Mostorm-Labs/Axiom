#include "platform/macos/wkwebview_surface.h"

#import <AppKit/AppKit.h>
#import <WebKit/WebKit.h>

#include <cmath>
#include <exception>
#include <memory>

// AppKit has no userInteractionEnabled property. This narrow subclass gates
// only this WebView's native hit testing; CanvasCompositionView remains the
// authority for whether the middle embedded container is reachable at all.
@interface CanvasWKWebView : WKWebView
@property(nonatomic) BOOL canvasInteractionEnabled;
@end

@implementation CanvasWKWebView

- (NSView*)hitTest:(NSPoint)point {
  if (!self.canvasInteractionEnabled || self.hidden) return nil;
  if (!NSPointInRect(point, self.bounds)) return nil;
  // A newly-created WKWebView can have no WebKit child view before its first
  // document commits. Keep the host itself hit-testable so native routing is
  // deterministic during that interval; loaded content still wins through
  // the normal AppKit hit-test result.
  NSView* hit = [super hitTest:point];
  return hit != nil ? hit : self;
}

@end

namespace {

bool isMainThread() { return [NSThread isMainThread]; }

void requireMainThread() {
  if (!isMainThread()) std::terminate();
}

bool hasUsableBounds(canvas::core::Rect bounds) {
  return bounds.width >= 0.0F && bounds.height >= 0.0F &&
         std::isfinite(bounds.x) && std::isfinite(bounds.y) &&
         std::isfinite(bounds.width) && std::isfinite(bounds.height);
}

}  // namespace

namespace canvas::macos {

struct WKWebViewSurface::Impl {
  __strong CanvasWKWebView* webView = nil;
  __weak NSView* container = nil;
  bool visible = true;
  bool interactive = false;
  bool geometryIsUsable = true;
  bool closed = false;
};

WKWebViewSurface::WKWebViewSurface(void* nativeContainer)
    : impl_(std::make_unique<Impl>()) {
  requireMainThread();

  WKWebViewConfiguration* configuration = [[WKWebViewConfiguration alloc] init];
  impl_->webView = [[CanvasWKWebView alloc] initWithFrame:NSZeroRect
                                             configuration:configuration];
  impl_->webView.canvasInteractionEnabled = false;
  if (nativeContainer != nullptr) (void)attach(nativeContainer);
}

WKWebViewSurface::~WKWebViewSurface() {
  requireMainThread();
  close();
}

bool WKWebViewSurface::attach(void* nativeContainer) {
  requireMainThread();
  if (impl_->closed) return false;
  if (nativeContainer == nullptr) {
    detach();
    return true;
  }

  NSView* container = (__bridge NSView*)nativeContainer;
  if (impl_->container == container && impl_->webView.superview == container) {
    return true;
  }

  [impl_->webView removeFromSuperview];
  impl_->container = container;
  [container addSubview:impl_->webView];
  impl_->webView.hidden = !impl_->visible || !impl_->geometryIsUsable;
  return true;
}

void WKWebViewSurface::detach() {
  requireMainThread();
  if (impl_->closed) return;
  [impl_->webView removeFromSuperview];
  impl_->container = nil;
}

void WKWebViewSurface::close() {
  requireMainThread();
  if (impl_->closed) return;
  detach();
  impl_->webView = nil;
  impl_->closed = true;
}

void WKWebViewSurface::setBounds(core::Rect bounds) {
  requireMainThread();
  if (impl_->closed) return;

  impl_->geometryIsUsable = hasUsableBounds(bounds);
  if (!impl_->geometryIsUsable) {
    impl_->webView.frame = NSZeroRect;
    impl_->webView.hidden = YES;
    return;
  }

  impl_->webView.frame = NSMakeRect(bounds.x, bounds.y, bounds.width,
                                    bounds.height);
  impl_->webView.hidden = !impl_->visible;
}

void WKWebViewSurface::setInteractive(bool interactive) {
  requireMainThread();
  if (impl_->closed) return;
  impl_->interactive = interactive;
  impl_->webView.canvasInteractionEnabled = interactive ? YES : NO;
}

void WKWebViewSurface::setVisible(bool visible) {
  requireMainThread();
  if (impl_->closed) return;
  impl_->visible = visible;
  impl_->webView.hidden = !visible || !impl_->geometryIsUsable;
}

bool WKWebViewSurface::isAttached() const noexcept {
  requireMainThread();
  return !impl_->closed && impl_->container != nil &&
         impl_->webView.superview == impl_->container;
}

bool WKWebViewSurface::isClosed() const noexcept {
  requireMainThread();
  return impl_->closed;
}

bool WKWebViewSurface::isInteractive() const noexcept {
  requireMainThread();
  return !impl_->closed && impl_->interactive;
}

}  // namespace canvas::macos
