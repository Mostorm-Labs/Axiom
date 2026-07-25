#pragma once

#import <AppKit/AppKit.h>

#include <cstdint>
#include <memory>

namespace canvas::document {
class Document;
}

// Objective-C++ AppKit adapter shared by the demo and scheduling integration
// test. The C++ MetalHost remains behind this view boundary.
@interface CanvasMetalView : NSView <CALayerDelegate>

- (void)setCanvasDocument:
    (std::shared_ptr<const canvas::document::Document>)document;
- (std::uint64_t)nativeDisplayRequestCount;
- (std::uint64_t)committedFrameCount;

@end
