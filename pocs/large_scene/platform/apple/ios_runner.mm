#import <UIKit/UIKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <mach/mach.h>

#include "canvas/poc03/large_scene.h"
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

constexpr int kAcceptanceFrames = 600;

double Percentile(std::vector<double> values, double percentile) {
  std::sort(values.begin(), values.end());
  if (values.empty()) return 0.0;
  const size_t index = static_cast<size_t>(
      std::ceil(percentile * static_cast<double>(values.size())) - 1.0);
  return values[std::min(index, values.size() - 1U)];
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

std::string JsonEscape(const std::string& value) {
  std::string result;
  for (const char character : value) {
    if (character == '\\' || character == '"') result.push_back('\\');
    result.push_back(character);
  }
  return result;
}

class IpadMetalHarness {
 public:
  bool Initialize(CAMetalLayer* layer, uint32_t width, uint32_t height,
                  float dpr, std::string* error) {
    if (layer == nil || width == 0U || height == 0U || !std::isfinite(dpr) ||
        dpr <= 0.0F) {
      *error = "invalid iPadOS Metal surface";
      return false;
    }
    device_ = MTLCreateSystemDefaultDevice();
    if (device_ == nil) {
      *error = "Metal device is unavailable";
      return false;
    }
    queue_ = [device_ newCommandQueue];
    if (queue_ == nil) {
      *error = "Metal command queue creation failed";
      return false;
    }
    GrMtlBackendContext backend;
    backend.fDevice = sk_ret_cfp((__bridge GrMTLHandle)device_);
    backend.fQueue = sk_ret_cfp((__bridge GrMTLHandle)queue_);
    context_ = GrDirectContexts::MakeMetal(backend);
    if (!context_) {
      *error = "Skia Ganesh Metal context creation failed";
      return false;
    }
    layer.device = device_;
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    layer.framebufferOnly = NO;
    layer.opaque = YES;
    layer.drawableSize = CGSizeMake(width, height);
    layer_ = layer;
    width_ = width;
    height_ = height;
    dpr_ = dpr;

    try {
      document_ = std::make_unique<Document>(GenerateDocument(
          GeneratorConfig{100000U, 0x43414e5641533033ULL, 1000U, 32.0F}));
      scene_ = std::make_unique<RuntimeScene>(
          SceneCompiler().CompileFull(*document_));
      SceneCompiler compiler;
      for (uint32_t update = 0; update < 1000U; ++update) {
        const uint64_t id =
            1U + (static_cast<uint64_t>(update) * 7919U) % 100000U;
        NodeRecord changed = *document_->Find(id);
        changed.rgba ^= 0x00010101U;
        ++changed.content_revision;
        ChangeSet changes;
        CompileDiagnostics diagnostics;
        if (!document_->Apply({OperationKind::kUpdate, id, changed}, &changes,
                              error) ||
            !compiler.ApplyIncremental(*document_, changes, scene_.get(),
                                       &diagnostics, error)) {
          return false;
        }
      }
      oracle_ = std::make_unique<RuntimeScene>(compiler.CompileFull(*document_));
      if (oracle_->Digest() != scene_->Digest()) {
        *error = "incremental/full scene digest differs";
        return false;
      }
    } catch (const std::exception& exception) {
      *error = exception.what();
      return false;
    }
    return true;
  }

  bool RunFrame(int frame, double display_timestamp, double nominal_interval,
                std::string* error) {
    const auto callback_start = std::chrono::steady_clock::now();
    if (last_display_timestamp_ > 0.0) {
      const double interval_ms =
          (display_timestamp - last_display_timestamp_) * 1000.0;
      display_intervals_ms_.push_back(interval_ms);
      if (nominal_interval > 0.0 &&
          interval_ms > nominal_interval * 1000.0 * 1.5) {
        const double ratio = interval_ms / (nominal_interval * 1000.0);
        missed_presentations_ +=
            static_cast<uint64_t>(std::max(1.0, std::round(ratio) - 1.0));
      }
    }
    last_display_timestamp_ = display_timestamp;
    const float zoom = 0.75F + static_cast<float>(frame % 8) * 0.125F;
    const float pan_x = static_cast<float>(
        (static_cast<uint64_t>(frame) * 37U) % 28000U);
    const float pan_y = static_cast<float>(
        (static_cast<uint64_t>(frame) * 17U) % 2200U);
    const ViewState view = MakeView(pan_x, pan_y, zoom,
                                    static_cast<uint64_t>(frame) + 1U);
    const ViewQueryResult query = QueryView(*scene_, view, std::nullopt);
    maximum_candidates_ =
        std::max(maximum_candidates_, query.candidates.size());
    maximum_visible_ = std::max(maximum_visible_, query.visible.size());
    double render_ms = 0.0;
    if (!Render(*scene_, view, query, false, nullptr, &render_ms, error)) {
      return false;
    }
    render_ms_.push_back(render_ms);
    maximum_footprint_mib_ =
        std::max(maximum_footprint_mib_, ProcessFootprintMib());
    callback_ms_.push_back(std::chrono::duration<double, std::milli>(
                               std::chrono::steady_clock::now() - callback_start)
                               .count());
    final_view_ = view;
    return true;
  }

  bool Finish(double refresh_rate, std::string* result, std::string* error) {
    const ViewQueryResult query = QueryView(*scene_, final_view_, std::nullopt);
    std::vector<uint8_t> incremental_rgba;
    std::vector<uint8_t> oracle_rgba;
    double ignored_ms = 0.0;
    if (!Render(*scene_, final_view_, query, true, &incremental_rgba,
                &ignored_ms, error)) {
      return false;
    }
    const ViewQueryResult oracle_query =
        QueryView(*oracle_, final_view_, std::nullopt);
    if (!Render(*oracle_, final_view_, oracle_query, true, &oracle_rgba,
                &ignored_ms, error)) {
      return false;
    }
    const bool visual_equivalent = incremental_rgba == oracle_rgba;
    pan_x_ = final_view_.world_viewport.left;
    pan_y_ = final_view_.world_viewport.top;
    zoom_ = final_view_.zoom;
    maximum_footprint_mib_ =
        std::max(maximum_footprint_mib_, ProcessFootprintMib());
    std::ostringstream json;
    json << std::fixed << std::setprecision(3)
         << "{\"schema_version\":1,\"platform\":\"ipados\","
         << "\"backend\":\"ganesh-metal\",\"nodes\":100000,"
         << "\"device\":\"" << JsonEscape([[device_ name] UTF8String])
         << "\",\"document_digest\":\"" << document_->Digest() << "\","
         << "\"scene_digest\":\"" << scene_->Digest() << "\","
         << "\"full_incremental_equivalent\":true,"
         << "\"visual_equivalent\":"
         << (visual_equivalent ? "true" : "false") << ','
         << "\"frames\":" << render_ms_.size() << ','
         << "\"frame_p50_ms\":" << Percentile(callback_ms_, 0.50) << ','
         << "\"frame_p95_ms\":" << Percentile(callback_ms_, 0.95) << ','
         << "\"frame_p99_ms\":" << Percentile(callback_ms_, 0.99) << ','
         << "\"frame_max_ms\":"
         << *std::max_element(callback_ms_.begin(), callback_ms_.end()) << ','
         << "\"render_submit_p50_ms\":" << Percentile(render_ms_, 0.50)
         << ','
         << "\"render_submit_p95_ms\":" << Percentile(render_ms_, 0.95)
         << ','
         << "\"render_submit_p99_ms\":" << Percentile(render_ms_, 0.99)
         << ','
         << "\"render_submit_max_ms\":"
         << *std::max_element(render_ms_.begin(), render_ms_.end()) << ','
         << "\"display_interval_p50_ms\":"
         << Percentile(display_intervals_ms_, 0.50) << ','
         << "\"display_interval_p95_ms\":"
         << Percentile(display_intervals_ms_, 0.95) << ','
         << "\"display_interval_p99_ms\":"
         << Percentile(display_intervals_ms_, 0.99) << ','
         << "\"display_interval_max_ms\":"
         << (display_intervals_ms_.empty()
                 ? 0.0
                 : *std::max_element(display_intervals_ms_.begin(),
                                     display_intervals_ms_.end()))
         << ",\"refresh_rate_hz\":" << refresh_rate << ','
         << "\"missed_presentations\":" << missed_presentations_ << ','
         << "\"surface_width_px\":" << width_ << ','
         << "\"surface_height_px\":" << height_ << ','
         << "\"dpr\":" << dpr_ << ','
         << "\"maximum_candidates\":" << maximum_candidates_ << ','
         << "\"maximum_visible\":" << maximum_visible_ << ','
         << "\"process_peak_mib\":" << maximum_footprint_mib_ << ','
         << "\"input_events\":" << input_events_ << '}';
    *result = json.str();
    return visual_equivalent;
  }

  bool Transform(float previous_focus_x_px, float previous_focus_y_px,
                 float current_focus_x_px, float current_focus_y_px,
                 float scale, std::string* error) {
    ViewportTransform transform{pan_x_, pan_y_, zoom_};
    if (!ApplyViewportGesture(
            ViewportGesture{previous_focus_x_px, previous_focus_y_px,
                            current_focus_x_px, current_focus_y_px, scale},
            dpr_, 0.25F, 4.0F,
            Bounds{0.0F, 0.0F, 30000.0F, 3000.0F}, &transform, error)) {
      return false;
    }
    pan_x_ = transform.pan_x;
    pan_y_ = transform.pan_y;
    zoom_ = transform.zoom;
    ++input_events_;
    const ViewState view = MakeView(pan_x_, pan_y_, zoom_, ++view_revision_);
    const ViewQueryResult query = QueryView(*scene_, view, std::nullopt);
    double ignored_ms = 0.0;
    return Render(*scene_, view, query, false, nullptr, &ignored_ms, error);
  }

 private:
  ViewState MakeView(float pan_x, float pan_y, float zoom,
                     uint64_t revision) const {
    return ViewState{
        1U, revision, 1U,
        Bounds{pan_x, pan_y,
               pan_x + static_cast<float>(width_) / (zoom * dpr_),
               pan_y + static_cast<float>(height_) / (zoom * dpr_)},
        zoom, dpr_, width_, height_};
  }

  bool Render(const RuntimeScene& scene, const ViewState& view,
              const ViewQueryResult& query, bool readback,
              std::vector<uint8_t>* rgba, double* elapsed_ms,
              std::string* error) {
    @autoreleasepool {
      const auto start = std::chrono::steady_clock::now();
      id<CAMetalDrawable> drawable = [layer_ nextDrawable];
      if (drawable == nil) {
        *error = "CAMetalLayer did not provide a drawable";
        return false;
      }
      GrMtlTextureInfo texture_info;
      texture_info.fTexture =
          sk_ret_cfp((__bridge GrMTLHandle)drawable.texture);
      const GrBackendRenderTarget target = GrBackendRenderTargets::MakeMtl(
          static_cast<int>(width_), static_cast<int>(height_), texture_info);
      sk_sp<SkSurface> surface = SkSurfaces::WrapBackendRenderTarget(
          context_.get(), target, kTopLeft_GrSurfaceOrigin,
          kBGRA_8888_SkColorType, SkColorSpace::MakeSRGB(), nullptr);
      if (!surface) {
        *error = "Skia could not wrap the iPadOS Metal drawable";
        return false;
      }
      DrawLargeScene(*surface->getCanvas(), scene, view,
                     BuildFrame(scene, query, {}));
      context_->flushAndSubmit(surface.get(),
                               readback ? GrSyncCpu::kYes : GrSyncCpu::kNo);
      if (readback && !ReadRgba(*surface, width_, height_, rgba)) {
        *error = "iPadOS RGBA readback failed";
        return false;
      }
      id<MTLCommandBuffer> present = [queue_ commandBuffer];
      [present presentDrawable:drawable];
      [present commit];
      if (readback) [present waitUntilCompleted];
      *elapsed_ms = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - start)
                        .count();
      return true;
    }
  }

  __strong CAMetalLayer* layer_ = nil;
  __strong id<MTLDevice> device_ = nil;
  __strong id<MTLCommandQueue> queue_ = nil;
  sk_sp<GrDirectContext> context_;
  std::unique_ptr<Document> document_;
  std::unique_ptr<RuntimeScene> scene_;
  std::unique_ptr<RuntimeScene> oracle_;
  uint32_t width_ = 0;
  uint32_t height_ = 0;
  float dpr_ = 1.0F;
  float pan_x_ = 0.0F;
  float pan_y_ = 0.0F;
  float zoom_ = 1.0F;
  uint64_t view_revision_ = 1U;
  uint64_t input_events_ = 0U;
  size_t maximum_candidates_ = 0U;
  size_t maximum_visible_ = 0U;
  uint64_t missed_presentations_ = 0U;
  double last_display_timestamp_ = 0.0;
  double maximum_footprint_mib_ = 0.0;
  ViewState final_view_;
  std::vector<double> render_ms_;
  std::vector<double> callback_ms_;
  std::vector<double> display_intervals_ms_;
};

}  // namespace

