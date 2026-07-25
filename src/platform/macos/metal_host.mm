#include "platform/macos/metal_host.h"

#include "platform/frame_invalidation.h"

#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <exception>
#include <utility>

namespace canvas::macos {

namespace {

bool isMainThread() { return [NSThread isMainThread]; }

void requireMainThread() {
  if (!isMainThread()) std::terminate();
}

CGFloat positiveScale(double backingScale) {
  return static_cast<CGFloat>(std::max(1.0, backingScale));
}

CGSize drawableSize(double widthInPoints, double heightInPoints,
                    double backingScale) {
  const CGFloat scale = positiveScale(backingScale);
  return CGSizeMake(std::max<CGFloat>(1.0, std::round(
                                      std::max(0.0, widthInPoints) * scale)),
                    std::max<CGFloat>(1.0, std::round(
                                      std::max(0.0, heightInPoints) * scale)));
}

}  // namespace

class MetalHost::Impl {
 public:
  platform::FrameInvalidation invalidation;
  __strong id<MTLDevice> device = nil;
  __strong id<MTLCommandQueue> commandQueue = nil;
  __strong CAMetalLayer* metalLayer = nil;
  __weak NSView* view = nil;
  std::uint64_t attachmentGeneration = 0;

  void scheduleDisplay() {
    if (view != nil) [view setNeedsDisplay:YES];
  }

  bool hasAttachment(std::uint64_t generation, NSView* expectedView,
                     CAMetalLayer* expectedLayer,
                     id<MTLCommandQueue> expectedCommandQueue) const {
    return attachmentGeneration == generation && view == expectedView &&
           metalLayer == expectedLayer && commandQueue == expectedCommandQueue &&
           expectedView != nil && expectedLayer != nil &&
           expectedCommandQueue != nil;
  }
};

MetalHost::MetalHost() : impl_(std::make_unique<Impl>()) {
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
  if (nativeView == nullptr || impl_->view != nil) {
    return false;
  }

  NSView* view = (__bridge NSView*)nativeView;
  if (![view isKindOfClass:[NSView class]]) return false;

  id<MTLDevice> device = MTLCreateSystemDefaultDevice();
  if (device == nil) return false;
  id<MTLCommandQueue> commandQueue = [device newCommandQueue];
  if (commandQueue == nil) return false;

  CAMetalLayer* metalLayer = [CAMetalLayer layer];
  metalLayer.device = device;
  metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
  metalLayer.framebufferOnly = YES;
  metalLayer.opaque = YES;
  metalLayer.presentsWithTransaction = NO;

  [view setWantsLayer:YES];
  view.layer = metalLayer;

  impl_->device = device;
  impl_->commandQueue = commandQueue;
  impl_->metalLayer = metalLayer;
  impl_->view = view;
  ++impl_->attachmentGeneration;
  impl_->invalidation.reset();
  return true;
}

void MetalHost::detachFromView() {
  requireMainThread();
  if (!impl_) return;
  ++impl_->attachmentGeneration;
  if (impl_->view != nil && impl_->view.layer == impl_->metalLayer) {
    impl_->view.layer = nil;
  }
  impl_->view = nil;
  impl_->metalLayer = nil;
  impl_->commandQueue = nil;
  impl_->device = nil;
  impl_->invalidation.reset();
}

void MetalHost::resize(double widthInPoints, double heightInPoints,
                       double backingScale) {
  requireMainThread();
  if (impl_->metalLayer == nil) return;

  const CGFloat scale = positiveScale(backingScale);
  impl_->metalLayer.contentsScale = scale;
  impl_->metalLayer.drawableSize =
      drawableSize(widthInPoints, heightInPoints, backingScale);
  invalidate();
}

void MetalHost::invalidate() {
  requireMainThread();
  if (impl_->view == nil || impl_->metalLayer == nil) return;
  if (impl_->invalidation.requestFrame()) impl_->scheduleDisplay();
}

void MetalHost::drawIfNeeded() {
  requireMainThread();
  if (!impl_->invalidation.beginFrame()) return;

  const platform::FrameInvalidation::FrameId frameId =
      impl_->invalidation.activeFrameId();
  const std::uint64_t attachmentGeneration = impl_->attachmentGeneration;
  __strong NSView* view = impl_->view;
  __strong CAMetalLayer* metalLayer = impl_->metalLayer;
  __strong id<MTLCommandQueue> commandQueue = impl_->commandQueue;
  const auto hostChanged = [&] {
    return !impl_->hasAttachment(attachmentGeneration, view, metalLayer,
                                 commandQueue);
  };

  if (hostChanged()) {
    impl_->invalidation.abandonFrame(frameId);
    return;
  }

  id<CAMetalDrawable> drawable = [metalLayer nextDrawable];
  if (drawable == nil || hostChanged()) {
    impl_->invalidation.abandonFrame(frameId);
    return;
  }

  MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
  if (pass == nil || hostChanged()) {
    impl_->invalidation.failFrame(frameId);
    return;
  }
  pass.colorAttachments[0].texture = drawable.texture;
  pass.colorAttachments[0].loadAction = MTLLoadActionClear;
  pass.colorAttachments[0].storeAction = MTLStoreActionStore;
  pass.colorAttachments[0].clearColor = MTLClearColorMake(0.075, 0.09, 0.13, 1.0);

  id<MTLCommandBuffer> commandBuffer = [commandQueue commandBuffer];
  if (commandBuffer == nil || hostChanged()) {
    impl_->invalidation.failFrame(frameId);
    return;
  }
  id<MTLRenderCommandEncoder> encoder =
      [commandBuffer renderCommandEncoderWithDescriptor:pass];
  if (encoder == nil || hostChanged()) {
    impl_->invalidation.failFrame(frameId);
    return;
  }
  [encoder endEncoding];
  if (hostChanged()) {
    impl_->invalidation.failFrame(frameId);
    return;
  }
  [commandBuffer presentDrawable:drawable];
  if (hostChanged()) {
    impl_->invalidation.failFrame(frameId);
    return;
  }
  [commandBuffer commit];
  if (hostChanged()) {
    impl_->invalidation.abandonFrame(frameId);
    return;
  }
  impl_->invalidation.completeFrame(frameId);
}

bool MetalHost::isReady() const noexcept {
  requireMainThread();
  return impl_ != nullptr && impl_->metalLayer != nil &&
         impl_->commandQueue != nil;
}

}  // namespace canvas::macos
