#pragma once

#import <AppKit/AppKit.h>

#include <memory>

namespace canvas::document {
class Document;
}

@class CanvasMetalView;

// Fixed AppKit host stack, ordered back to front as:
//   opaque Base Metal -> embedded native views -> transparent Overlay Metal.
// Embedded views are deliberately supplied by later WKWebView work; this
// increment owns their stable container and the input-routing policy.
@interface CanvasCompositionView : NSView

@property(nonatomic, strong, readonly) CanvasMetalView* baseMetalView;
@property(nonatomic, strong, readonly) NSView* embeddedContainerView;
@property(nonatomic, strong, readonly) CanvasMetalView* overlayMetalView;
@property(nonatomic, getter=isEmbeddedInteractionEnabled)
    BOOL embeddedInteractionEnabled;

- (void)setCanvasDocument:
    (std::shared_ptr<const canvas::document::Document>)document;
- (void)setEditableCanvasDocument:
    (std::shared_ptr<canvas::document::Document>)document;

@end