@interface CanvasPoc03View : UIView
@end

@implementation CanvasPoc03View {
  std::unique_ptr<IpadMetalHarness> _harness;
  CADisplayLink* _displayLink;
  NSInteger _frame;
  BOOL _started;
  BOOL _acceptanceComplete;
  CGFloat _lastPinchScale;
  CGPoint _lastPinchLocation;
}

+ (Class)layerClass { return CAMetalLayer.class; }

- (void)didMoveToWindow {
  [super didMoveToWindow];
  if (_started || self.window == nil) return;
  _started = YES;
  self.multipleTouchEnabled = YES;
  UIPanGestureRecognizer* pan =
      [[UIPanGestureRecognizer alloc] initWithTarget:self action:@selector(pan:)];
  pan.maximumNumberOfTouches = 1;
  UIPinchGestureRecognizer* pinch = [[UIPinchGestureRecognizer alloc]
      initWithTarget:self action:@selector(pinch:)];
  [self addGestureRecognizer:pan];
  [self addGestureRecognizer:pinch];

  const CGFloat dpr = self.window.screen.scale;
  const uint32_t width =
      static_cast<uint32_t>(std::round(self.bounds.size.width * dpr));
  const uint32_t height =
      static_cast<uint32_t>(std::round(self.bounds.size.height * dpr));
  _harness = std::make_unique<IpadMetalHarness>();
  std::string error;
  if (!_harness->Initialize((CAMetalLayer*)self.layer, width, height,
                            static_cast<float>(dpr), &error)) {
    NSLog(@"CANVAS_POC03_FAILURE %s", error.c_str());
    return;
  }
  _displayLink = [CADisplayLink displayLinkWithTarget:self
                                             selector:@selector(frame:)];
  _displayLink.preferredFramesPerSecond = self.window.screen.maximumFramesPerSecond;
  [_displayLink addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
}

- (void)frame:(CADisplayLink*)link {
  std::string error;
  const double nominalInterval =
      link.targetTimestamp > link.timestamp
          ? link.targetTimestamp - link.timestamp
          : 1.0 / std::max<NSInteger>(
                      1, self.window.screen.maximumFramesPerSecond);
  if (!_harness->RunFrame(static_cast<int>(_frame), link.timestamp,
                          nominalInterval, &error)) {
    [_displayLink invalidate];
    NSLog(@"CANVAS_POC03_FAILURE %s", error.c_str());
    return;
  }
  ++_frame;
  if (_frame < kAcceptanceFrames) return;
  [_displayLink invalidate];
  _displayLink = nil;
  std::string result;
  const double refreshRate =
      nominalInterval > 0.0 ? 1.0 / nominalInterval
                            : self.window.screen.maximumFramesPerSecond;
  if (!_harness->Finish(refreshRate, &result, &error)) {
    NSLog(@"CANVAS_POC03_FAILURE %s", error.c_str());
    return;
  }
  NSURL* documents = [[NSFileManager defaultManager]
      URLForDirectory:NSDocumentDirectory
             inDomain:NSUserDomainMask
    appropriateForURL:nil
               create:YES
                error:nil];
  NSURL* output = [documents URLByAppendingPathComponent:@"poc03-result.json"];
  [[NSString stringWithUTF8String:result.c_str()]
      writeToURL:output
      atomically:YES
      encoding:NSUTF8StringEncoding
      error:nil];
  NSLog(@"CANVAS_POC03_RESULT %s", result.c_str());
  _acceptanceComplete = YES;
}

- (void)pan:(UIPanGestureRecognizer*)recognizer {
  if (!_acceptanceComplete) return;
  const CGPoint translation = [recognizer translationInView:self];
  const CGPoint current = [recognizer locationInView:self];
  const CGPoint previous = CGPointMake(current.x - translation.x,
                                       current.y - translation.y);
  [recognizer setTranslation:CGPointZero inView:self];
  const CGFloat dpr = self.window.screen.scale;
  std::string error;
  if (!_harness->Transform(static_cast<float>(previous.x * dpr),
                           static_cast<float>(previous.y * dpr),
                           static_cast<float>(current.x * dpr),
                           static_cast<float>(current.y * dpr), 1.0F,
                           &error)) {
    NSLog(@"CANVAS_POC03_INTERACTIVE_FAILURE %s", error.c_str());
  }
}

- (void)pinch:(UIPinchGestureRecognizer*)recognizer {
  if (!_acceptanceComplete) return;
  const CGPoint current = [recognizer locationInView:self];
  if (recognizer.state == UIGestureRecognizerStateBegan) {
    _lastPinchScale = recognizer.scale;
    _lastPinchLocation = current;
    return;
  }
  if (recognizer.state != UIGestureRecognizerStateChanged ||
      recognizer.numberOfTouches < 2) {
    return;
  }
  const CGFloat delta = recognizer.scale / std::max(_lastPinchScale, 0.001);
  _lastPinchScale = recognizer.scale;
  const CGFloat dpr = self.window.screen.scale;
  std::string error;
  if (!_harness->Transform(static_cast<float>(_lastPinchLocation.x * dpr),
                           static_cast<float>(_lastPinchLocation.y * dpr),
                           static_cast<float>(current.x * dpr),
                           static_cast<float>(current.y * dpr),
                           static_cast<float>(delta), &error)) {
    NSLog(@"CANVAS_POC03_INTERACTIVE_FAILURE %s", error.c_str());
  }
  _lastPinchLocation = current;
}

@end

@interface CanvasPoc03AppDelegate : UIResponder <UIApplicationDelegate>
@property(nonatomic, strong) UIWindow* window;
@end

@implementation CanvasPoc03AppDelegate
- (BOOL)application:(UIApplication*)application
    didFinishLaunchingWithOptions:(NSDictionary*)launchOptions {
  (void)application;
  (void)launchOptions;
  self.window = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
  UIViewController* controller = [[UIViewController alloc] init];
  controller.view = [[CanvasPoc03View alloc] initWithFrame:self.window.bounds];
  self.window.rootViewController = controller;
  [self.window makeKeyAndVisible];
  return YES;
}
@end

int main(int argc, char* argv[]) {
  @autoreleasepool {
    return UIApplicationMain(argc, argv, nil,
                             NSStringFromClass(CanvasPoc03AppDelegate.class));
  }
}
