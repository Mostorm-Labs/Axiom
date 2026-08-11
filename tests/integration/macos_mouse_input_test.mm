#import <AppKit/AppKit.h>

#include <gtest/gtest.h>

#include <chrono>
#include <functional>
#include <memory>
#include <variant>

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

NSEvent* mouseEvent(CanvasMetalView* view, NSEventType type, float canvasX,
                    float canvasY, NSInteger eventNumber) {
  const NSRect bounds = view.bounds;
  const NSPoint local =
      NSMakePoint(bounds.origin.x + canvasX,
                  bounds.origin.y + bounds.size.height - canvasY);
  const NSPoint windowLocation = [view convertPoint:local toView:nil];
  return [NSEvent mouseEventWithType:type
                           location:windowLocation
                      modifierFlags:0
                          timestamp:static_cast<NSTimeInterval>(eventNumber)
                       windowNumber:view.window.windowNumber
                            context:nil
                        eventNumber:eventNumber
                         clickCount:1
                           pressure:0.5];
}

void sendStroke(CanvasMetalView* view, NSPoint first, NSPoint second,
                NSPoint third) {
  [view mouseDown:mouseEvent(view, NSEventTypeLeftMouseDown, first.x, first.y,
                             1)];
  [view mouseDragged:mouseEvent(view, NSEventTypeLeftMouseDragged, second.x,
                                second.y, 2)];
  [view mouseUp:mouseEvent(view, NSEventTypeLeftMouseUp, third.x, third.y, 3)];
}

const canvas::document::StrokeNode& stroke(
    const canvas::document::Node& node) {
  return std::get<canvas::document::StrokeNode>(node.payload);
}

class MacosMouseInput : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE([NSThread isMainThread]);
    NSApplication* application = [NSApplication sharedApplication];
    [application setActivationPolicy:NSApplicationActivationPolicyAccessory];
    [application finishLaunching];

    const NSRect frame = NSMakeRect(0, 0, 320, 240);
    composition_ = [[CanvasCompositionView alloc] initWithFrame:frame];
    ASSERT_NE(composition_, nil);
    window_ = [[NSWindow alloc] initWithContentRect:frame
                                          styleMask:NSWindowStyleMaskBorderless
                                            backing:NSBackingStoreBuffered
                                              defer:NO];
    window_.releasedWhenClosed = NO;
    window_.contentView = composition_;
    [window_ orderFrontRegardless];
    ASSERT_TRUE(runMainLoopUntil(
        [&] {
          return [composition_.baseMetalView committedFrameCount] > 0 &&
                 [composition_.overlayMetalView committedFrameCount] > 0;
        },
        std::chrono::seconds{5}));
  }

  void TearDown() override {
    [window_ orderOut:nil];
    window_.contentView = nil;
    composition_ = nil;
    window_ = nil;
  }

  CanvasCompositionView* composition_ = nil;
  NSWindow* window_ = nil;
};

