#import <AVFoundation/AVFoundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <UIKit/UIKit.h>
#import <WebKit/WebKit.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <mach/mach.h>

#include "canvas/poc03/large_scene.h"
#include "canvas/poc05/hybrid_surface.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkSurface.h"
#include "include/gpu/ganesh/GrBackendSurface.h"
#include "include/gpu/ganesh/GrDirectContext.h"
#include "include/gpu/ganesh/SkSurfaceGanesh.h"
#include "include/gpu/ganesh/mtl/GrMtlBackendContext.h"
#include "include/gpu/ganesh/mtl/GrMtlBackendSurface.h"
#include "include/gpu/ganesh/mtl/GrMtlDirectContext.h"
#include "include/ports/SkCFObject.h"
#include "skia_large_scene_renderer.h"

namespace {

using namespace canvas::poc03;
using namespace canvas::poc05;

constexpr CanvasViewHandle kViewHandle = 1U;

class AppleProjector final : public RuntimeViewProjector {
 public:
  bool worldToViewLogical(CanvasViewHandle view, CanvasPointF world,
                          CanvasPointF* logical,
                          std::string* error) const override {
    if (view != kViewHandle || logical == nullptr || error == nullptr ||
        !std::isfinite(world.x) || !std::isfinite(world.y)) {
      if (error != nullptr) *error = "invalid Apple projection request";
      return false;
    }
    logical->x = (world.x - pan_x) * zoom;
    logical->y = (world.y - pan_y) * zoom;
    return true;
  }

  float pan_x = 0.0F;
  float pan_y = 0.0F;
  float zoom = 1.0F;
};

class ApplePlacementBackend final : public PlatformOverlayBackend {
 public:
  bool create(ExternalSurfaceId id, SurfaceKind kind,
              std::string* error) override {
    if (id == 0U || commands.contains(id)) {
      if (error != nullptr) *error = "duplicate Apple overlay";
      return false;
    }
    kinds[id] = kind;
    commands[id] = PlacementCommand{};
    return true;
  }

  bool apply(const PlacementCommand& command,
             std::string* error) override {
    if (!commands.contains(command.id)) {
      if (error != nullptr) *error = "Apple overlay is not materialized";
      return false;
    }
    commands[command.id] = command;
    return true;
  }

  void destroy(ExternalSurfaceId id) override {
    commands.erase(id);
    kinds.erase(id);
  }

  bool focus(ExternalSurfaceId id, std::string* error) override {
    if (!commands.contains(id)) {
      if (error != nullptr) *error = "Apple overlay is not focusable";
      return false;
    }
    focused = id;
    return true;
  }

  void focusCanvas() override { focused = 0U; }

  const PlacementCommand* find(ExternalSurfaceId id) const {
    const auto found = commands.find(id);
    return found == commands.end() ? nullptr : &found->second;
  }

