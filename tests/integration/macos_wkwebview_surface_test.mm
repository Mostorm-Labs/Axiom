#import <AppKit/AppKit.h>
#import <WebKit/WebKit.h>

#include <gtest/gtest.h>

#include "canvas/core/geometry.h"
#include "platform/macos/composition_view.h"
#include "platform/macos/wkwebview_surface.h"

namespace {

class MacosWKWebViewSurface : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE([NSThread isMainThread]);
    NSApplication* application = [NSApplication sharedApplication];
    [application setActivationPolicy:NSApplicationActivationPolicyAccessory];
    [application finishLaunching];
  }
};

TEST_F(MacosWKWebViewSurface,
       HostsWebViewBelowOverlayAndSupportsGeometryVisibilityAndInputGates) {
  CanvasCompositionView* composition =
      [[CanvasCompositionView alloc] initWithFrame:NSMakeRect(0, 0, 320, 240)];
  ASSERT_NE(composition, nil);
  NSView* container = composition.embeddedContainerView;
  ASSERT_NE(container, nil);
  EXPECT_TRUE([container isFlipped]);

  canvas::macos::WKWebViewSurface surface((__bridge void*)container);
  ASSERT_TRUE(surface.isAttached());
  ASSERT_EQ(container.subviews.count, 1U);
  NSView* hosted = container.subviews.firstObject;
  ASSERT_TRUE([hosted isKindOfClass:[WKWebView class]]);
  EXPECT_LT([composition.subviews indexOfObject:container],
            [composition.subviews
                indexOfObject:(NSView*)composition.overlayMetalView]);

  surface.setBounds({13.5F, 27.0F, 160.25F, 90.5F});
  EXPECT_TRUE(NSEqualRects(hosted.frame,
                           NSMakeRect(13.5, 27.0, 160.25, 90.5)));
  EXPECT_FALSE(hosted.hidden);

  NSWindow* window =
      [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 320, 240)
                                  styleMask:NSWindowStyleMaskBorderless
                                    backing:NSBackingStoreBuffered
                                      defer:NO];
  window.releasedWhenClosed = NO;
  window.contentView = composition;
  [window orderFrontRegardless];
  [NSApp updateWindows];
  NSView* hitProbe = [[NSView alloc] initWithFrame:hosted.bounds];
  [hosted addSubview:hitProbe];

  surface.setInteractive(false);
  EXPECT_FALSE(surface.isInteractive());
  EXPECT_EQ([hosted hitTest:NSMakePoint(2.0, 2.0)], nil);
  surface.setInteractive(true);
  EXPECT_TRUE(surface.isInteractive());
  EXPECT_NE([hosted hitTest:NSMakePoint(2.0, 2.0)], nil);
  [hitProbe removeFromSuperview];

  surface.setVisible(false);
  EXPECT_TRUE(hosted.hidden);
  surface.setVisible(true);
  EXPECT_FALSE(hosted.hidden);

  [window orderOut:nil];
  window.contentView = nil;
  window = nil;
}

TEST_F(MacosWKWebViewSurface, DetachesReattachesAndClosesWithoutLeakingAChild) {
  NSView* firstContainer = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 20, 20)];
  NSView* secondContainer = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 20, 20)];
  canvas::macos::WKWebViewSurface surface((__bridge void*)firstContainer);
  ASSERT_EQ(firstContainer.subviews.count, 1U);

  surface.detach();
  surface.detach();
  EXPECT_FALSE(surface.isAttached());
  EXPECT_EQ(firstContainer.subviews.count, 0U);

  ASSERT_TRUE(surface.attach((__bridge void*)secondContainer));
  EXPECT_TRUE(surface.isAttached());
  EXPECT_EQ(firstContainer.subviews.count, 0U);
  EXPECT_EQ(secondContainer.subviews.count, 1U);

  surface.close();
  surface.close();
  EXPECT_TRUE(surface.isClosed());
  EXPECT_FALSE(surface.isAttached());
  EXPECT_EQ(secondContainer.subviews.count, 0U);
  EXPECT_FALSE(surface.attach((__bridge void*)firstContainer));
}

TEST_F(MacosWKWebViewSurface, InvalidBoundsSuppressUntilValidGeometryReturns) {
  NSView* container = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 100, 100)];
  canvas::macos::WKWebViewSurface surface((__bridge void*)container);
  NSView* hosted = container.subviews.firstObject;
  ASSERT_NE(hosted, nil);

  surface.setBounds({10.0F, 20.0F, -1.0F, 40.0F});
  EXPECT_TRUE(hosted.hidden);
  EXPECT_TRUE(NSEqualRects(hosted.frame, NSZeroRect));
  surface.setBounds({10.0F, 20.0F, 30.0F, 40.0F});
  EXPECT_FALSE(hosted.hidden);
  EXPECT_TRUE(NSEqualRects(hosted.frame, NSMakeRect(10.0, 20.0, 30.0, 40.0)));
}

}  // namespace