TEST_F(MacosMouseInput, DrawsOneBaseStrokeInTopLeftLogicalCoordinates) {
  auto document = std::make_shared<canvas::document::Document>();
  const auto baseFrames =
      [composition_.baseMetalView committedFrameCount];
  const auto overlayFrames =
      [composition_.overlayMetalView committedFrameCount];
  [composition_ setEditableCanvasDocument:document];
  ASSERT_TRUE(runMainLoopUntil(
      [&] {
        return [composition_.baseMetalView committedFrameCount] > baseFrames &&
               [composition_.overlayMetalView committedFrameCount] >
                   overlayFrames;
      },
      std::chrono::seconds{5}));
  const auto baseRequests =
      [composition_.baseMetalView nativeDisplayRequestCount];
  const auto overlayRequests =
      [composition_.overlayMetalView nativeDisplayRequestCount];
  const auto drawBaseFrames =
      [composition_.baseMetalView committedFrameCount];
  const auto drawOverlayFrames =
      [composition_.overlayMetalView committedFrameCount];

  sendStroke(composition_.overlayMetalView, NSMakePoint(20, 30),
             NSMakePoint(50, 60), NSMakePoint(80, 90));

  ASSERT_TRUE(runMainLoopUntil(
      [&] {
        return [composition_.baseMetalView committedFrameCount] >
               drawBaseFrames;
      },
      std::chrono::seconds{5}));
  runMainLoopFor(std::chrono::milliseconds{100});

  ASSERT_EQ(document->nodes().size(), 1U);
  const auto& node = document->nodes().front();
  EXPECT_EQ(node.layer, canvas::document::LayerClass::Base);
  ASSERT_EQ(stroke(node).points.size(), 3U)
      << "Down/Dragged/Up must remain one mouse session";
  EXPECT_EQ(stroke(node).points[0].position,
            (canvas::core::Vec2{20, 30}));
  EXPECT_EQ(stroke(node).points[1].position,
            (canvas::core::Vec2{50, 60}));
  EXPECT_EQ(stroke(node).points[2].position,
            (canvas::core::Vec2{80, 90}));
  EXPECT_GT([composition_.baseMetalView nativeDisplayRequestCount],
            baseRequests);
  EXPECT_EQ([composition_.overlayMetalView nativeDisplayRequestCount],
            overlayRequests);
  EXPECT_GT([composition_.baseMetalView committedFrameCount], drawBaseFrames);
  EXPECT_EQ([composition_.overlayMetalView committedFrameCount],
            drawOverlayFrames);
}

TEST_F(MacosMouseInput, DrawsEmbeddedAnnotationOnlyOnOverlay) {
  auto document = std::make_shared<canvas::document::Document>();
  canvas::document::Node embedded;
  embedded.id = "web";
  embedded.layer = canvas::document::LayerClass::Embedded;
  embedded.bounds = {80, 40, 160, 100};
  embedded.payload = canvas::document::EmbeddedNode{};
  ASSERT_TRUE(document->add(std::move(embedded)));
  const auto baseFrames =
      [composition_.baseMetalView committedFrameCount];
  const auto overlayFrames =
      [composition_.overlayMetalView committedFrameCount];
  [composition_ setEditableCanvasDocument:document];
  ASSERT_TRUE(runMainLoopUntil(
      [&] {
        return [composition_.baseMetalView committedFrameCount] > baseFrames &&
               [composition_.overlayMetalView committedFrameCount] >
                   overlayFrames;
      },
      std::chrono::seconds{5}));
  const auto baseRequests =
      [composition_.baseMetalView nativeDisplayRequestCount];
  const auto overlayRequests =
      [composition_.overlayMetalView nativeDisplayRequestCount];
  const auto drawBaseFrames =
      [composition_.baseMetalView committedFrameCount];
  const auto drawOverlayFrames =
      [composition_.overlayMetalView committedFrameCount];

  sendStroke(composition_.overlayMetalView, NSMakePoint(100, 60),
             NSMakePoint(140, 80), NSMakePoint(180, 100));

  ASSERT_TRUE(runMainLoopUntil(
      [&] {
        return [composition_.overlayMetalView committedFrameCount] >
               drawOverlayFrames;
      },
      std::chrono::seconds{5}));
  runMainLoopFor(std::chrono::milliseconds{100});

  ASSERT_EQ(document->nodes().size(), 2U);
  const auto& annotation = document->nodes().back();
  EXPECT_EQ(annotation.layer, canvas::document::LayerClass::Annotation);
  EXPECT_EQ(annotation.parentId,
            std::optional<canvas::document::NodeId>{"web"});
  EXPECT_EQ(stroke(annotation).coordinateSpace,
            canvas::document::StrokeCoordinateSpace::ParentNormalized);
  EXPECT_EQ(stroke(annotation).points[0].position,
            (canvas::core::Vec2{0.125F, 0.2F}));
  EXPECT_EQ([composition_.baseMetalView nativeDisplayRequestCount],
            baseRequests);
  EXPECT_GT([composition_.overlayMetalView nativeDisplayRequestCount],
            overlayRequests);
  EXPECT_EQ([composition_.baseMetalView committedFrameCount], drawBaseFrames);
  EXPECT_GT([composition_.overlayMetalView committedFrameCount],
            drawOverlayFrames);
}

