#import <AppKit/AppKit.h>

#include "platform/macos/metal_host.h"

#include <memory>

@interface CanvasMetalView : NSView
@end

@implementation CanvasMetalView {
  std::unique_ptr<canvas::macos::MetalHost> metalHost_;
}

- (instancetype)initWithFrame:(NSRect)frame {
  self = [super initWithFrame:frame];
  if (self != nil) {
    metalHost_ = std::make_unique<canvas::macos::MetalHost>();
    if (!metalHost_->attachToView((__bridge void*)self)) return nil;
    [self resizeDrawable];
  }
  return self;
}

- (void)dealloc {
  metalHost_.reset();
}

- (void)setFrameSize:(NSSize)newSize {
  [super setFrameSize:newSize];
  [self resizeDrawable];
}

- (void)viewDidChangeBackingProperties {
  [super viewDidChangeBackingProperties];
  [self resizeDrawable];
}

- (void)viewDidMoveToWindow {
  [super viewDidMoveToWindow];
  [self resizeDrawable];
}

- (void)drawRect:(NSRect)dirtyRect {
  (void)dirtyRect;
  metalHost_->drawIfNeeded();
}

- (void)resizeDrawable {
  if (!metalHost_) return;
  NSScreen* screen = self.window.screen;
  if (screen == nil) screen = NSScreen.mainScreen;
  const CGFloat scale = screen != nil ? screen.backingScaleFactor : 1.0;
  metalHost_->resize(self.bounds.size.width, self.bounds.size.height, scale);
}

@end

@interface CanvasApplicationDelegate : NSObject <NSApplicationDelegate>
@property(nonatomic, strong) NSWindow* window;
@end

@implementation CanvasApplicationDelegate

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
  (void)notification;
  const NSRect frame = NSMakeRect(0.0, 0.0, 1280.0, 720.0);
  const NSWindowStyleMask style = NSWindowStyleMaskTitled |
      NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable |
      NSWindowStyleMaskResizable;
  self.window = [[NSWindow alloc] initWithContentRect:frame
                                             styleMask:style
                                               backing:NSBackingStoreBuffered
                                                 defer:NO];
  self.window.title = @"Mostorm Canvas";
  self.window.contentView = [[CanvasMetalView alloc] initWithFrame:frame];
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
