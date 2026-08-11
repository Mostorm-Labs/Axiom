#include "platform/macos/metal_host.h"

#include "canvas/document/document.h"
#include "canvas/render/skia_renderer.h"
#include "platform/frame_invalidation.h"

#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkSurface.h"
#include "include/gpu/ganesh/GrBackendSurface.h"
#include "include/gpu/ganesh/GrDirectContext.h"
#include "include/gpu/ganesh/SkSurfaceGanesh.h"
#include "include/gpu/ganesh/mtl/GrMtlBackendContext.h"
#include "include/gpu/ganesh/mtl/GrMtlBackendSurface.h"
#include "include/gpu/ganesh/mtl/GrMtlDirectContext.h"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <utility>

namespace canvas::macos {

namespace {

bool isMainThread() { return [NSThread isMainThread]; }

void requireMainThread() {
  if (!isMainThread()) std::terminate();
}

}  // namespace

// The definition deliberately remains in Objective-C++ so public C++ headers
// do not expose AppKit, Metal, or Ganesh types. A single instance is retained
// by both MetalHost attachments in CanvasCompositionView.
class MetalRenderResources {
 public:
  __strong id<MTLDevice> device = nil;
  __strong id<MTLCommandQueue> commandQueue = nil;
  sk_sp<GrDirectContext> skiaContext;
  render::SkiaRenderer renderer;

  ~MetalRenderResources() { requireMainThread(); }

  bool isReady() const noexcept {
    return device != nil && commandQueue != nil && skiaContext != nullptr;
  }
};

std::shared_ptr<MetalRenderResources> createMetalRenderResources() {
  requireMainThread();

  id<MTLDevice> device = MTLCreateSystemDefaultDevice();
  if (device == nil) return {};
  id<MTLCommandQueue> commandQueue = [device newCommandQueue];
  if (commandQueue == nil) return {};

  GrMtlBackendContext backendContext{};
  backendContext.fDevice.retain((__bridge GrMTLHandle)device);
  backendContext.fQueue.retain((__bridge GrMTLHandle)commandQueue);
  sk_sp<GrDirectContext> skiaContext =
      GrDirectContexts::MakeMetal(backendContext);
  if (skiaContext == nullptr) return {};

  auto resources = std::make_shared<MetalRenderResources>();
  resources->device = device;
  resources->commandQueue = commandQueue;
  resources->skiaContext = std::move(skiaContext);
  return resources;
}

class MetalHost::Impl {
 public:
  struct Attachment {
    std::shared_ptr<MetalRenderResources> resources;
    __strong CAMetalLayer* metalLayer = nil;
    MetalSurfaceRole role = MetalSurfaceRole::Base;
    double backingScale = 1.0;
  };

  explicit Impl(MetalSurfaceRole surfaceRole,
                std::shared_ptr<MetalRenderResources> resources)
      : sharedResources(std::move(resources)), role(surfaceRole) {}

  platform::FrameInvalidation invalidation;
  __weak NSView* view = nil;
  std::shared_ptr<Attachment> attachment;
  std::shared_ptr<MetalRenderResources> sharedResources;
  std::shared_ptr<const document::Document> document;
  MetalSurfaceRole role = MetalSurfaceRole::Base;
  std::uint64_t attachmentGeneration = 0;
  std::uint64_t nativeDisplayRequestCount = 0;
  std::uint64_t committedFrameCount = 0;
  bool attachmentTransition = false;

  void scheduleDisplay() {
    ++nativeDisplayRequestCount;
    if (view != nil) [view setNeedsDisplay:YES];
    if (attachment && attachment->metalLayer != nil) {
      [attachment->metalLayer setNeedsDisplay];
    }
  }

