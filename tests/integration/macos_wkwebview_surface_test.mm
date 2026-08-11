#import <AppKit/AppKit.h>
#import <WebKit/WebKit.h>

#include <gtest/gtest.h>

#include <string>
#include <utility>

#include "canvas/core/geometry.h"
#include "platform/macos/composition_view.h"
#include "platform/macos/wkwebview_surface.h"

namespace {

#ifndef CANVAS_SOURCE_DIR
#error "CANVAS_SOURCE_DIR must identify the Canvas source tree"
#endif

constexpr NSTimeInterval kNavigationTimeoutSeconds = 5.0;
constexpr NSTimeInterval kRunLoopSliceSeconds = 0.01;

template <typename Predicate>
bool PumpMainRunLoopUntil(Predicate predicate,
                          NSTimeInterval timeout =
                              kNavigationTimeoutSeconds) {
  NSDate* deadline = [NSDate dateWithTimeIntervalSinceNow:timeout];
  while (!predicate() && [deadline timeIntervalSinceNow] > 0.0) {
    @autoreleasepool {
      NSDate* nextSlice =
          [NSDate dateWithTimeIntervalSinceNow:kRunLoopSliceSeconds];
      [[NSRunLoop mainRunLoop] runMode:NSDefaultRunLoopMode
                            beforeDate:nextSlice];
    }
  }
  return predicate();
}

void PumpMainRunLoopFor(NSTimeInterval duration) {
  NSDate* deadline = [NSDate dateWithTimeIntervalSinceNow:duration];
  while ([deadline timeIntervalSinceNow] > 0.0) {
    @autoreleasepool {
      NSDate* nextSlice =
          [NSDate dateWithTimeIntervalSinceNow:kRunLoopSliceSeconds];
      [[NSRunLoop mainRunLoop] runMode:NSDefaultRunLoopMode
                            beforeDate:nextSlice];
    }
  }
}

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
  EXPECT_EQ(surface.state(), canvas::macos::WKWebViewSurface::State::Closed);
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

TEST_F(MacosWKWebViewSurface, PackagedFileNavigationReachesReady) {
  const std::string packageRoot =
      std::string(CANVAS_SOURCE_DIR) + "/tests/fixtures/macos";
  const std::string fixturePath = packageRoot + "/wkwebview-ready.html";
  NSString* fixturePathString =
      [NSString stringWithUTF8String:fixturePath.c_str()];
  ASSERT_NE(fixturePathString, nil);
  ASSERT_TRUE([[NSFileManager defaultManager]
      fileExistsAtPath:fixturePathString]);
  NSURL* fixtureUrl = [NSURL fileURLWithPath:fixturePathString isDirectory:NO];
  ASSERT_NE(fixtureUrl, nil);
  ASSERT_NE(fixtureUrl.absoluteString.UTF8String, nullptr);
  const std::string fixtureUri(fixtureUrl.absoluteString.UTF8String);

  NSView* container = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 100, 100)];
  canvas::macos::WKWebViewSurface::Options options;
  options.packagedContentRoot = packageRoot;
  canvas::macos::WKWebViewSurface surface((__bridge void*)container, options);
  int completions = 0;
  canvas::macos::WKWebViewSurface::InitialLoadCompletion completion;
  ASSERT_TRUE(surface.setInitialLoadCompletionHandler(
      [&](canvas::macos::WKWebViewSurface::InitialLoadCompletion value) {
        ++completions;
        completion = std::move(value);
      }));

  ASSERT_TRUE(surface.navigate(fixtureUri));
  ASSERT_TRUE(PumpMainRunLoopUntil([&] { return completions != 0; }));
  EXPECT_EQ(completions, 1);
  EXPECT_EQ(completion.state,
            canvas::macos::WKWebViewSurface::InitialLoadState::Ready);
  EXPECT_EQ(completion.failure,
            canvas::macos::WKWebViewSurface::NavigationFailure::None);
  EXPECT_EQ(completion.uri, fixtureUri);
  EXPECT_EQ(surface.state(), canvas::macos::WKWebViewSurface::State::Ready);
  EXPECT_EQ(surface.initialLoadState(),
            canvas::macos::WKWebViewSurface::InitialLoadState::Ready);
}

