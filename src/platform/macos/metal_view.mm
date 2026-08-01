#include "platform/macos/metal_view.h"

#include "platform/macos/metal_host.h"

#include <memory>
#include <utility>

@implementation CanvasMetalView {
  std::unique_ptr<canvas::macos::MetalHost> metalHost_;
  canvas::macos::MetalSurfaceRole surfaceRole_;
  std::shared_ptr<canvas::macos::MetalRenderResources> renderResources_;
  BOOL hasBeenInWindow_;
}

- (instancetype)initWithFrame:(NSRect)frame {
  return [self initWithFrame:frame
                 surfaceRole:canvas::macos::MetalSurfaceRole::Base
             renderResources:{}];
}

- (instancetype)initWithFrame:(NSRect)frame
                   surfaceRole:(canvas::macos::MetalSurfaceRole)surfaceRole
               renderResources:(std::shared_ptr<canvas::macos::MetalRenderResources>)
                                  renderResources {
  self = [super initWithFrame:frame];
  if (self != nil) {
    surfaceRole_ = surfaceRole;
    hasBeenInWindow_ = NO;
    renderResources_ = std::move(renderResources);
    metalHost_ = std::make_unique<canvas::macos::MetalHost>(
        surfaceRole_, renderResources_);
    if (!metalHost_->attachToView((__bridge void*)self)) return nil;
    [self resizeDrawable];
  }
  return self;
}

- (void)dealloc {
  // MetalHost enforces its AppKit-main-thread ownership contract. AppKit
  // normally tears down views on the main thread; explicitly detaching here
  // also makes the layer removal ordering visible at destruction time.
  if (metalHost_) metalHost_->detachFromView();
  metalHost_.reset();
  renderResources_.reset();
}

- (void)setCanvasDocument:
    (std::shared_ptr<const canvas::document::Document>)document {
  if (metalHost_) metalHost_->setDocument(std::move(document));
}

- (void)setFrameSize:(NSSize)newSize {
  [super setFrameSize:newSize];
  [self resizeDrawable];
}

- (void)viewDidChangeBackingProperties {
  [super viewDidChangeBackingProperties];
  [self resizeDrawable];
}

- (void)viewDidMoveToWindow {
  [super viewDidMoveToWindow];

  if (self.window == nil) {
    // A view can receive an initial nil-window callback while it is being
    // assembled in a windowless composition. Only detach after it has really
    // been hosted, so the layer remains inspectable and can queue a pre-window
    // first frame.
    if (hasBeenInWindow_) {
      if (metalHost_) metalHost_->detachFromView();
      hasBeenInWindow_ = NO;
    }
    return;
  }

  hasBeenInWindow_ = YES;
  if (metalHost_ && !metalHost_->isReady()) {
    if (!metalHost_->attachToView((__bridge void*)self)) return;
  }
  [self resizeDrawable];
  if (metalHost_) metalHost_->reschedulePendingFrame();
}

- (BOOL)wantsUpdateLayer {
  return YES;
}

- (BOOL)isOpaque {
  return surfaceRole_ == canvas::macos::MetalSurfaceRole::Base;
}

- (void)updateLayer {
  if (metalHost_) metalHost_->drawIfNeeded();
}

- (void)displayLayer:(CALayer*)layer {
  if (layer == self.layer && metalHost_) metalHost_->drawIfNeeded();
}

- (std::uint64_t)nativeDisplayRequestCount {
  return metalHost_ ? metalHost_->nativeDisplayRequestCount() : 0;
}

- (std::uint64_t)committedFrameCount {
  return metalHost_ ? metalHost_->committedFrameCount() : 0;
}

- (BOOL)sharesRenderResourcesWithView:(CanvasMetalView*)other {
  if (other == nil || metalHost_ == nullptr || other->metalHost_ == nullptr) {
    return NO;
  }
  return metalHost_->sharesRenderResourcesWith(*other->metalHost_) ? YES : NO;
}

- (canvas::macos::MetalSurfaceRole)surfaceRole {
  return surfaceRole_;
}

- (void)resizeDrawable {
  if (!metalHost_) return;
  NSScreen* screen = self.window.screen;
  if (screen == nil) screen = NSScreen.mainScreen;
  const CGFloat scale = screen != nil ? screen.backingScaleFactor : 1.0;
  metalHost_->resize(self.bounds.size.width, self.bounds.size.height, scale);
}

@end
