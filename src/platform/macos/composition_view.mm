#include "platform/macos/composition_view.h"

#include "platform/macos/macos_mouse_session.h"
#include "platform/macos/macos_whiteboard_input.h"
#include "platform/macos/metal_host.h"
#include "platform/macos/metal_view.h"

#import <QuartzCore/CALayer.h>

#include <exception>
#include <memory>
#include <utility>

namespace {

void requireAppKitMainThread() {
  if (![NSThread isMainThread]) std::terminate();
}

canvas::macos::RawMacMouseEvent rawMouseEvent(
    NSView* view, NSEvent* event, canvas::macos::MacMousePhase phase) {
  const NSPoint local = [view convertPoint:event.locationInWindow fromView:nil];
  const NSRect bounds = view.bounds;
  double backingScale = view.window.backingScaleFactor;
  if (!(backingScale > 0.0)) {
    NSScreen* screen = view.window.screen;
    if (screen == nil) screen = NSScreen.mainScreen;
    backingScale = screen != nil ? screen.backingScaleFactor : 1.0;
  }

  canvas::macos::RawMacMouseEvent raw;
  raw.localPosition = {static_cast<float>(local.x),
                       static_cast<float>(local.y)};
  raw.boundsOrigin = {static_cast<float>(bounds.origin.x),
                      static_cast<float>(bounds.origin.y)};
  raw.boundsSize = {static_cast<float>(bounds.size.width),
                    static_cast<float>(bounds.size.height)};
  raw.viewFlipped = view.isFlipped;
  raw.backingScale = backingScale;
  raw.timestampSeconds = event.timestamp;
  raw.pressure = static_cast<double>(event.pressure);
  raw.buttonNumber = static_cast<std::int64_t>(event.buttonNumber);
  raw.eventNumber = static_cast<std::int64_t>(event.eventNumber);
  raw.deviceId = 0;
  raw.phase = phase;
  return raw;
}

}  // namespace

@class CanvasPointerMetalView;

@protocol CanvasPointerMetalViewDelegate <NSObject>
- (void)canvasPointerMetalView:(CanvasPointerMetalView*)view
              didProduceSample:(const canvas::input::PointerSample&)sample;
@end

@interface CanvasPointerMetalView : CanvasMetalView
@property(nonatomic, weak) id<CanvasPointerMetalViewDelegate>
    pointerInputDelegate;
- (canvas::macos::MacosMouseSessionOutput)takeMouseCancellationOutput;
- (void)cancelActiveMouseSession;
@end

@implementation CanvasPointerMetalView {
  canvas::macos::MacosMouseSession mouseSession_;
  __weak id<CanvasPointerMetalViewDelegate> pointerInputDelegate_;
  __strong id applicationResignObserver_;
  __strong id windowResignObserver_;
  __strong id windowCloseObserver_;
}

@synthesize pointerInputDelegate = pointerInputDelegate_;

- (instancetype)initWithFrame:(NSRect)frame
                   surfaceRole:(canvas::macos::MetalSurfaceRole)surfaceRole
               renderResources:(std::shared_ptr<canvas::macos::MetalRenderResources>)
                                  renderResources {
  self = [super initWithFrame:frame
                   surfaceRole:surfaceRole
               renderResources:std::move(renderResources)];
  if (self == nil) return nil;

  NSNotificationCenter* center = NSNotificationCenter.defaultCenter;
  __weak CanvasPointerMetalView* weakSelf = self;
  applicationResignObserver_ = [center
      addObserverForName:NSApplicationWillResignActiveNotification
                  object:nil
                   queue:nil
              usingBlock:^(NSNotification*) {
                [weakSelf cancelActiveMouseSession];
              }];
  windowResignObserver_ = [center
      addObserverForName:NSWindowDidResignKeyNotification
                  object:nil
                   queue:nil
              usingBlock:^(NSNotification* notification) {
                CanvasPointerMetalView* view = weakSelf;
                if (view != nil && notification.object == view.window) {
                  [view cancelActiveMouseSession];
                }
              }];
  windowCloseObserver_ = [center
      addObserverForName:NSWindowWillCloseNotification
                  object:nil
                   queue:nil
              usingBlock:^(NSNotification* notification) {
                CanvasPointerMetalView* view = weakSelf;
                if (view != nil && notification.object == view.window) {
                  [view cancelActiveMouseSession];
                }
              }];
  return self;
}