TEST_F(MacosWKWebViewSurface,
       TwoOptInDataNavigationsCompleteLatestUriExactlyOnce) {
  NSView* container = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 100, 100)];
  canvas::macos::WKWebViewSurface::Options options;
  options.allowTestDataUrls = true;
  canvas::macos::WKWebViewSurface surface((__bridge void*)container, options);
  int completions = 0;
  canvas::macos::WKWebViewSurface::InitialLoadCompletion completion;
  ASSERT_TRUE(surface.setInitialLoadCompletionHandler(
      [&](canvas::macos::WKWebViewSurface::InitialLoadCompletion value) {
        ++completions;
        completion = std::move(value);
      }));
  const std::string firstUri =
      "data:text/html,<html><body>superseded</body></html>";
  const std::string latestUri =
      "data:text/html,<html><body>latest</body></html>";
  ASSERT_TRUE(surface.navigate(firstUri));
  ASSERT_TRUE(surface.navigate(latestUri));
  ASSERT_TRUE(PumpMainRunLoopUntil([&] { return completions != 0; }));
  // Give stale cancellation/finish callbacks a chance to arrive. They must
  // not result in a second public completion.
  PumpMainRunLoopFor(0.1);

  EXPECT_EQ(completions, 1);
  EXPECT_EQ(completion.state,
            canvas::macos::WKWebViewSurface::InitialLoadState::Ready);
  EXPECT_EQ(completion.failure,
            canvas::macos::WKWebViewSurface::NavigationFailure::None);
  EXPECT_EQ(completion.uri, latestUri);
  EXPECT_EQ(surface.initialLoadState(),
            canvas::macos::WKWebViewSurface::InitialLoadState::Ready);
  EXPECT_FALSE(surface.setInitialLoadCompletionHandler(nullptr));
}

TEST_F(MacosWKWebViewSurface,
       LateCompletionRegistrationReportsTerminalUriExactlyOnce) {
  NSView* container = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 100, 100)];
  canvas::macos::WKWebViewSurface::Options options;
  options.allowTestDataUrls = true;
  canvas::macos::WKWebViewSurface surface((__bridge void*)container, options);
  const std::string uri =
      "data:text/html,<html><body>late-handler</body></html>";

  ASSERT_TRUE(surface.navigate(uri));
  ASSERT_TRUE(PumpMainRunLoopUntil([&] {
    return surface.initialLoadState() ==
           canvas::macos::WKWebViewSurface::InitialLoadState::Ready;
  }));

  int completions = 0;
  canvas::macos::WKWebViewSurface::InitialLoadCompletion completion;
  ASSERT_TRUE(surface.setInitialLoadCompletionHandler(
      [&](canvas::macos::WKWebViewSurface::InitialLoadCompletion value) {
        ++completions;
        completion = std::move(value);
      }));
  EXPECT_EQ(completions, 1);
  EXPECT_EQ(completion.state,
            canvas::macos::WKWebViewSurface::InitialLoadState::Ready);
  EXPECT_EQ(completion.failure,
            canvas::macos::WKWebViewSurface::NavigationFailure::None);
  EXPECT_EQ(completion.uri, uri);
  EXPECT_FALSE(surface.setInitialLoadCompletionHandler(
      [](canvas::macos::WKWebViewSurface::InitialLoadCompletion) {}));
}

TEST_F(MacosWKWebViewSurface,
       CompletionMayCloseAndReenterSurfaceWithoutLeavingClosedState) {
  NSView* container = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 100, 100)];
  canvas::macos::WKWebViewSurface::Options options;
  options.allowTestDataUrls = true;
  canvas::macos::WKWebViewSurface surface((__bridge void*)container, options);
  int completions = 0;
  bool readyCompletion = false;
  bool closedInsideCompletion = false;
  bool reentrantNavigationAccepted = true;
  ASSERT_TRUE(surface.setInitialLoadCompletionHandler(
      [&](canvas::macos::WKWebViewSurface::InitialLoadCompletion value) {
        ++completions;
        readyCompletion =
            value.state ==
                canvas::macos::WKWebViewSurface::InitialLoadState::Ready &&
            value.failure ==
                canvas::macos::WKWebViewSurface::NavigationFailure::None;
        surface.close();
        surface.close();
        closedInsideCompletion =
            surface.state() ==
            canvas::macos::WKWebViewSurface::State::Closed;
        reentrantNavigationAccepted = surface.navigate(
            "data:text/html,<html><body>must-not-load</body></html>");
      }));

  ASSERT_TRUE(surface.navigate(
      "data:text/html,<html><body>close-in-completion</body></html>"));
  ASSERT_TRUE(PumpMainRunLoopUntil([&] { return completions != 0; }));
  PumpMainRunLoopFor(0.1);

  EXPECT_EQ(completions, 1);
  EXPECT_TRUE(readyCompletion);
  EXPECT_TRUE(closedInsideCompletion);
  EXPECT_FALSE(reentrantNavigationAccepted);
  EXPECT_TRUE(surface.isClosed());
  EXPECT_EQ(surface.state(), canvas::macos::WKWebViewSurface::State::Closed);
  EXPECT_FALSE(surface.isAttached());
  EXPECT_EQ(container.subviews.count, 0U);
}

}  // namespace
