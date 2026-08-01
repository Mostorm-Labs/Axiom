#pragma once

#import <AppKit/AppKit.h>

#include <cstdint>
#include <memory>

#include "platform/macos/skia_frame_plan.h"

namespace canvas::document {
class Document;
}

namespace canvas::macos {
class MetalRenderResources;
}

// Objective-C++ AppKit adapter shared by the demo and scheduling integration
// test. The C++ MetalHost remains behind this view boundary.
@interface CanvasMetalView : NSView <CALayerDelegate>

- (instancetype)initWithFrame:(NSRect)frame
                   surfaceRole:(canvas::macos::MetalSurfaceRole)surfaceRole
               renderResources:(std::shared_ptr<canvas::macos::MetalRenderResources>)
                                  renderResources;

- (void)setCanvasDocument:
    (std::shared_ptr<const canvas::document::Document>)document;
- (std::uint64_t)nativeDisplayRequestCount;
- (std::uint64_t)committedFrameCount;
- (BOOL)sharesRenderResourcesWithView:(CanvasMetalView*)other;
- (canvas::macos::MetalSurfaceRole)surfaceRole;

@end
