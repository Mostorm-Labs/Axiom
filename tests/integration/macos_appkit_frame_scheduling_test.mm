#import <AppKit/AppKit.h>

#include <gtest/gtest.h>

#include <chrono>
#include <functional>
#include <memory>
#include <utility>

#include "canvas/document/document.h"
#include "platform/macos/metal_view.h"

@interface CanvasSchedulingProbeView : CanvasMetalView
@property(nonatomic) NSUInteger drawRectCalls;
@property(nonatomic) NSUInteger updateLayerCalls;
@property(nonatomic) NSUInteger displayLayerCalls;
@end

@implementation CanvasSchedulingProbeView

- (void)drawRect:(NSRect)dirtyRect {
  ++self.drawRectCalls;
  [super drawRect:dirtyRect];
}

- (void)updateLayer {
  ++self.updateLayerCalls;
  [super updateLayer];
}

- (void)displayLayer:(CALayer*)layer {
  ++self.displayLayerCalls;
  [super displayLayer:layer];
}

@end

namespace {

bool runMainLoopUntil(const std::function<bool()>& predicate,
                      std::chrono::seconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (!predicate() && std::chrono::steady_clock::now() < deadline) {
    @autoreleasepool {
      [NSApp updateWindows];
      NSDate* nextSlice = [NSDate dateWithTimeIntervalSinceNow:0.01];
      [[NSRunLoop mainRunLoop] runMode:NSDefaultRunLoopMode
                           beforeDate:nextSlice];
    }
  }
  return predicate();
}

TEST(MacosAppKitFrameScheduling,
     PreWindowInvalidationReachesLayerCallbackAndCommitsFrame) {
  ASSERT_TRUE([NSThread isMainThread]);
  NSApplication* application = [NSApplication sharedApplication];
  [application setActivationPolicy:NSApplicationActivationPolicyAccessory];
  [application finishLaunching];

  auto document = std::make_shared<canvas::document::Document>();
  canvas::document::StrokeNode stroke;
  stroke.points = {{{24.0F, 30.0F}, 1.0F, 1},
                   {{180.0F, 120.0F}, 1.0F, 2}};
  ASSERT_TRUE(document->add({"appkit-scheduling-probe",
                             canvas::document::LayerClass::Annotation,
                             {20.0F, 26.0F, 164.0F, 98.0F},
                             {},
                             std::move(stroke)}));

  const NSRect frame = NSMakeRect(80.0, 80.0, 240.0, 160.0);
  CanvasSchedulingProbeView* view =
      [[CanvasSchedulingProbeView alloc] initWithFrame:frame];
  ASSERT_NE(view, nil);
  [view setCanvasDocument:document];

  // Model AppKit discarding a display request made while the view had no
  // window. MetalHost still owns the pending frame, so viewDidMoveToWindow
  // must explicitly reassert the native display request.
  [view setNeedsDisplay:NO];
  id<CALayerDelegate> layerDelegate = view.layer.delegate;
  view.layer.delegate = nil;
  [view.layer displayIfNeeded];
  view.layer.delegate = layerDelegate;
  const std::uint64_t preWindowDisplayRequests =
      [view nativeDisplayRequestCount];

  NSWindow* window =
      [[NSWindow alloc] initWithContentRect:frame
                                 styleMask:NSWindowStyleMaskBorderless
                                   backing:NSBackingStoreBuffered
                                     defer:NO];
  window.releasedWhenClosed = NO;
  window.contentView = view;
  [window orderFrontRegardless];
  EXPECT_TRUE(view.wantsLayer);
  EXPECT_TRUE([view wantsUpdateLayer]);
  EXPECT_EQ(view.window, window);
  EXPECT_TRUE(window.visible);
  EXPECT_TRUE(view.needsDisplay);
  EXPECT_GT([view nativeDisplayRequestCount], preWindowDisplayRequests)
      << "viewDidMoveToWindow must reassert a still-pending native frame";
  [view.layer displayIfNeeded];
  [window displayIfNeeded];

  const bool committed = runMainLoopUntil(
      [&] { return [view committedFrameCount] > 0; },
      std::chrono::seconds{5});

  EXPECT_TRUE(committed)
      << "AppKit did not turn the pending invalidation into a Metal commit";
  EXPECT_GT(view.displayLayerCalls + view.updateLayerCalls, 0U)
      << "the layer-hosting view must render through an AppKit/Core Animation callback";
  EXPECT_EQ(view.drawRectCalls, 0U)
      << "drawRect is not the supported callback for this layer-hosting view";
  EXPECT_GT([view committedFrameCount], 0U);

  [window orderOut:nil];
  window.contentView = nil;
  view = nil;
  window = nil;
}

}  // namespace
