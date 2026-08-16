#include "apple_metal_adapter.h"

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

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
  uint32_t width = 0;
  uint32_t height = 0;
  std::string device_name;
};

AppleMetalAdapter::AppleMetalAdapter() : impl_(std::make_unique<Impl>()) {}

AppleMetalAdapter::~AppleMetalAdapter() {
  if (impl_->context != nullptr) {
    impl_->context->abandonContext();
  }
}

canvas_poc_status_t AppleMetalAdapter::Initialize(uint32_t width,
                                                   uint32_t height) {
  if (width == 0 || height == 0) {
    SetLastError("Apple Metal dimensions must be non-zero");
    return CANVAS_POC_STATUS_INVALID_ARGUMENT;
  }
  @autoreleasepool {
    impl_->surface.reset();
    if (impl_->context != nullptr) {
      impl_->context->abandonContext();
      impl_->context.reset();
    }
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
  if (readback == nullptr) {
    SetLastError("Apple Metal readback must not be null");
    return CANVAS_POC_STATUS_INVALID_ARGUMENT;
  }
  if (impl_->surface == nullptr || impl_->context == nullptr) {
    SetLastError("Apple Metal adapter is not initialized");
    return CANVAS_POC_STATUS_PLATFORM_ERROR;
  }
  @autoreleasepool {
    const RuntimeScene scene = SceneCompiler().Compile(document);
    SkiaSceneRenderer renderer;
    canvas_poc_status_t status =
        renderer.Draw(*impl_->surface->getCanvas(), scene, document.assets());
    if (status != CANVAS_POC_STATUS_OK) {
      return status;
    }
    impl_->context->flushAndSubmit(impl_->surface.get(), GrSyncCpu::kYes);
    return renderer.Readback(*impl_->surface, impl_->width, impl_->height,
                             readback);
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