  bool hasAttachment(std::uint64_t generation, NSView* expectedView,
                     const std::shared_ptr<Attachment>& expected) const {
    return !attachmentTransition && attachmentGeneration == generation &&
           view == expectedView && attachment == expected && expectedView != nil &&
           expected && expected->resources && expected->resources->isReady() &&
           expected->metalLayer != nil;
  }
};

MetalHost::MetalHost() : MetalHost(MetalSurfaceRole::Base, {}) {}

MetalHost::MetalHost(
    MetalSurfaceRole role,
    std::shared_ptr<MetalRenderResources> sharedResources)
    : impl_(std::make_unique<Impl>(role, std::move(sharedResources))) {
  requireMainThread();
}

MetalHost::~MetalHost() {
  // The unique_ptr releases ARC-owned objects immediately after this body, so
  // checking only detachFromView() is insufficient to make teardown safe.
  requireMainThread();
  detachFromView();
}

bool MetalHost::attachToView(void* nativeView) {
  requireMainThread();
  if (nativeView == nullptr || impl_->attachmentTransition ||
      impl_->view != nil || impl_->attachment) {
    return false;
  }

  NSView* view = (__bridge NSView*)nativeView;
  if (![view isKindOfClass:[NSView class]]) return false;

  if (!impl_->sharedResources) {
    impl_->sharedResources = createMetalRenderResources();
  }
  if (!impl_->sharedResources || !impl_->sharedResources->isReady()) {
    return false;
  }

  const SkiaSurfaceFramePlan plan = skiaSurfaceFramePlan(impl_->role);
  CAMetalLayer* metalLayer = [CAMetalLayer layer];
  metalLayer.device = impl_->sharedResources->device;
  metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
  // Ganesh may use transfer operations while wrapping this drawable. Skia's
  // own Metal window context therefore requires a non-framebuffer-only layer.
  metalLayer.framebufferOnly = NO;
  metalLayer.opaque = plan.opaque ? YES : NO;
  metalLayer.presentsWithTransaction = NO;

  auto attachment = std::make_shared<Impl::Attachment>();
  attachment->resources = impl_->sharedResources;
  attachment->metalLayer = metalLayer;
  attachment->role = impl_->role;

  // Keep the transition guard active while AppKit installs the layer. A
  // synchronous layer callback must not observe a half-published attachment.
  impl_->attachmentTransition = true;
  [view setWantsLayer:YES];
  view.layer = metalLayer;
  impl_->view = view;
  impl_->attachment = std::move(attachment);
  ++impl_->attachmentGeneration;
  impl_->invalidation.reset();
  metalLayer.delegate = static_cast<id<CALayerDelegate>>(view);
  impl_->attachmentTransition = false;

  // Attaching is an explicit lifecycle event. Request one frame even when a
  // document was set while detached; no display timer or continuous loop is
  // used.
  invalidate();
  return true;
}

void MetalHost::detachFromView() {
  requireMainThread();
  if (!impl_ || impl_->attachmentTransition) return;
  if (impl_->view == nil && !impl_->attachment) return;

  impl_->attachmentTransition = true;
  __strong NSView* view = impl_->view;
  const auto attachment = impl_->attachment;
  ++impl_->attachmentGeneration;

  // Publish the detached state before touching AppKit. setLayer: can invoke a
  // delegate synchronously, and such a callback must fail the hostChanged()
  // check rather than render through an attachment being torn down.
  impl_->view = nil;
  impl_->attachment.reset();
  impl_->invalidation.reset();
  if (attachment && attachment->metalLayer != nil) {
    attachment->metalLayer.delegate = nil;
  }
  if (view != nil && attachment && view.layer == attachment->metalLayer) {
    view.layer = nil;
  }
  impl_->attachmentTransition = false;
}

void MetalHost::resize(double widthInPoints, double heightInPoints,
                       double backingScale) {
  requireMainThread();
  const auto attachment = impl_->attachment;
  if (!attachment) return;
  const std::uint64_t generation = impl_->attachmentGeneration;
  __strong NSView* view = impl_->view;
  const auto hostChanged = [&] {
    return !impl_->hasAttachment(generation, view, attachment);
  };

  const double scale = sanitizedBackingScale(backingScale);
  attachment->backingScale = scale;
  attachment->metalLayer.contentsScale = static_cast<CGFloat>(scale);
  if (hostChanged()) return;
  const DrawablePixelSize pixels =
      drawablePixelSize(widthInPoints, heightInPoints, scale);
  attachment->metalLayer.drawableSize = CGSizeMake(pixels.width, pixels.height);
  if (hostChanged()) return;
  invalidate();
}

void MetalHost::setDocument(
    std::shared_ptr<const document::Document> document) {
  requireMainThread();
  impl_->document = std::move(document);
  invalidate();
}

void MetalHost::invalidate() {
  requireMainThread();
  const bool shouldSchedule = impl_->invalidation.requestFrame();
  if (shouldSchedule && impl_->view != nil && impl_->attachment &&
      !impl_->attachmentTransition) {
    impl_->scheduleDisplay();
  }
}

void MetalHost::reschedulePendingFrame() {
  requireMainThread();
  if (impl_->view == nil || !impl_->attachment ||
      !impl_->invalidation.hasPendingFrame()) {
    return;
  }
  impl_->scheduleDisplay();
}

void MetalHost::drawIfNeeded() {
  requireMainThread();
  if (!impl_->invalidation.beginFrame()) return;

  const platform::FrameInvalidation::FrameId frameId =
      impl_->invalidation.activeFrameId();
  const std::uint64_t attachmentGeneration = impl_->attachmentGeneration;
  __strong NSView* view = impl_->view;
  const auto attachment = impl_->attachment;
  const auto document = impl_->document;
  const auto hostChanged = [&] {
    return !impl_->hasAttachment(attachmentGeneration, view, attachment);
  };

  if (hostChanged()) {
    impl_->invalidation.abandonFrame(frameId);
    return;
  }

  id<CAMetalDrawable> drawable = [attachment->metalLayer nextDrawable];
  if (drawable == nil || hostChanged()) {
    impl_->invalidation.abandonFrame(frameId);
    return;
  }

  id<MTLTexture> texture = drawable.texture;
  constexpr NSUInteger maximumExtent =
      static_cast<NSUInteger>(std::numeric_limits<int>::max());
  if (texture == nil || texture.width > maximumExtent ||
      texture.height > maximumExtent) {
    impl_->invalidation.failFrame(frameId);
    return;
  }
  const int width = static_cast<int>(texture.width);
  const int height = static_cast<int>(texture.height);
  if (width <= 0 || height <= 0 || hostChanged()) {
    impl_->invalidation.failFrame(frameId);
    return;
  }

  GrMtlTextureInfo textureInfo{};
  textureInfo.fTexture.retain((__bridge GrMTLHandle)texture);
  const GrBackendRenderTarget backendTarget =
      GrBackendRenderTargets::MakeMtl(width, height, textureInfo);
  if (!backendTarget.isValid() || hostChanged()) {
    impl_->invalidation.failFrame(frameId);
    return;
  }

  const auto resources = attachment->resources;
  auto surface = SkSurfaces::WrapBackendRenderTarget(
      resources->skiaContext.get(), backendTarget, kTopLeft_GrSurfaceOrigin,
      kBGRA_8888_SkColorType, sk_sp<SkColorSpace>{}, nullptr);
  if (surface == nullptr || hostChanged()) {
    impl_->invalidation.failFrame(frameId);
    return;
  }

  const SkiaSurfaceFramePlan plan = skiaSurfaceFramePlan(attachment->role);
  SkCanvas* canvas = surface->getCanvas();
  canvas->clear(static_cast<SkColor>(plan.clearColorArgb));
  if (document) {
    canvas->save();
    const float scale = static_cast<float>(attachment->backingScale);
    canvas->scale(scale, scale);
    for (std::size_t layerIndex = 0; layerIndex < plan.layerCount;
         ++layerIndex) {
      resources->renderer.drawLayer(*canvas, *document,
                                    plan.layers[layerIndex]);
    }
    canvas->restore();
  }
  if (hostChanged()) {
    impl_->invalidation.abandonFrame(frameId);
    return;
  }

  GrFlushInfo flushInfo{};
  GrDirectContext* context = resources->skiaContext.get();
  context->flush(surface.get(), SkSurfaces::BackendSurfaceAccess::kPresent,
                 flushInfo);
  if (hostChanged()) {
    impl_->invalidation.abandonFrame(frameId);
    return;
  }
  if (!context->submit(GrSyncCpu::kNo)) {
    impl_->invalidation.failFrame(frameId);
    return;
  }
  if (hostChanged()) {
    impl_->invalidation.abandonFrame(frameId);
    return;
  }

  id<MTLCommandBuffer> presentationBuffer =
      [resources->commandQueue commandBuffer];
  if (presentationBuffer == nil || hostChanged()) {
    impl_->invalidation.failFrame(frameId);
    return;
  }
  [presentationBuffer presentDrawable:drawable];
  if (hostChanged()) {
    impl_->invalidation.abandonFrame(frameId);
    return;
  }
  [presentationBuffer commit];
  ++impl_->committedFrameCount;
  if (hostChanged()) {
    impl_->invalidation.abandonFrame(frameId);
    return;
  }
  impl_->invalidation.completeFrame(frameId);
}

std::uint64_t MetalHost::committedFrameCount() const noexcept {
  requireMainThread();
  return impl_ != nullptr ? impl_->committedFrameCount : 0;
}

std::uint64_t MetalHost::nativeDisplayRequestCount() const noexcept {
  requireMainThread();
  return impl_ != nullptr ? impl_->nativeDisplayRequestCount : 0;
}

bool MetalHost::isReady() const noexcept {
  requireMainThread();
  return impl_ != nullptr && impl_->attachment &&
         impl_->attachment->resources &&
         impl_->attachment->resources->isReady() &&
         impl_->attachment->metalLayer != nil;
}

MetalSurfaceRole MetalHost::surfaceRole() const noexcept {
  requireMainThread();
  return impl_ != nullptr ? impl_->role : MetalSurfaceRole::Base;
}

bool MetalHost::sharesRenderResourcesWith(
    const MetalHost& other) const noexcept {
  requireMainThread();
  return impl_ != nullptr && other.impl_ != nullptr &&
         impl_->sharedResources != nullptr &&
         impl_->sharedResources == other.impl_->sharedResources;
}

}  // namespace canvas::macos
