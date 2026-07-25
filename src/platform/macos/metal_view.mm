#include "platform/macos/metal_view.h"

#include "platform/macos/metal_host.h"

#include <memory>
#include <utility>

@implementation CanvasMetalView {
  std::unique_ptr<canvas::macos::MetalHost> metalHost_;
}

- (instancetype)initWithFrame:(NSRect)frame {
  self = [super initWithFrame:frame];
  if (self != nil) {
    metalHost_ = std::make_unique<canvas::macos::MetalHost>();
    if (!metalHost_->attachToView((__bridge void*)self)) return nil;
    [self resizeDrawable];
  }
  return self;
}

- (void)dealloc {
  metalHost_.reset();
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
  [self resizeDrawable];
  if (metalHost_) metalHost_->reschedulePendingFrame();
}

- (BOOL)wantsUpdateLayer {
  return YES;
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

- (void)resizeDrawable {
  if (!metalHost_) return;
  NSScreen* screen = self.window.screen;
  if (screen == nil) screen = NSScreen.mainScreen;
  const CGFloat scale = screen != nil ? screen.backingScaleFactor : 1.0;
  metalHost_->resize(self.bounds.size.width, self.bounds.size.height, scale);
}

@end
