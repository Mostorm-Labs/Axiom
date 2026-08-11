#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <functional>
#include <memory>
#include <utility>

#include "canvas/document/document.h"
#include "platform/macos/composition_view.h"
#include "platform/macos/metal_view.h"

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

void runMainLoopFor(std::chrono::milliseconds duration) {
  const auto deadline = std::chrono::steady_clock::now() + duration;
  (void)runMainLoopUntil(
      [&] { return std::chrono::steady_clock::now() >= deadline; },
      std::chrono::seconds{1});
}

std::shared_ptr<canvas::document::Document> makeLayeredDocument(
    float verticalOffset) {
  auto document = std::make_shared<canvas::document::Document>();
  const auto addStroke = [&](const char* id,
                             canvas::document::LayerClass layer, float y,
                             std::uint32_t color) {
    canvas::document::StrokeNode stroke;
    stroke.points = {{{20.0F, y + verticalOffset}, 1.0F, 1},
                     {{180.0F, y + verticalOffset}, 1.0F, 2}};
    stroke.width = 6.0F;
    stroke.colorArgb = color;
    return document->add({id,
                          layer,
                          {17.0F, y + verticalOffset - 3.0F, 166.0F, 6.0F},
                          {},
                          std::move(stroke)});
  };
  EXPECT_TRUE(addStroke("base", canvas::document::LayerClass::Base, 30.0F,
                        0xFFFF0000U));
  EXPECT_TRUE(addStroke("annotation",
                        canvas::document::LayerClass::Annotation, 70.0F,
                        0xFF0000FFU));
  EXPECT_TRUE(addStroke("chrome", canvas::document::LayerClass::Chrome,
                        100.0F, 0xFF00AA00U));
  return document;
}

class MacosCompositionLayerStack : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE([NSThread isMainThread]);
    NSApplication* application = [NSApplication sharedApplication];
    [application setActivationPolicy:NSApplicationActivationPolicyAccessory];
    [application finishLaunching];
  }
};

TEST_F(MacosCompositionLayerStack,
       ExposesFixedSiblingOrderFramesOpacityAndHitTestingPolicy) {
  const NSRect frame = NSMakeRect(30.0, 40.0, 240.0, 160.0);
  CanvasCompositionView* composition =
      [[CanvasCompositionView alloc] initWithFrame:frame];
  ASSERT_NE(composition, nil);
  ASSERT_EQ(composition.subviews.count, 3U);

  CanvasMetalView* base = composition.baseMetalView;
  NSView* embedded = composition.embeddedContainerView;
  CanvasMetalView* overlay = composition.overlayMetalView;
  ASSERT_NE(base, nil);
  ASSERT_NE(embedded, nil);
  ASSERT_NE(overlay, nil);
  EXPECT_TRUE([embedded isFlipped]);
  EXPECT_EQ(composition.subviews[0], base);
  EXPECT_EQ(composition.subviews[1], embedded);
  EXPECT_EQ(composition.subviews[2], overlay);
  EXPECT_TRUE(NSEqualRects(base.frame, composition.bounds));
  EXPECT_TRUE(NSEqualRects(embedded.frame, composition.bounds));
  EXPECT_TRUE(NSEqualRects(overlay.frame, composition.bounds));
  EXPECT_TRUE(NSEqualRects(embedded.frame, base.frame));
  EXPECT_TRUE(NSEqualRects(embedded.frame, overlay.frame));

  ASSERT_TRUE([base.layer isKindOfClass:[CAMetalLayer class]]);
  ASSERT_TRUE([overlay.layer isKindOfClass:[CAMetalLayer class]]);
  EXPECT_TRUE(base.layer.opaque);
  EXPECT_FALSE(overlay.layer.opaque);
  EXPECT_TRUE([base sharesRenderResourcesWithView:overlay]);

  const NSPoint center = NSMakePoint(NSMidX(composition.bounds),
                                     NSMidY(composition.bounds));
  EXPECT_FALSE(composition.isEmbeddedInteractionEnabled);
  EXPECT_EQ([composition hitTest:center], overlay)
      << "annotation overlay must own input by default";
  composition.embeddedInteractionEnabled = YES;
  EXPECT_EQ([composition hitTest:center], embedded)
      << "explicit embedded interaction must pass through the overlay";
  composition.embeddedInteractionEnabled = NO;
  EXPECT_EQ([composition hitTest:center], overlay);

  [composition setFrameSize:NSMakeSize(360.0, 220.0)];
  [composition layoutSubtreeIfNeeded];
  EXPECT_TRUE(NSEqualRects(base.frame, composition.bounds));
  EXPECT_TRUE(NSEqualRects(embedded.frame, composition.bounds));
  EXPECT_TRUE(NSEqualRects(overlay.frame, composition.bounds));
}

