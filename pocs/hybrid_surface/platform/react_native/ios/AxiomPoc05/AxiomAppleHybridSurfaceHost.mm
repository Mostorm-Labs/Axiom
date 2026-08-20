#import "AxiomAppleHybridSurfaceHost.h"

#import <AVFoundation/AVFoundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <WebKit/WebKit.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

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
std::atomic<double> g_js_stall_deadline{0.0};

class AppleProjector final : public RuntimeViewProjector {
 public:
  bool worldToViewLogical(CanvasViewHandle view, CanvasPointF world,
                          CanvasPointF* logical,
                          std::string* error) const override {
    if (view != kViewHandle || logical == nullptr || error == nullptr ||
        !std::isfinite(world.x) || !std::isfinite(world.y)) {
      if (error != nullptr) *error = "invalid Apple Fabric projection request";
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
      if (error != nullptr) *error = "duplicate Apple Fabric overlay";
      return false;
    }
    kinds[id] = kind;
    commands[id] = PlacementCommand{};
    return true;
  }

  bool apply(const PlacementCommand& command, std::string* error) override {
    if (!commands.contains(command.id)) {
      if (error != nullptr) *error = "Apple Fabric overlay is not materialized";
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
      if (error != nullptr) *error = "Apple Fabric overlay is not focusable";
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

}  // namespace

@interface AxiomPoc05FabricVideoView : UIView
- (void)stopPlayback;
@end

@implementation AxiomPoc05FabricVideoView {
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
      (NSString*)kCVPixelBufferWidthKey : @(320),
      (NSString*)kCVPixelBufferHeightKey : @(180),
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
  if (_pixelBuffer == nullptr) return;
  if (_displayLayer.status == AVQueuedSampleBufferRenderingStatusFailed) {
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
  if (buffer != nullptr) {
    [_displayLayer enqueueSampleBuffer:buffer];
    CFRelease(buffer);
  }
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

@implementation AxiomAppleHybridSurfaceHost {
  UIView* _canvasView;
  UIView* _overlayLayer;
  WKWebView* _webView;
  AxiomPoc05FabricVideoView* _videoView;
  UILabel* _failureView;
  UILabel* _stallLabel;
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
  NSInteger _lifecycleGeneration;
  NSInteger _activePage;
  BOOL _webVisible;
  BOOL _failureMode;
  BOOL _backgrounded;
  std::uint64_t _jsStallFrames;
  CGPoint _lastPanPoint;
  CGPoint _lastPinchPoint;
  CGFloat _lastPinchScale;
}

- (instancetype)initWithFrame:(CGRect)frame {
  if ((self = [super initWithFrame:frame])) {
    self.backgroundColor = [UIColor colorWithRed:244.0 / 255.0
                                           green:245.0 / 255.0
                                            blue:247.0 / 255.0 alpha:1.0];
    self.multipleTouchEnabled = YES;
    _frameRevision = 1U;
    _viewportRevision = 1U;
    _targetGeneration = 1U;
    _lifecycleGeneration = 1;
    _activePage = 1;
    _webVisible = YES;

    _canvasView = [[UIView alloc] initWithFrame:self.bounds];
    _canvasView.autoresizingMask = UIViewAutoresizingFlexibleWidth |
                                   UIViewAutoresizingFlexibleHeight;
    _metalLayer = [CAMetalLayer layer];
    [_canvasView.layer addSublayer:_metalLayer];
    [self addSubview:_canvasView];

    _overlayLayer = [[UIView alloc] initWithFrame:self.bounds];
    _overlayLayer.autoresizingMask = UIViewAutoresizingFlexibleWidth |
                                    UIViewAutoresizingFlexibleHeight;
    [self addSubview:_overlayLayer];
    [self installExternalViews];

    _stallLabel = [[UILabel alloc] initWithFrame:CGRectZero];
    _stallLabel.text = @"Native placement is Fabric-independent";
    _stallLabel.textColor = UIColor.whiteColor;
    _stallLabel.font = [UIFont systemFontOfSize:11.0F weight:UIFontWeightMedium];
    _stallLabel.backgroundColor = [UIColor colorWithRed:0.10 green:0.41
                                                    blue:0.25 alpha:0.92];
    _stallLabel.layer.cornerRadius = 4.0F;
    _stallLabel.clipsToBounds = YES;
    _stallLabel.hidden = YES;
    [self addSubview:_stallLabel];

    UIPanGestureRecognizer* pan = [[UIPanGestureRecognizer alloc]
        initWithTarget:self action:@selector(pan:)];
    pan.maximumNumberOfTouches = 1;
    [_canvasView addGestureRecognizer:pan];
    UIPinchGestureRecognizer* pinch = [[UIPinchGestureRecognizer alloc]
        initWithTarget:self action:@selector(pinch:)];
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

- (void)installExternalViews {
  WKWebViewConfiguration* configuration = [[WKWebViewConfiguration alloc] init];
  _webView = [[WKWebView alloc] initWithFrame:CGRectZero configuration:configuration];
  [_webView loadHTMLString:@"<!doctype html><html><meta name='viewport' content='width=device-width'><body contenteditable='true' style='margin:0;padding:16px;font:17px -apple-system;background:white;color:#172033'><b>Real Fabric WKWebView</b><p>Edit or scroll this text to validate focus and IME handoff.</p><p>Canvas placement stays native while React is blocked.</p><p>Line 4</p><p>Line 5</p></body></html>"
                 baseURL:nil];
  _videoView = [[AxiomPoc05FabricVideoView alloc] initWithFrame:CGRectZero];
  _failureView = [[UILabel alloc] initWithFrame:CGRectZero];
  _failureView.text = @"Web content unavailable\nToggle Recover in Fabric UI";
  _failureView.numberOfLines = 2;
  _failureView.textAlignment = NSTextAlignmentCenter;
  _failureView.textColor = UIColor.whiteColor;
  _failureView.backgroundColor = [UIColor colorWithRed:0.58 green:0.14
                                                   blue:0.18 alpha:1.0];
  _failureView.hidden = YES;
  [_overlayLayer addSubview:_webView];
  [_overlayLayer addSubview:_videoView];
  [_overlayLayer addSubview:_failureView];
}

- (void)didMoveToWindow {
  [super didMoveToWindow];
  if (self.window != nil && !_context) [self initializeRuntime];
}

- (void)layoutSubviews {
  [super layoutSubviews];
  _metalLayer.frame = _canvasView.bounds;
  const CGFloat scale = self.window.screen.scale ?: UIScreen.mainScreen.scale;
  _metalLayer.contentsScale = scale;
  _metalLayer.drawableSize = CGSizeMake(_canvasView.bounds.size.width * scale,
                                        _canvasView.bounds.size.height * scale);
  _stallLabel.frame = CGRectMake(12.0F, self.bounds.size.height - 34.0F,
                                 254.0F, 22.0F);
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
  if (!_context) {
    NSLog(@"CANVAS_POC05_APPLE_FABRIC_FAILURE could not create Ganesh Metal");
    return;
  }
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
    NSLog(@"CANVAS_POC05_APPLE_FABRIC_FAILURE %s", error.c_str());
    return;
  }
  _displayLink = [CADisplayLink displayLinkWithTarget:self
                                             selector:@selector(frame:)];
  _displayLink.preferredFrameRateRange = CAFrameRateRangeMake(60, 120, 60);
  [_displayLink addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
}

- (ViewState)currentView {
  const CGFloat dpr = self.window.screen.scale ?: UIScreen.mainScreen.scale;
  const std::uint32_t width = static_cast<std::uint32_t>(
      std::round(_canvasView.bounds.size.width * dpr));
  const std::uint32_t height = static_cast<std::uint32_t>(
      std::round(_canvasView.bounds.size.height * dpr));
  return ViewState{kViewHandle, _viewportRevision, 1U,
                   Bounds{_projector->pan_x, _projector->pan_y,
                          _projector->pan_x + static_cast<float>(width) /
                              (_projector->zoom * static_cast<float>(dpr)),
                          _projector->pan_y + static_cast<float>(height) /
                              (_projector->zoom * static_cast<float>(dpr))},
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
        static_cast<int>(view.pixel_width), static_cast<int>(view.pixel_height),
        textureInfo);
    sk_sp<SkSurface> surface = SkSurfaces::WrapBackendRenderTarget(
        _context.get(), target, kTopLeft_GrSurfaceOrigin, kBGRA_8888_SkColorType,
        SkColorSpace::MakeSRGB(), nullptr);
    if (!surface) {
      *error = "Skia could not wrap the Fabric Metal drawable";
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
  if (![self render:&error] || ![self applyPlacements:&error]) {
    NSLog(@"CANVAS_POC05_APPLE_FABRIC_FAILURE %s", error.c_str());
  }
  if ([AxiomAppleHybridSurfaceHost isJsStallActive]) {
    ++_jsStallFrames;
    _stallLabel.text = [NSString stringWithFormat:
        @"Native placement during JS stall: %llu frames",
        static_cast<unsigned long long>(_jsStallFrames)];
    _stallLabel.hidden = NO;
  }
}

- (BOOL)applyPlacements:(std::string*)error {
  if (!_registry || !_projector || !_backend) return YES;
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
  _registry->setActivePage(static_cast<std::uint64_t>(_activePage));
  _registry->setBackgrounded(_backgrounded);
  if (!_registry->setHidden(1U, !_webVisible, error)) return NO;
  const RuntimeViewFrame frame{kViewHandle, camera, surface, ++_frameRevision};
  if (!_registry->applyFrame(frame, error)) return NO;
  [self apply:_backend->find(1U) to:_webView fallback:_failureView];
  [self apply:_backend->find(2U) to:_videoView fallback:nil];
  return YES;
}

- (void)apply:(const PlacementCommand*)command to:(UIView*)content
      fallback:(UIView*)fallback {
  if (command == nullptr) {
    content.hidden = YES;
    if (fallback != nil) fallback.hidden = YES;
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
      command->relativeDeviceClip.x / dpr, command->relativeDeviceClip.y / dpr,
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
}

- (void)transformFrom:(CGPoint)previous to:(CGPoint)current scale:(CGFloat)scale {
  if (!_projector) return;
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
    NSLog(@"CANVAS_POC05_APPLE_FABRIC_GESTURE_FAILURE %s", error.c_str());
    return;
  }
  _projector->pan_x = transform.pan_x;
  _projector->pan_y = transform.pan_y;
  _projector->zoom = transform.zoom;
  ++_viewportRevision;
}

- (CGPoint)canvasPointFromSelf:(CGPoint)point {
  return [self convertPoint:point toView:_canvasView];
}

- (void)pan:(UIPanGestureRecognizer*)recognizer {
  const CGPoint current = [self canvasPointFromSelf:[recognizer locationInView:self]];
  if (recognizer.state == UIGestureRecognizerStateBegan) {
    _lastPanPoint = current;
  } else if (recognizer.state == UIGestureRecognizerStateChanged) {
    [self transformFrom:_lastPanPoint to:current scale:1.0F];
    _lastPanPoint = current;
  }
}

- (void)pinch:(UIPinchGestureRecognizer*)recognizer {
  const CGPoint current = [self canvasPointFromSelf:[recognizer locationInView:self]];
  if (recognizer.state == UIGestureRecognizerStateBegan) {
    _lastPinchPoint = current;
    _lastPinchScale = recognizer.scale;
  } else if (recognizer.state == UIGestureRecognizerStateChanged) {
    const CGFloat delta = recognizer.scale /
        std::max(_lastPinchScale, static_cast<CGFloat>(0.001));
    [self transformFrom:_lastPinchPoint to:current scale:delta];
    _lastPinchPoint = current;
    _lastPinchScale = recognizer.scale;
  }
}

- (void)recreateExternalViews {
  [_webView resignFirstResponder];
  [_webView removeFromSuperview];
  [_videoView stopPlayback];
  [_videoView removeFromSuperview];
  [_failureView removeFromSuperview];
  std::string error;
  if (_registry &&
      (!_registry->unregisterSurface(1U, &error) ||
       !_registry->unregisterSurface(2U, &error) ||
       !_registry->registerSurface(WebPlaceholder(), &error) ||
       !_registry->registerSurface(VideoPlaceholder(), &error) ||
       !_registry->markReady(1U, &error) || !_registry->markReady(2U, &error))) {
    NSLog(@"CANVAS_POC05_APPLE_FABRIC_FAILURE recreate: %s", error.c_str());
    return;
  }
  [self installExternalViews];
  ++_targetGeneration;
}

- (void)setWebVisible:(BOOL)visible { _webVisible = visible; }

+ (void)noteJsStallStarted:(NSTimeInterval)milliseconds {
  const NSTimeInterval duration = MAX(milliseconds, 1.0) / 1000.0;
  g_js_stall_deadline.store(CACurrentMediaTime() + duration,
                            std::memory_order_release);
}

+ (BOOL)isJsStallActive {
  return CACurrentMediaTime() <=
      g_js_stall_deadline.load(std::memory_order_acquire);
}

- (void)setFailureMode:(BOOL)failed {
  if (_failureMode == failed) return;
  _failureMode = failed;
  if (!_registry) return;
  std::string error;
  if (failed) {
    if (!_registry->markFailed(1U, &error)) {
      NSLog(@"CANVAS_POC05_APPLE_FABRIC_FAILURE fail: %s", error.c_str());
    }
  } else if (!_registry->recover(1U, &error) || !_registry->markReady(1U, &error)) {
    NSLog(@"CANVAS_POC05_APPLE_FABRIC_FAILURE recover: %s", error.c_str());
  }
}

- (void)setActivePage:(NSInteger)page {
  const NSInteger nextPage = std::max<NSInteger>(1, page);
  if (_activePage == nextPage) return;
  _activePage = nextPage;
  _overlayLayer.userInteractionEnabled = _activePage == 1;
  [_webView resignFirstResponder];
  ++_targetGeneration;
}

- (void)setLifecycleGeneration:(NSInteger)generation {
  if (generation <= _lifecycleGeneration) return;
  _lifecycleGeneration = generation;
  [self recreateExternalViews];
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

- (void)dealloc {
  [_displayLink invalidate];
  [_videoView stopPlayback];
  [[NSNotificationCenter defaultCenter] removeObserver:self];
  _registry.reset();
  if (_context) _context->releaseResourcesAndAbandonContext();
  _context.reset();
}

@end