TEST_F(MacosMouseInput, FullRedrawFailureCommitsBothMetalLayers) {
  auto document = std::make_shared<canvas::document::Document>();
  const auto setterBaseFrames =
      [composition_.baseMetalView committedFrameCount];
  const auto setterOverlayFrames =
      [composition_.overlayMetalView committedFrameCount];
  [composition_ setEditableCanvasDocument:document];
  ASSERT_TRUE(runMainLoopUntil(
      [&] {
        return [composition_.baseMetalView committedFrameCount] >
                   setterBaseFrames &&
               [composition_.overlayMetalView committedFrameCount] >
                   setterOverlayFrames;
      },
      std::chrono::seconds{5}));

  CanvasMetalView* overlay = composition_.overlayMetalView;
  const auto downBaseFrames =
      [composition_.baseMetalView committedFrameCount];
  [overlay mouseDown:mouseEvent(overlay, NSEventTypeLeftMouseDown, 20, 30, 1)];
  ASSERT_EQ(document->nodes().size(), 1U);
  ASSERT_TRUE(runMainLoopUntil(
      [&] {
        return [composition_.baseMetalView committedFrameCount] >
               downBaseFrames;
      },
      std::chrono::seconds{5}));

  const auto baseRequests =
      [composition_.baseMetalView nativeDisplayRequestCount];
  const auto overlayRequests =
      [composition_.overlayMetalView nativeDisplayRequestCount];
  const auto baseFrames =
      [composition_.baseMetalView committedFrameCount];
  const auto overlayFrames =
      [composition_.overlayMetalView committedFrameCount];
  const auto previewId = document->nodes().front().id;
  ASSERT_TRUE(document->mutate(previewId, [](canvas::document::Node& node) {
    std::get<canvas::document::StrokeNode>(node.payload).width = 42.0F;
  }));

  [overlay mouseDragged:mouseEvent(overlay, NSEventTypeLeftMouseDragged, 50,
                                   60, 2)];

  EXPECT_TRUE(document->nodes().empty());
  ASSERT_TRUE(runMainLoopUntil(
      [&] {
        return [composition_.baseMetalView committedFrameCount] > baseFrames &&
               [composition_.overlayMetalView committedFrameCount] >
                   overlayFrames;
      },
      std::chrono::seconds{5}));
  EXPECT_GT([composition_.baseMetalView nativeDisplayRequestCount],
            baseRequests);
  EXPECT_GT([composition_.overlayMetalView nativeDisplayRequestCount],
            overlayRequests);
}

TEST_F(MacosMouseInput, WindowResignCancelsPreview) {
  auto document = std::make_shared<canvas::document::Document>();
  [composition_ setEditableCanvasDocument:document];
  CanvasMetalView* overlay = composition_.overlayMetalView;
  [overlay mouseDown:mouseEvent(overlay, NSEventTypeLeftMouseDown, 20, 30, 1)];
  ASSERT_EQ(document->nodes().size(), 1U);

  [[NSNotificationCenter defaultCenter]
      postNotificationName:NSWindowDidResignKeyNotification
                    object:window_];
  EXPECT_TRUE(document->nodes().empty());
}

TEST_F(MacosMouseInput, ApplicationResignCancelsPreview) {
  auto document = std::make_shared<canvas::document::Document>();
  [composition_ setEditableCanvasDocument:document];
  CanvasMetalView* overlay = composition_.overlayMetalView;
  [overlay mouseDown:mouseEvent(overlay, NSEventTypeLeftMouseDown, 20, 30, 1)];
  ASSERT_EQ(document->nodes().size(), 1U);

  [[NSNotificationCenter defaultCenter]
      postNotificationName:NSApplicationWillResignActiveNotification
                    object:NSApp];
  EXPECT_TRUE(document->nodes().empty());
}

TEST_F(MacosMouseInput, WindowCloseCancelsPreview) {
  auto document = std::make_shared<canvas::document::Document>();
  [composition_ setEditableCanvasDocument:document];
  CanvasMetalView* overlay = composition_.overlayMetalView;
  [overlay mouseDown:mouseEvent(overlay, NSEventTypeLeftMouseDown, 20, 30, 1)];
  ASSERT_EQ(document->nodes().size(), 1U);

  [[NSNotificationCenter defaultCenter]
      postNotificationName:NSWindowWillCloseNotification
                    object:window_];
  EXPECT_TRUE(document->nodes().empty());
}