- (void)dealloc {
  requireAppKitMainThread();
  [self cancelActiveMouseSession];
  NSNotificationCenter* center = NSNotificationCenter.defaultCenter;
  if (applicationResignObserver_ != nil) {
    [center removeObserver:applicationResignObserver_];
  }
  if (windowResignObserver_ != nil) {
    [center removeObserver:windowResignObserver_];
  }
  if (windowCloseObserver_ != nil) {
    [center removeObserver:windowCloseObserver_];
  }
}

- (BOOL)acceptsFirstResponder {
  return YES;
}

- (void)dispatchOutput:(const canvas::macos::MacosMouseSessionOutput&)output {
  __strong id<CanvasPointerMetalViewDelegate> delegate =
      self.pointerInputDelegate;
  if (delegate == nil) return;
  for (std::size_t index = 0; index < output.size(); ++index) {
    [delegate canvasPointerMetalView:self didProduceSample:output[index]];
  }
}

- (void)consumeEvent:(NSEvent*)event
               phase:(canvas::macos::MacMousePhase)phase {
  requireAppKitMainThread();
  if (event == nil) return;
  const auto output = mouseSession_.consume(rawMouseEvent(self, event, phase));
  [self dispatchOutput:output];
}

- (void)mouseDown:(NSEvent*)event {
  if (self.window != nil) [self.window makeFirstResponder:self];
  [self consumeEvent:event phase:canvas::macos::MacMousePhase::Down];
}

- (void)mouseDragged:(NSEvent*)event {
  [self consumeEvent:event phase:canvas::macos::MacMousePhase::Move];
}

- (void)mouseUp:(NSEvent*)event {
  [self consumeEvent:event phase:canvas::macos::MacMousePhase::Up];
}

- (void)cancelOperation:(id)sender {
  (void)sender;
  [self cancelActiveMouseSession];
}

- (void)cancelActiveMouseSession {
  const auto output = [self takeMouseCancellationOutput];
  [self dispatchOutput:output];
}

- (canvas::macos::MacosMouseSessionOutput)takeMouseCancellationOutput {
  requireAppKitMainThread();
  canvas::macos::MacosMouseSessionOutput output;
  const auto sample = mouseSession_.cancel();
  if (!sample) return output;
  output.samples[0] = *sample;
  output.count = 1;
  return output;
}

- (void)viewWillMoveToWindow:(NSWindow*)newWindow {
  if (self.window != nil && newWindow != self.window) {
    [self cancelActiveMouseSession];
  }
  [super viewWillMoveToWindow:newWindow];
}

@end

@interface CanvasCompositionView () <CanvasPointerMetalViewDelegate>
- (void)applyInputResult:
    (const canvas::macos::MacosWhiteboardInputResult&)result;
- (void)cancelPointerInput;
@end

// Document/Metal geometry is expressed from the top-left. Keeping the
// middle native-view host flipped makes a WKWebView frame use the same point
// coordinates as ink and chrome instead of AppKit's default bottom-left
// coordinate system.
@interface CanvasEmbeddedContainerView : NSView
@end

@implementation CanvasEmbeddedContainerView

- (BOOL)isFlipped {
  return YES;
}

@end

