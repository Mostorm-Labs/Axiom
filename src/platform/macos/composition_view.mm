#include "platform/macos/composition_view.h"

#include "platform/macos/metal_host.h"
#include "platform/macos/metal_view.h"

#import <QuartzCore/CALayer.h>

#include <memory>
#include <utility>

@implementation CanvasCompositionView {
  CanvasMetalView* baseMetalView_;
  NSView* embeddedContainerView_;
  CanvasMetalView* overlayMetalView_;
  std::shared_ptr<canvas::macos::MetalRenderResources> renderResources_;
  BOOL embeddedInteractionEnabled_;
}

- (instancetype)initWithFrame:(NSRect)frame {
  self = [super initWithFrame:frame];
  if (self == nil) return nil;

  renderResources_ = canvas::macos::createMetalRenderResources();
  if (!renderResources_) return nil;

  const NSRect bounds = self.bounds;
  baseMetalView_ = [[CanvasMetalView alloc]
            initWithFrame:bounds
              surfaceRole:canvas::macos::MetalSurfaceRole::Base
          renderResources:renderResources_];
  embeddedContainerView_ = [[NSView alloc] initWithFrame:bounds];
  overlayMetalView_ = [[CanvasMetalView alloc]
            initWithFrame:bounds
              surfaceRole:canvas::macos::MetalSurfaceRole::Overlay
          renderResources:renderResources_];
  if (baseMetalView_ == nil || embeddedContainerView_ == nil ||
      overlayMetalView_ == nil) {
    return nil;
  }

  self.wantsLayer = YES;
  self.layer.opaque = NO;
  embeddedContainerView_.wantsLayer = YES;
  embeddedContainerView_.layer.opaque = NO;

  const NSAutoresizingMaskOptions resizeWithHost =
      NSViewWidthSizable | NSViewHeightSizable;
  baseMetalView_.autoresizingMask = resizeWithHost;
  embeddedContainerView_.autoresizingMask = resizeWithHost;
  overlayMetalView_.autoresizingMask = resizeWithHost;

  // NSView's sibling order is the composition contract: base is backmost,
  // embedded native views are in the middle, and transparent ink is frontmost.
  [self addSubview:baseMetalView_];
  [self addSubview:embeddedContainerView_];
  [self addSubview:overlayMetalView_];
  embeddedInteractionEnabled_ = NO;
  return self;
}

- (CanvasMetalView*)baseMetalView {
  return baseMetalView_;
}

- (NSView*)embeddedContainerView {
  return embeddedContainerView_;
}

- (CanvasMetalView*)overlayMetalView {
  return overlayMetalView_;
}

- (BOOL)isEmbeddedInteractionEnabled {
  return embeddedInteractionEnabled_;
}

- (void)setEmbeddedInteractionEnabled:(BOOL)enabled {
  embeddedInteractionEnabled_ = enabled;
}

- (void)setCanvasDocument:
    (std::shared_ptr<const canvas::document::Document>)document {
  [baseMetalView_ setCanvasDocument:document];
  [overlayMetalView_ setCanvasDocument:std::move(document)];
}

- (void)setFrameSize:(NSSize)newSize {
  [super setFrameSize:newSize];
  [self setNeedsLayout:YES];
}

- (void)layout {
  [super layout];
  const NSRect bounds = self.bounds;
  if (!NSEqualRects(baseMetalView_.frame, bounds)) {
    baseMetalView_.frame = bounds;
  }
  if (!NSEqualRects(embeddedContainerView_.frame, bounds)) {
    embeddedContainerView_.frame = bounds;
  }
  if (!NSEqualRects(overlayMetalView_.frame, bounds)) {
    overlayMetalView_.frame = bounds;
  }
}

- (NSView*)hitTest:(NSPoint)point {
  if (self.hidden || self.alphaValue <= 0.0 ||
      !NSPointInRect(point, self.bounds)) {
    return nil;
  }

  NSView* target = embeddedInteractionEnabled_ ? embeddedContainerView_
                                               : overlayMetalView_;
  const NSPoint targetPoint = [target convertPoint:point fromView:self];
  NSView* hit = [target hitTest:targetPoint];
  return hit != nil ? hit : target;
}

@end