TEST_F(MacosMouseInput, DetachAndInteractionModeCancelPreview) {
  auto document = std::make_shared<canvas::document::Document>();
  [composition_ setEditableCanvasDocument:document];
  CanvasMetalView* overlay = composition_.overlayMetalView;
  [overlay mouseDown:mouseEvent(overlay, NSEventTypeLeftMouseDown, 20, 30, 1)];
  ASSERT_EQ(document->nodes().size(), 1U);
  composition_.embeddedInteractionEnabled = YES;
  EXPECT_TRUE(document->nodes().empty());

  composition_.embeddedInteractionEnabled = NO;
  [overlay mouseDown:mouseEvent(overlay, NSEventTypeLeftMouseDown, 40, 50, 2)];
  ASSERT_EQ(document->nodes().size(), 1U);
  window_.contentView = nil;
  EXPECT_TRUE(document->nodes().empty());
}

TEST_F(MacosMouseInput, EditableReplacementCancelsOldPreview) {
  auto oldDocument = std::make_shared<canvas::document::Document>();
  auto nextDocument = std::make_shared<canvas::document::Document>();
  [composition_ setEditableCanvasDocument:oldDocument];
  CanvasMetalView* overlay = composition_.overlayMetalView;
  [overlay mouseDown:mouseEvent(overlay, NSEventTypeLeftMouseDown, 20, 30, 1)];
  ASSERT_EQ(oldDocument->nodes().size(), 1U);

  [composition_ setEditableCanvasDocument:nextDocument];
  EXPECT_TRUE(oldDocument->nodes().empty());
  EXPECT_TRUE(nextDocument->nodes().empty());
}

TEST_F(MacosMouseInput, ActiveCompositionDeallocRollsBackPreviewDirectly) {
  auto document = std::make_shared<canvas::document::Document>();
  __weak CanvasCompositionView* weakComposition = nil;
  @autoreleasepool {
    CanvasCompositionView* transient =
        [[CanvasCompositionView alloc] initWithFrame:NSMakeRect(0, 0, 160, 120)];
    ASSERT_NE(transient, nil);
    weakComposition = transient;
    [transient setEditableCanvasDocument:document];
    CanvasMetalView* overlay = transient.overlayMetalView;
    [overlay mouseDown:mouseEvent(overlay, NSEventTypeLeftMouseDown, 20, 30, 1)];
    ASSERT_EQ(document->nodes().size(), 1U);
    overlay = nil;
    transient = nil;
  }

  EXPECT_EQ(weakComposition, nil);
  EXPECT_TRUE(document->nodes().empty())
      << "composition teardown must not depend on its zeroed weak delegate";
}

TEST_F(MacosMouseInput, ReadOnlyDocumentDoesNotAcceptMouseInput) {
  auto document = std::make_shared<canvas::document::Document>();
  [composition_ setCanvasDocument:document];
  sendStroke(composition_.overlayMetalView, NSMakePoint(20, 30),
             NSMakePoint(50, 60), NSMakePoint(80, 90));
  EXPECT_TRUE(document->nodes().empty());
}

TEST_F(MacosMouseInput, OrphanAndRightMouseEventsDoNotDraw) {
  auto document = std::make_shared<canvas::document::Document>();
  [composition_ setEditableCanvasDocument:document];
  CanvasMetalView* overlay = composition_.overlayMetalView;
  [overlay mouseDragged:mouseEvent(overlay, NSEventTypeLeftMouseDragged, 20, 30,
                                   1)];
  [overlay mouseUp:mouseEvent(overlay, NSEventTypeLeftMouseUp, 40, 50, 2)];
  [overlay rightMouseDown:mouseEvent(overlay, NSEventTypeRightMouseDown, 60, 70,
                                     3)];
  EXPECT_TRUE(document->nodes().empty());
}

}  // namespace