@implementation CanvasCompositionView {
  CanvasMetalView* baseMetalView_;
  NSView* embeddedContainerView_;
  CanvasMetalView* overlayMetalView_;
  std::shared_ptr<canvas::macos::MetalRenderResources> renderResources_;
  std::unique_ptr<canvas::macos::MacosWhiteboardInput> whiteboardInput_;
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
  embeddedContainerView_ =
      [[CanvasEmbeddedContainerView alloc] initWithFrame:bounds];
  overlayMetalView_ = [[CanvasPointerMetalView alloc]
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
  ((CanvasPointerMetalView*)overlayMetalView_).pointerInputDelegate = self;
  embeddedInteractionEnabled_ = NO;
  return self;
}

- (void)dealloc {
  requireAppKitMainThread();
  CanvasPointerMetalView* pointerView =
      (CanvasPointerMetalView*)overlayMetalView_;
  canvas::macos::MacosMouseSessionOutput cancellation;
  if (pointerView != nil) {
    pointerView.pointerInputDelegate = nil;
    cancellation = [pointerView takeMouseCancellationOutput];
  }
  if (whiteboardInput_) {
    for (std::size_t index = 0; index < cancellation.size(); ++index) {
      (void)whiteboardInput_->consume(cancellation[index]);
    }
    if (whiteboardInput_->active()) {
      (void)whiteboardInput_->setMode(canvas::input::InputMode::Interact);
    }
  }
  whiteboardInput_.reset();
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
  requireAppKitMainThread();
  if (embeddedInteractionEnabled_ == enabled) return;
  if (enabled) [self cancelPointerInput];
  if (whiteboardInput_) {
    const auto result = whiteboardInput_->setMode(
        enabled ? canvas::input::InputMode::Interact
                : canvas::input::InputMode::Draw);
    [self applyInputResult:result];
  }
  embeddedInteractionEnabled_ = enabled;
}

- (void)setCanvasDocument:
    (std::shared_ptr<const canvas::document::Document>)document {
  requireAppKitMainThread();
  [self cancelPointerInput];
  if (whiteboardInput_) {
    const auto result =
        whiteboardInput_->setMode(canvas::input::InputMode::Interact);
    [self applyInputResult:result];
    whiteboardInput_.reset();
  }
  [baseMetalView_ setCanvasDocument:document];
  [overlayMetalView_ setCanvasDocument:std::move(document)];
}

- (void)setEditableCanvasDocument:
    (std::shared_ptr<canvas::document::Document>)document {
  requireAppKitMainThread();
  if (!document) {
    std::shared_ptr<const canvas::document::Document> empty;
    [self setCanvasDocument:std::move(empty)];
    return;
  }

  [self cancelPointerInput];
  if (whiteboardInput_) {
    const auto replacement =
        whiteboardInput_->replaceDocument(document);
    [self applyInputResult:replacement];
  } else {
    whiteboardInput_ =
        std::make_unique<canvas::macos::MacosWhiteboardInput>(document);
  }
  const auto modeResult = whiteboardInput_->setMode(
      embeddedInteractionEnabled_ ? canvas::input::InputMode::Interact
                                  : canvas::input::InputMode::Draw);
  [self applyInputResult:modeResult];
  [baseMetalView_ setCanvasDocument:document];
  [overlayMetalView_ setCanvasDocument:std::move(document)];
}

- (void)cancelPointerInput {
  [(CanvasPointerMetalView*)overlayMetalView_ cancelActiveMouseSession];
}

- (void)canvasPointerMetalView:(CanvasPointerMetalView*)view
              didProduceSample:(const canvas::input::PointerSample&)sample {
  requireAppKitMainThread();
  if (view != overlayMetalView_ || !whiteboardInput_) return;
  const auto result = whiteboardInput_->consume(sample);
  [self applyInputResult:result];
}

- (void)applyInputResult:
    (const canvas::macos::MacosWhiteboardInputResult&)result {
  if (result.fullRedraw) {
    [baseMetalView_ invalidateCanvas];
    [overlayMetalView_ invalidateCanvas];
    return;
  }
  if (result.kind ==
      canvas::macos::MacosWhiteboardInputResultKind::Ignored) {
    return;
  }
  if (result.layer) {
    switch (*result.layer) {
      case canvas::document::LayerClass::Base:
        [baseMetalView_ invalidateCanvas];
        return;
      case canvas::document::LayerClass::Annotation:
      case canvas::document::LayerClass::Chrome:
        [overlayMetalView_ invalidateCanvas];
        return;
      case canvas::document::LayerClass::Embedded:
        break;
    }
  }
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
