#include "apple_metal_adapter.h"

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <TargetConditionals.h>

#include "include/core/SkColorSpace.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkSurface.h"
#include "include/gpu/ganesh/GrDirectContext.h"
#include "include/gpu/ganesh/SkSurfaceGanesh.h"
#include "include/gpu/ganesh/mtl/GrMtlBackendContext.h"
#include "include/gpu/ganesh/mtl/GrMtlDirectContext.h"
#include "include/ports/SkCFObject.h"
#include "scene_compiler.h"
#include "skia_scene_renderer.h"

namespace canvas::poc01 {

struct AppleMetalAdapter::Impl {
  id<MTLDevice> device = nil;
  id<MTLCommandQueue> queue = nil;
  sk_sp<GrDirectContext> context;
  sk_sp<SkSurface> surface;
  RuntimeScene scene;
  const Document* scene_document = nullptr;
  SkiaSceneRenderer renderer;
  uint32_t width = 0;
  uint32_t height = 0;
  std::string device_name;

  void ReleaseBackend() {
#if TARGET_OS_OSX
    // This adapter owns a healthy Metal backend. Drop every Skia object that
    // can reference it before asking Ganesh to finish work and release native
    // resources. abandonContext() is reserved for a lost/unusable backend and
    // intentionally skips that cleanup path.
    surface.reset();
    renderer.ResetCaches();
    scene = {};
    scene_document = nullptr;
    if (context != nullptr) {
      context->releaseResourcesAndAbandonContext();
      context.reset();
    }
#else
    // Preserve the already accepted iOS/iPadOS backend behavior. The desktop
    // cleanup fix is intentionally scoped to macOS physical evidence.
    surface.reset();
    scene = {};
    scene_document = nullptr;
    if (context != nullptr) {
      context->abandonContext();
      context.reset();
    }
#endif
    queue = nil;
    device = nil;
    width = 0;
    height = 0;
    device_name.clear();
  }
};

AppleMetalAdapter::AppleMetalAdapter() : impl_(std::make_unique<Impl>()) {}

AppleMetalAdapter::~AppleMetalAdapter() { impl_->ReleaseBackend(); }

canvas_poc_status_t AppleMetalAdapter::Initialize(uint32_t width,
                                                   uint32_t height) {
  if (width == 0 || height == 0) {
    SetLastError("Apple Metal dimensions must be non-zero");
    return CANVAS_POC_STATUS_INVALID_ARGUMENT;
  }
  @autoreleasepool {
    impl_->ReleaseBackend();
    impl_->device = MTLCreateSystemDefaultDevice();
    if (impl_->device == nil) {
      SetLastError("Metal device is unavailable");
      return CANVAS_POC_STATUS_PLATFORM_ERROR;
    }
    impl_->queue = [impl_->device newCommandQueue];
    if (impl_->queue == nil) {
      SetLastError("Metal command queue creation failed");
      return CANVAS_POC_STATUS_PLATFORM_ERROR;
    }

    GrMtlBackendContext backend;
    backend.fDevice = sk_ret_cfp((__bridge GrMTLHandle)impl_->device);
    backend.fQueue = sk_ret_cfp((__bridge GrMTLHandle)impl_->queue);
    impl_->context = GrDirectContexts::MakeMetal(backend);
    if (impl_->context == nullptr) {
      SetLastError("Skia failed to create Ganesh Metal context");
      return CANVAS_POC_STATUS_PLATFORM_ERROR;
    }

    const SkImageInfo info = SkImageInfo::Make(
        static_cast<int>(width), static_cast<int>(height),
        kRGBA_8888_SkColorType, kPremul_SkAlphaType, SkColorSpace::MakeSRGB());
    impl_->surface = SkSurfaces::RenderTarget(
        impl_->context.get(), skgpu::Budgeted::kNo, info, 0,
        kTopLeft_GrSurfaceOrigin, nullptr);
    if (impl_->surface == nullptr) {
      SetLastError("Skia failed to allocate Metal render target");
      return CANVAS_POC_STATUS_RENDER_ERROR;
    }
    impl_->width = width;
    impl_->height = height;
    impl_->device_name = [[impl_->device name] UTF8String];
    return CANVAS_POC_STATUS_OK;
  }
}

canvas_poc_status_t AppleMetalAdapter::Render(
    const Document& document, std::vector<uint8_t>* readback) {
  if (impl_->surface == nullptr || impl_->context == nullptr) {
    SetLastError("Apple Metal adapter is not initialized");
    return CANVAS_POC_STATUS_PLATFORM_ERROR;
  }
  @autoreleasepool {
    if (impl_->scene_document != &document ||
        impl_->scene.source_revision != document.state().revision) {
      impl_->scene = SceneCompiler().Compile(document);
      impl_->scene_document = &document;
    }
    canvas_poc_status_t status =
        impl_->renderer.Draw(*impl_->surface->getCanvas(), impl_->scene,
                             document.assets());
    if (status != CANVAS_POC_STATUS_OK) {
      return status;
    }
    // A window swapchain normally bounds the number of frames in flight. The
    // macOS POC renders to one offscreen target, so synchronize each measured
    // frame instead of allowing Metal command buffers and their resources to
    // queue without backpressure. The caller's frame timing therefore includes
    // GPU completion. Preserve the accepted mobile submission behavior.
#if TARGET_OS_OSX
    constexpr GrSyncCpu sync = GrSyncCpu::kYes;
#else
    const GrSyncCpu sync =
        readback == nullptr ? GrSyncCpu::kNo : GrSyncCpu::kYes;
#endif
    impl_->context->flushAndSubmit(impl_->surface.get(), sync);
    return readback == nullptr
               ? CANVAS_POC_STATUS_OK
               : impl_->renderer.Readback(*impl_->surface, impl_->width,
                                          impl_->height, readback);
  }
}

const std::string& AppleMetalAdapter::device_name() const {
  return impl_->device_name;
}

canvas_poc_status_t RenderAppleMetal(const Document& document,
                                     AppleMetalResult* result) {
  if (result == nullptr) {
    SetLastError("Apple Metal result must not be null");
    return CANVAS_POC_STATUS_INVALID_ARGUMENT;
  }
  AppleMetalAdapter adapter;
  canvas_poc_status_t status = adapter.Initialize(document.state().page_width,
                                                   document.state().page_height);
  if (status != CANVAS_POC_STATUS_OK) return status;
  status = adapter.Render(document, &result->rgba);
  result->device_name = adapter.device_name();
  return status;
}

}  // namespace canvas::poc01