  std::unordered_map<ExternalSurfaceId, SurfaceKind> kinds;
  std::unordered_map<ExternalSurfaceId, PlacementCommand> commands;
  ExternalSurfaceId focused = 0U;
};

ExternalSurfacePlaceholder WebPlaceholder() {
  ExternalSurfacePlaceholder placeholder;
  placeholder.id = 1U;
  placeholder.kind = SurfaceKind::kWebView;
  placeholder.worldBounds = CanvasRectF{180.0F, 120.0F, 380.0F, 230.0F};
  placeholder.worldClip = CanvasRectF{190.0F, 130.0F, 360.0F, 210.0F};
  placeholder.order = 1U;
  placeholder.pageId = 1U;
  return placeholder;
}

ExternalSurfacePlaceholder VideoPlaceholder() {
  ExternalSurfacePlaceholder placeholder;
  placeholder.id = 2U;
  placeholder.kind = SurfaceKind::kVideo;
  placeholder.worldBounds = CanvasRectF{680.0F, 300.0F, 320.0F, 180.0F};
  placeholder.order = 2U;
  placeholder.pageId = 1U;
  return placeholder;
}

std::string JsonEscape(const std::string& value) {
  std::string result;
  for (const char character : value) {
    if (character == '\\' || character == '"') result.push_back('\\');
    result.push_back(character);
  }
  return result;
}

double ProcessFootprintMib() {
  task_vm_info_data_t info{};
  mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
  if (task_info(mach_task_self(), TASK_VM_INFO,
                reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS) {
    return 0.0;
  }
  return static_cast<double>(info.phys_footprint) / (1024.0 * 1024.0);
}

}  // namespace

@interface Poc05VideoView : UIView
- (void)stopPlayback;
@end

@implementation Poc05VideoView {
  AVSampleBufferDisplayLayer* _displayLayer;
  CADisplayLink* _displayLink;
  CMTime _presentationTime;
  CVPixelBufferRef _pixelBuffer;
}

+ (Class)layerClass { return AVSampleBufferDisplayLayer.class; }

- (instancetype)initWithFrame:(CGRect)frame {
  if ((self = [super initWithFrame:frame])) {
    _displayLayer = (AVSampleBufferDisplayLayer*)self.layer;
    _displayLayer.videoGravity = AVLayerVideoGravityResizeAspectFill;
    _displayLayer.backgroundColor = UIColor.blackColor.CGColor;
    NSDictionary* attributes = @{
      (NSString*)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA),
      (NSString*)kCVPixelBufferWidthKey : @320,
      (NSString*)kCVPixelBufferHeightKey : @180,
      (NSString*)kCVPixelBufferIOSurfacePropertiesKey : @{},
    };
    CVPixelBufferCreate(kCFAllocatorDefault, 320, 180,
                        kCVPixelFormatType_32BGRA,
                        (__bridge CFDictionaryRef)attributes, &_pixelBuffer);
    if (_pixelBuffer != nullptr) {
      CVPixelBufferLockBaseAddress(_pixelBuffer, 0);
      auto* pixels = static_cast<std::uint8_t*>(
          CVPixelBufferGetBaseAddress(_pixelBuffer));
      const size_t stride = CVPixelBufferGetBytesPerRow(_pixelBuffer);
      for (size_t y = 0; y < 180; ++y) {
        for (size_t x = 0; x < 320; ++x) {
          std::uint8_t* pixel = pixels + y * stride + x * 4U;
          pixel[0] = static_cast<std::uint8_t>(80U + (x % 120U));
          pixel[1] = static_cast<std::uint8_t>(40U + (y % 160U));
          pixel[2] = static_cast<std::uint8_t>(180U - (x % 100U));
          pixel[3] = 255U;
        }
      }
      CVPixelBufferUnlockBaseAddress(_pixelBuffer, 0);
    }
    _displayLink = [CADisplayLink displayLinkWithTarget:self
                                               selector:@selector(tick:)];
    _displayLink.preferredFramesPerSecond = 30;
    [_displayLink addToRunLoop:NSRunLoop.mainRunLoop
                       forMode:NSRunLoopCommonModes];
  }
  return self;
}

- (void)tick:(CADisplayLink*)link {
  (void)link;
  if (_pixelBuffer == nullptr || _displayLayer.status == AVQueuedSampleBufferRenderingStatusFailed) {
    [_displayLayer flush];
  }
  CMVideoFormatDescriptionRef description = nullptr;
  CMVideoFormatDescriptionCreateForImageBuffer(kCFAllocatorDefault,
                                                _pixelBuffer, &description);
  CMSampleTimingInfo timing{CMTimeMake(1, 30), _presentationTime,
                            kCMTimeInvalid};
  CMSampleBufferRef buffer = nullptr;
  CMSampleBufferCreateReadyWithImageBuffer(kCFAllocatorDefault, _pixelBuffer,
                                           description, &timing, &buffer);
  _presentationTime = CMTimeAdd(_presentationTime, CMTimeMake(1, 30));
  [_displayLayer enqueueSampleBuffer:buffer];
  if (buffer != nullptr) CFRelease(buffer);
  if (description != nullptr) CFRelease(description);
}

