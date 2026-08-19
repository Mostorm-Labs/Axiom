#import "apple_ime_adapter.h"

#import <AppKit/AppKit.h>

#include <iostream>
#include <string>

namespace {

std::string Digest(canvas_poc04_handle_t session) {
  size_t required = 0;
  if (canvas_poc04_document_digest(session, nullptr, 0, &required) !=
      CANVAS_POC04_STATUS_BUFFER_TOO_SMALL) return {};
  std::string result(required, '\0');
  if (canvas_poc04_document_digest(session, result.data(), result.size(),
                                   &required) != CANVAS_POC04_STATUS_OK) return {};
  result.resize(required - 1);
  return result;
}

}  // namespace

int main() {
  @autoreleasepool {
    [NSApplication sharedApplication];
    canvas_poc04_create_info_t info{sizeof(info), CANVAS_POC04_ABI_VERSION};
    canvas_poc04_handle_t session = 0;
    if (canvas_poc04_session_create(&info, &session) != CANVAS_POC04_STATUS_OK) {
      return 1;
    }
    if (canvas_poc04_session_focus(session) != CANVAS_POC04_STATUS_OK) {
      static_cast<void>(canvas_poc04_session_destroy(session));
      return 1;
    }
    CanvasPoc04MacTextView* view =
        [[CanvasPoc04MacTextView alloc] initWithFrame:NSMakeRect(0, 0, 640, 480)
                                              session:session];
    NSWindow* window = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(100, 100, 640, 480)
                  styleMask:NSWindowStyleMaskBorderless
                    backing:NSBackingStoreBuffered
                      defer:NO];
    window.contentView = view;
    [window makeKeyAndOrderFront:nil];
    [view insertText:@"hello" replacementRange:NSMakeRange(NSNotFound, 0)];
    [view setMarkedText:@"拼音" selectedRange:NSMakeRange(1, 1)
                       replacementRange:NSMakeRange(NSNotFound, 0)];
    const NSRange marked = [view markedRange];
    const NSRange selection = [view selectedRange];
    NSRange actual = NSMakeRange(0, 0);
    NSRect first = [view firstRectForCharacterRange:marked actualRange:&actual];
    const NSUInteger hit = [view characterIndexForPoint:NSMakePoint(first.origin.x + 1,
                                                                      first.origin.y + 1)];
    const BOOL has_text = [view hasMarkedText] &&
                          [[view attributedSubstringForProposedRange:marked
                                                            actualRange:&actual].string
                              isEqualToString:@"拼音"];
    [view unmarkText];
    [view doCommandBySelector:@selector(deleteBackward:)];
    std::cout << "{\"platform\":\"macos\",\"protocol\":\"NSTextInputClient\","
                 "\"marked_range\":["
              << marked.location << "," << marked.length << "],\"selection\":["
              << selection.location << "," << selection.length << "],"
                 "\"actual_range\":["
              << actual.location << "," << actual.length << "],\"hit\":" << hit
              << ",\"geometry\":[" << first.origin.x << "," << first.origin.y
              << "," << first.size.width << "," << first.size.height
              << "],\"composition_text\":" << (has_text ? "true" : "false")
              << ",\"digest\":\"" << Digest(session) << "\"}\n";
    static_cast<void>(canvas_poc04_session_destroy(session));
  }
  return 0;
}