TEST_F(MacosCompositionLayerStack,
       BothSurfacesCommitOnAttachResizeAndReattachWithoutAContinuousLoop) {
  const NSRect frame = NSMakeRect(80.0, 80.0, 240.0, 160.0);
  CanvasCompositionView* composition =
      [[CanvasCompositionView alloc] initWithFrame:frame];
  ASSERT_NE(composition, nil);
  [composition setCanvasDocument:makeLayeredDocument(0.0F)];

  NSWindow* window =
      [[NSWindow alloc] initWithContentRect:frame
                                 styleMask:NSWindowStyleMaskBorderless
                                   backing:NSBackingStoreBuffered
                                     defer:NO];
  window.releasedWhenClosed = NO;
  window.contentView = composition;
  [window orderFrontRegardless];

  CanvasMetalView* base = composition.baseMetalView;
  CanvasMetalView* overlay = composition.overlayMetalView;
  ASSERT_TRUE(runMainLoopUntil(
      [&] {
        return [base committedFrameCount] > 0 &&
               [overlay committedFrameCount] > 0;
      },
      std::chrono::seconds{5}));

  const std::uint64_t stableBaseFrames = [base committedFrameCount];
  const std::uint64_t stableOverlayFrames = [overlay committedFrameCount];
  runMainLoopFor(std::chrono::milliseconds{200});
  EXPECT_EQ([base committedFrameCount], stableBaseFrames);
  EXPECT_EQ([overlay committedFrameCount], stableOverlayFrames);

  [composition setFrameSize:NSMakeSize(310.0, 190.0)];
  [window setContentSize:NSMakeSize(310.0, 190.0)];
  [composition layoutSubtreeIfNeeded];
  const CGFloat scale = window.screen != nil
                            ? window.screen.backingScaleFactor
                            : NSScreen.mainScreen.backingScaleFactor;
  ASSERT_TRUE(runMainLoopUntil(
      [&] {
        const CGSize baseSize =
            static_cast<CAMetalLayer*>(base.layer).drawableSize;
        const CGSize overlaySize =
            static_cast<CAMetalLayer*>(overlay.layer).drawableSize;
        const CGFloat expectedWidth = std::round(310.0 * scale);
        const CGFloat expectedHeight = std::round(190.0 * scale);
        return baseSize.width == expectedWidth &&
               baseSize.height == expectedHeight &&
               overlaySize.width == expectedWidth &&
               overlaySize.height == expectedHeight &&
               [base committedFrameCount] > stableBaseFrames &&
               [overlay committedFrameCount] > stableOverlayFrames;
      },
      std::chrono::seconds{5}));

  const std::uint64_t beforeDetachBase = [base committedFrameCount];
  const std::uint64_t beforeDetachOverlay = [overlay committedFrameCount];
  window.contentView = nil;
  [composition setCanvasDocument:makeLayeredDocument(5.0F)];
  runMainLoopFor(std::chrono::milliseconds{100});
  EXPECT_EQ([base committedFrameCount], beforeDetachBase);
  EXPECT_EQ([overlay committedFrameCount], beforeDetachOverlay);

  window.contentView = composition;
  [window orderFrontRegardless];
  EXPECT_TRUE(runMainLoopUntil(
      [&] {
        return [base committedFrameCount] > beforeDetachBase &&
               [overlay committedFrameCount] > beforeDetachOverlay;
      },
      std::chrono::seconds{5}));

  [window orderOut:nil];
  window.contentView = nil;
  composition = nil;
  window = nil;
}

}  // namespace