- (void)dealloc {
  [self stopPlayback];
  if (_pixelBuffer != nullptr) CVPixelBufferRelease(_pixelBuffer);
}

- (void)stopPlayback {
  [_displayLink invalidate];
  _displayLink = nil;
  [_displayLayer flush];
}

@end

@interface CanvasPoc05AppleView : UIView <WKNavigationDelegate,
                                          UIGestureRecognizerDelegate>
@end

@implementation CanvasPoc05AppleView {
  UIView* _canvasView;
  UIView* _overlayLayer;
  WKWebView* _webView;
  Poc05VideoView* _videoView;
  UILabel* _failureView;
  UILabel* _statusLabel;
  UIButton* _pageButton;
  UIButton* _failureButton;
  UIButton* _recreateButton;
  CADisplayLink* _displayLink;
  CAMetalLayer* _metalLayer;
  id<MTLDevice> _device;
  id<MTLCommandQueue> _queue;
  sk_sp<GrDirectContext> _context;
  std::unique_ptr<Document> _document;
  std::unique_ptr<RuntimeScene> _scene;
  std::unique_ptr<AppleProjector> _projector;
  std::unique_ptr<ApplePlacementBackend> _backend;
  std::unique_ptr<ExternalSurfaceRegistry> _registry;
  std::uint64_t _frameRevision;
  std::uint64_t _viewportRevision;
  std::uint32_t _targetGeneration;
  std::uint64_t _placementFrames;
  float _maxPlacementErrorPx;
  std::uint64_t _activePage;
  BOOL _webVisible;
  BOOL _failureMode;
  BOOL _backgrounded;
  CGPoint _lastPanPoint;
  CGPoint _lastPinchPoint;
  CGFloat _lastPinchScale;
  double _warmPssMib;
}

- (instancetype)initWithFrame:(CGRect)frame {
  if ((self = [super initWithFrame:frame])) {
    self.backgroundColor = [UIColor colorWithRed:244.0 / 255.0
                                           green:245.0 / 255.0
                                            blue:247.0 / 255.0 alpha:1.0];
    _frameRevision = 1U;
    _viewportRevision = 1U;
    _targetGeneration = 1U;
    _activePage = 1U;
    _webVisible = YES;

    _canvasView = [[UIView alloc] initWithFrame:self.bounds];
    _canvasView.autoresizingMask = UIViewAutoresizingFlexibleWidth |
                                   UIViewAutoresizingFlexibleHeight;
    _metalLayer = [CAMetalLayer layer];
    _metalLayer.frame = _canvasView.bounds;
    [_canvasView.layer addSublayer:_metalLayer];
    [self addSubview:_canvasView];

    _overlayLayer = [[UIView alloc] initWithFrame:self.bounds];
    _overlayLayer.autoresizingMask = UIViewAutoresizingFlexibleWidth |
                                    UIViewAutoresizingFlexibleHeight;
    _overlayLayer.userInteractionEnabled = YES;
    [self addSubview:_overlayLayer];

    WKWebViewConfiguration* configuration = [[WKWebViewConfiguration alloc] init];
    _webView = [[WKWebView alloc] initWithFrame:CGRectZero
                                  configuration:configuration];
    _webView.navigationDelegate = self;
    [_webView loadHTMLString:@"<!doctype html><html><meta name='viewport' content='width=device-width'><body contenteditable='true' style='margin:0;padding:16px;font:17px -apple-system;background:white;color:#172033'><b>Real WKWebView</b><p>Edit or scroll this text to validate focus and IME handoff.</p><p>Two-finger Canvas pan/zoom stays native.</p><p>Line 4</p><p>Line 5</p></body></html>" baseURL:nil];
    [_overlayLayer addSubview:_webView];

    _videoView = [[Poc05VideoView alloc] initWithFrame:CGRectZero];
    [_overlayLayer addSubview:_videoView];

    _failureView = [[UILabel alloc] initWithFrame:CGRectZero];
    _failureView.text = @"Web content unavailable\nTap Recover";
    _failureView.numberOfLines = 2;
    _failureView.textAlignment = NSTextAlignmentCenter;
    _failureView.textColor = UIColor.whiteColor;
    _failureView.backgroundColor = [UIColor colorWithRed:0.58 green:0.14 blue:0.18 alpha:1.0];
    _failureView.hidden = YES;
    [_overlayLayer addSubview:_failureView];

    [self configureToolbar];
    UIPanGestureRecognizer* pan = [[UIPanGestureRecognizer alloc]
        initWithTarget:self action:@selector(pan:)];
    pan.maximumNumberOfTouches = 1;
    pan.delegate = self;
    [_canvasView addGestureRecognizer:pan];
    UIPinchGestureRecognizer* pinch = [[UIPinchGestureRecognizer alloc]
        initWithTarget:self action:@selector(pinch:)];
    pinch.delegate = self;
    [self addGestureRecognizer:pinch];

    [[NSNotificationCenter defaultCenter]
        addObserver:self selector:@selector(background:)
              name:UIApplicationDidEnterBackgroundNotification object:nil];
    [[NSNotificationCenter defaultCenter]
        addObserver:self selector:@selector(foreground:)
              name:UIApplicationWillEnterForegroundNotification object:nil];
  }
  return self;
}

