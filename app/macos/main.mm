#import <AppKit/AppKit.h>

#include "canvas/document/document.h"
#include "platform/macos/composition_view.h"

#include <memory>
#include <utility>

@interface CanvasApplicationDelegate : NSObject <NSApplicationDelegate>
@property(nonatomic, strong) NSWindow* window;
@end

@implementation CanvasApplicationDelegate

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
  (void)notification;
  const NSRect frame = NSMakeRect(0.0, 0.0, 1280.0, 720.0);
  const NSWindowStyleMask style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                  NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;
  self.window = [[NSWindow alloc] initWithContentRect:frame
                                            styleMask:style
                                              backing:NSBackingStoreBuffered
                                                defer:NO];
  self.window.title = @"Mostorm Canvas";
  auto document = std::make_shared<canvas::document::Document>();
  canvas::document::StrokeNode stroke;
  stroke.points = {{{120.0F, 180.0F}, 0.6F, 1},
                   {{280.0F, 110.0F}, 0.8F, 2},
                   {{460.0F, 240.0F}, 1.0F, 3},
                   {{680.0F, 150.0F}, 0.7F, 4}};
  stroke.width = 8.0F;
  stroke.colorArgb = 0xFF2563EB;
  if (!document->add({"macos-skia-sample",
                      canvas::document::LayerClass::Annotation,
                      {116.0F, 106.0F, 568.0F, 138.0F},
                      {},
                      std::move(stroke)})) {
    [NSApp terminate:nil];
    return;
  }
  CanvasCompositionView* view =
      [[CanvasCompositionView alloc] initWithFrame:frame];
  [view setEditableCanvasDocument:document];
  self.window.contentView = view;
  [self.window center];
  [self.window makeKeyAndOrderFront:nil];
  [NSApp activateIgnoringOtherApps:YES];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender {
  (void)sender;
  return YES;
}

@end

int main(int argc, const char* argv[]) {
  (void)argc;
  (void)argv;
  @autoreleasepool {
    NSApplication* application = [NSApplication sharedApplication];
    application.activationPolicy = NSApplicationActivationPolicyRegular;
    CanvasApplicationDelegate* delegate = [[CanvasApplicationDelegate alloc] init];
    application.delegate = delegate;
    [application run];
  }
  return 0;
}