- (void)configureToolbar {
  UIView* toolbar = [[UIView alloc] initWithFrame:CGRectMake(0, 0, self.bounds.size.width, 58)];
  toolbar.autoresizingMask = UIViewAutoresizingFlexibleWidth;
  toolbar.backgroundColor = [UIColor colorWithWhite:0.97 alpha:0.96];
  [self addSubview:toolbar];
  _statusLabel = [[UILabel alloc] initWithFrame:CGRectMake(12, 4, 360, 25)];
  _statusLabel.font = [UIFont boldSystemFontOfSize:14];
  _statusLabel.text = @"Axiom POC-05 · Apple Native Shell";
  [toolbar addSubview:_statusLabel];
  _pageButton = [self button:@"Page 2" x:12 action:@selector(togglePage:)];
  _failureButton = [self button:@"Fail Web" x:102 action:@selector(toggleFailure:)];
  _recreateButton = [self button:@"Recreate" x:212 action:@selector(recreate:)];
  [toolbar addSubview:_pageButton];
  [toolbar addSubview:_failureButton];
  [toolbar addSubview:_recreateButton];
}

- (UIButton*)button:(NSString*)title x:(CGFloat)x action:(SEL)action {
  UIButton* button = [UIButton buttonWithType:UIButtonTypeSystem];
  button.frame = CGRectMake(x, 29, 100, 28);
  [button setTitle:title forState:UIControlStateNormal];
  [button addTarget:self action:action forControlEvents:UIControlEventTouchUpInside];
  return button;
}

- (void)didMoveToWindow {
  [super didMoveToWindow];
  if (self.window == nil || _context) return;
  [self initializeRuntime];
}

- (void)layoutSubviews {
  [super layoutSubviews];
  _metalLayer.frame = _canvasView.bounds;
  const CGFloat scale = self.window.screen.scale ?: UIScreen.mainScreen.scale;
  _metalLayer.contentsScale = scale;
  _metalLayer.drawableSize = CGSizeMake(_canvasView.bounds.size.width * scale,
                                        _canvasView.bounds.size.height * scale);
  ++_targetGeneration;
}

- (void)initializeRuntime {
  _device = MTLCreateSystemDefaultDevice();
  _queue = [_device newCommandQueue];
  _metalLayer.device = _device;
  _metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
  _metalLayer.framebufferOnly = NO;
  GrMtlBackendContext backend;
  backend.fDevice = sk_ret_cfp((__bridge GrMTLHandle)_device);
  backend.fQueue = sk_ret_cfp((__bridge GrMTLHandle)_queue);
  _context = GrDirectContexts::MakeMetal(backend);
  _document = std::make_unique<Document>(GenerateDocument(
      GeneratorConfig{100000U, UINT64_C(0x43414e5641533035), 1000U, 32.0F}));
  _scene = std::make_unique<RuntimeScene>(SceneCompiler().CompileFull(*_document));
  _projector = std::make_unique<AppleProjector>();
  _backend = std::make_unique<ApplePlacementBackend>();
  _registry = std::make_unique<ExternalSurfaceRegistry>(*_projector, *_backend);
  std::string error;
  if (!_registry->registerSurface(WebPlaceholder(), &error) ||
      !_registry->registerSurface(VideoPlaceholder(), &error) ||
      !_registry->markReady(1U, &error) || !_registry->markReady(2U, &error)) {
    NSLog(@"CANVAS_POC05_APPLE_FAILURE %s", error.c_str());
    return;
  }
  _displayLink = [CADisplayLink displayLinkWithTarget:self
                                             selector:@selector(frame:)];
  _displayLink.preferredFrameRateRange = CAFrameRateRangeMake(60, 120, 60);
  [_displayLink addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
  _warmPssMib = ProcessFootprintMib();
}

- (ViewState)currentView {
  const CGFloat dpr = self.window.screen.scale ?: UIScreen.mainScreen.scale;
  const std::uint32_t width = static_cast<std::uint32_t>(
      std::round(_canvasView.bounds.size.width * dpr));
  const std::uint32_t height = static_cast<std::uint32_t>(
      std::round(_canvasView.bounds.size.height * dpr));
  return ViewState{kViewHandle, _viewportRevision, 1U,
                   Bounds{_projector->pan_x, _projector->pan_y,
                          static_cast<float>(
                              _projector->pan_x + static_cast<float>(width) /
                              (_projector->zoom * dpr)),
                          static_cast<float>(
                              _projector->pan_y + static_cast<float>(height) /
                              (_projector->zoom * dpr))},
                   _projector->zoom, static_cast<float>(dpr), width, height};
}

- (BOOL)render:(std::string*)error {
  @autoreleasepool {
    if (!_context || !_scene || _backgrounded) return YES;
    id<CAMetalDrawable> drawable = [_metalLayer nextDrawable];
    if (drawable == nil) return YES;
    const ViewState view = [self currentView];
    GrMtlTextureInfo textureInfo;
    textureInfo.fTexture = sk_ret_cfp((__bridge GrMTLHandle)drawable.texture);
    const GrBackendRenderTarget target = GrBackendRenderTargets::MakeMtl(
        static_cast<int>(view.pixel_width),
        static_cast<int>(view.pixel_height), textureInfo);
    sk_sp<SkSurface> surface = SkSurfaces::WrapBackendRenderTarget(
        _context.get(), target, kTopLeft_GrSurfaceOrigin,
        kBGRA_8888_SkColorType, SkColorSpace::MakeSRGB(), nullptr);
    if (!surface) {
      *error = "Skia could not wrap the Apple drawable";
      return NO;
    }
    const auto query = QueryView(*_scene, view, std::nullopt);
    DrawLargeScene(*surface->getCanvas(), *_scene, view,
                   BuildFrame(*_scene, query, {}));
    _context->flushAndSubmit(surface.get(), GrSyncCpu::kNo);
    id<MTLCommandBuffer> present = [_queue commandBuffer];
    [present presentDrawable:drawable];
    [present commit];
    return YES;
  }
}

- (void)frame:(CADisplayLink*)link {
  (void)link;
  std::string error;
  if (![self render:&error]) {
    NSLog(@"CANVAS_POC05_APPLE_FAILURE %s", error.c_str());
    return;
  }
  if (![self applyPlacements:&error]) {
    NSLog(@"CANVAS_POC05_APPLE_FAILURE %s", error.c_str());
    return;
  }
  if (_placementFrames == 120U) [self runCorpus];
}

- (BOOL)applyPlacements:(std::string*)error {
  const ViewState view = [self currentView];
  CanvasCameraStateV1 camera{};
  camera.struct_size = sizeof(camera);
  camera.abi_version = CANVAS_RUNTIME_ABI_VERSION;
  camera.scale = _projector->zoom;
  camera.world_origin_x = _projector->pan_x;
  camera.world_origin_y = _projector->pan_y;
  camera.viewport_revision = _viewportRevision;
  CanvasSurfaceStateV1 surface{};
  surface.struct_size = sizeof(surface);
  surface.abi_version = CANVAS_RUNTIME_ABI_VERSION;
  surface.width_pixels = view.pixel_width;
  surface.height_pixels = view.pixel_height;
  surface.device_pixel_ratio = view.dpr;
  surface.target_generation = _targetGeneration;
  surface.color_space = kCanvasColorSpaceSrgb;
  surface.orientation = kCanvasSurfaceOrientationIdentity;
  _registry->setActivePage(_activePage);
  _registry->setBackgrounded(_backgrounded);
  if (!_registry->setHidden(1U, !_webVisible, error)) return NO;
  const RuntimeViewFrame frame{kViewHandle, camera, surface, ++_frameRevision};
  if (!_registry->applyFrame(frame, error)) return NO;
  [self apply:_backend->find(1U) to:_webView fallback:_failureView];
  [self apply:_backend->find(2U) to:_videoView fallback:nil];
  ++_placementFrames;
  return YES;
}

- (void)apply:(const PlacementCommand*)command to:(UIView*)content
      fallback:(UIView*)fallback {
  if (command == nullptr) {
    content.hidden = YES;
    fallback.hidden = YES;
    return;
  }
  const CGFloat dpr = self.window.screen.scale ?: UIScreen.mainScreen.scale;
  const CGRect frame = CGRectMake(command->deviceBounds.x / dpr,
                                  command->deviceBounds.y / dpr,
                                  command->deviceBounds.width / dpr,
                                  command->deviceBounds.height / dpr);
  content.frame = frame;
  CAShapeLayer* mask = [CAShapeLayer layer];
  mask.path = [UIBezierPath bezierPathWithRect:CGRectMake(
      command->relativeDeviceClip.x / dpr,
      command->relativeDeviceClip.y / dpr,
      command->relativeDeviceClip.width / dpr,
      command->relativeDeviceClip.height / dpr)].CGPath;
  content.layer.mask = mask;
  content.alpha = command->opacity;
  content.hidden = !(command->visible && command->contentVisible) || _failureMode;
  if (fallback != nil) {
    fallback.frame = frame;
    fallback.layer.mask = mask;
    fallback.hidden = !(command->visible &&
                        (command->failurePlaceholder || _failureMode));
  }
  _maxPlacementErrorPx = std::max(
      _maxPlacementErrorPx,
      static_cast<float>(std::abs(content.frame.origin.x * dpr -
                                  command->deviceBounds.x)));
}

- (void)transformFrom:(CGPoint)previous to:(CGPoint)current scale:(CGFloat)scale {
  ViewportTransform transform{_projector->pan_x, _projector->pan_y,
                              _projector->zoom};
  const CGFloat dpr = self.window.screen.scale ?: UIScreen.mainScreen.scale;
  std::string error;
  if (!ApplyViewportGesture(
          ViewportGesture{static_cast<float>(previous.x * dpr),
                          static_cast<float>(previous.y * dpr),
                          static_cast<float>(current.x * dpr),
                          static_cast<float>(current.y * dpr),
                          static_cast<float>(scale)},
          static_cast<float>(dpr), 0.25F, 4.0F,
          Bounds{0.0F, 0.0F, 30000.0F, 3000.0F}, &transform, &error)) {
    NSLog(@"CANVAS_POC05_APPLE_INTERACTIVE_FAILURE %s", error.c_str());
    return;
  }
  _projector->pan_x = transform.pan_x;
  _projector->pan_y = transform.pan_y;
  _projector->zoom = transform.zoom;
  ++_viewportRevision;
}

- (CGPoint)canvasPointFromSelf:(CGPoint)point {
  // Gesture recognizers attached to the root view report points in the root
  // coordinate system.  The runtime viewport, however, is the Canvas view
  // (which can be offset by the toolbar and safe-area insets).  Convert before
  // applying the pixel-space anchor calculation; otherwise pinch zoom drifts
  // toward the root view's origin on devices with a non-zero inset.
  return [self convertPoint:point toView:_canvasView];
}

- (void)pan:(UIPanGestureRecognizer*)recognizer {
  const CGPoint current = [self canvasPointFromSelf:
      [recognizer locationInView:self]];
  if (recognizer.state == UIGestureRecognizerStateBegan) {
    _lastPanPoint = current;
  } else if (recognizer.state == UIGestureRecognizerStateChanged) {
    [self transformFrom:_lastPanPoint to:current scale:1.0];
    _lastPanPoint = current;
  }
}

- (void)pinch:(UIPinchGestureRecognizer*)recognizer {
  const CGPoint current = [self canvasPointFromSelf:
      [recognizer locationInView:self]];
  if (recognizer.state == UIGestureRecognizerStateBegan) {
    _lastPinchPoint = current;
    _lastPinchScale = recognizer.scale;
  } else if (recognizer.state == UIGestureRecognizerStateChanged) {
    const CGFloat delta = recognizer.scale / std::max(_lastPinchScale, 0.001);
    [self transformFrom:_lastPinchPoint to:current scale:delta];
    _lastPinchPoint = current;
    _lastPinchScale = recognizer.scale;
  }
}

- (BOOL)gestureRecognizer:(UIGestureRecognizer*)gestureRecognizer
    shouldRecognizeSimultaneouslyWithGestureRecognizer:(UIGestureRecognizer*)other {
  (void)gestureRecognizer;
  (void)other;
  return YES;
}

- (void)togglePage:(UIButton*)sender {
  _activePage = _activePage == 1U ? 2U : 1U;
  _overlayLayer.userInteractionEnabled = _activePage == 1U;
  [sender setTitle:_activePage == 1U ? @"Page 2" : @"Page 1"
          forState:UIControlStateNormal];
  [_webView resignFirstResponder];
}

- (void)toggleFailure:(UIButton*)sender {
  _failureMode = !_failureMode;
  if (_failureMode) {
    std::string error;
    _registry->markFailed(1U, &error);
    [sender setTitle:@"Recover" forState:UIControlStateNormal];
  } else {
    std::string error;
    _registry->recover(1U, &error);
    _registry->markReady(1U, &error);
    [sender setTitle:@"Fail Web" forState:UIControlStateNormal];
  }
}

- (void)recreate:(UIButton*)sender {
  (void)sender;
  // Recreate is an explicit lifecycle test.  Advancing only the generation
  // leaves the old native objects alive and makes the button appear inert.
  // Tear down both platform surfaces, destroy their registry materialization,
  // then create fresh instances and register them against the same semantic
  // placeholders.
  [_webView resignFirstResponder];
  _webView.navigationDelegate = nil;
  [_webView removeFromSuperview];
  _webView = nil;
  [_videoView stopPlayback];
  [_videoView removeFromSuperview];
  _videoView = nil;

  std::string error;
  if (_registry != nullptr) {
    if (!_registry->unregisterSurface(1U, &error) ||
        !_registry->unregisterSurface(2U, &error) ||
        !_registry->registerSurface(WebPlaceholder(), &error) ||
        !_registry->registerSurface(VideoPlaceholder(), &error) ||
        !_registry->markReady(1U, &error) ||
        !_registry->markReady(2U, &error)) {
      NSLog(@"CANVAS_POC05_APPLE_FAILURE recreate: %s", error.c_str());
      return;
    }
    if (_failureMode && !_registry->markFailed(1U, &error)) {
      NSLog(@"CANVAS_POC05_APPLE_FAILURE recreate failure state: %s",
            error.c_str());
      return;
    }
  }

  WKWebViewConfiguration* configuration = [[WKWebViewConfiguration alloc] init];
  _webView = [[WKWebView alloc] initWithFrame:CGRectZero
                                  configuration:configuration];
  _webView.navigationDelegate = self;
  [_webView loadHTMLString:@"<!doctype html><html><meta name='viewport' content='width=device-width'><body contenteditable='true' style='margin:0;padding:16px;font:17px -apple-system;background:white;color:#172033'><b>Real WKWebView</b><p>Edit or scroll this text to validate focus and IME handoff.</p><p>Two-finger Canvas pan/zoom stays native.</p><p>Line 4</p><p>Line 5</p></body></html>" baseURL:nil];
  [_overlayLayer insertSubview:_webView atIndex:0];
  _videoView = [[Poc05VideoView alloc] initWithFrame:CGRectZero];
  [_overlayLayer insertSubview:_videoView atIndex:1];
  ++_targetGeneration;
  _statusLabel.text = @"POC-05 Apple · overlays recreated";
}

- (void)background:(NSNotification*)notification {
  (void)notification;
  _backgrounded = YES;
  [_webView resignFirstResponder];
}

- (void)foreground:(NSNotification*)notification {
  (void)notification;
  _backgrounded = NO;
  ++_targetGeneration;
}

- (void)runCorpus {
  const RuntimeScene oracle = SceneCompiler().CompileFull(*_document);
  const bool sceneEquivalent = oracle.Digest() == _scene->Digest();
  const RegistryDiagnostics& diagnostics = _registry->diagnostics();
  const double endingPss = ProcessFootprintMib();
  std::ostringstream result;
  result << "{\"schema_version\":1,\"platform\":\"apple\","
         << "\"shell\":\"native-universal-poc-adapter\","
         << "\"backend\":\"ganesh-metal\",\"nodes\":100000,"
         << "\"webview\":\"WKWebView\","
         << "\"video\":\"AVSampleBufferDisplayLayer\","
         << "\"scene_equivalent\":" << (sceneEquivalent ? "true" : "false")
         << ",\"placement_frames\":" << _placementFrames
         << ",\"max_placement_error_px\":" << _maxPlacementErrorPx
         << ",\"active_surfaces\":" << diagnostics.activeSurfaceCount
         << ",\"materialized_surfaces\":" << diagnostics.materializedSurfaceCount
         << ",\"process_warm_mib\":" << _warmPssMib
         << ",\"process_end_mib\":" << endingPss
         << ",\"runtime_c_abi_binary_conformance\":false,"
         << "\"react_native_fabric_conformance\":false}";
  NSURL* documents = [[NSFileManager defaultManager]
      URLForDirectory:NSDocumentDirectory inDomain:NSUserDomainMask
      appropriateForURL:nil create:YES error:nil];
  NSURL* output = [documents URLByAppendingPathComponent:@"poc05-apple-result.json"];
  [[NSString stringWithUTF8String:result.str().c_str()]
      writeToURL:output atomically:YES encoding:NSUTF8StringEncoding error:nil];
  NSLog(@"CANVAS_POC05_APPLE_RESULT %s", result.str().c_str());
  _statusLabel.text = sceneEquivalent ? @"POC-05 Apple ready · test gestures"
                                      : @"POC-05 Apple corpus failed";
}

- (void)dealloc {
  [_displayLink invalidate];
  [[NSNotificationCenter defaultCenter] removeObserver:self];
  _registry.reset();
  _context->releaseResourcesAndAbandonContext();
  _context.reset();
}

@end

@interface CanvasPoc05AppDelegate : UIResponder <UIApplicationDelegate>
@property(nonatomic, strong) UIWindow* window;
@end

@implementation CanvasPoc05AppDelegate
- (BOOL)application:(UIApplication*)application
    didFinishLaunchingWithOptions:(NSDictionary*)launchOptions {
  (void)application;
  (void)launchOptions;
  self.window = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
  UIViewController* controller = [[UIViewController alloc] init];
  controller.view = [[CanvasPoc05AppleView alloc]
      initWithFrame:self.window.bounds];
  self.window.rootViewController = controller;
  [self.window makeKeyAndVisible];
  return YES;
}
@end

int main(int argc, char* argv[]) {
  @autoreleasepool {
    return UIApplicationMain(argc, argv, nil,
                             NSStringFromClass(CanvasPoc05AppDelegate.class));
  }
}
