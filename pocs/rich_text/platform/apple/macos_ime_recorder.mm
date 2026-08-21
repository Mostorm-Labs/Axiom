#import "apple_ime_adapter.h"

#import <AppKit/AppKit.h>

#include <algorithm>
#include <cstring>
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

NSString* SessionText(canvas_poc04_handle_t session) {
  uint64_t length = 0;
  if (canvas_poc04_session_presented_utf16_length(session, &length) !=
      CANVAS_POC04_STATUS_OK) return @"";
  size_t required = 0;
  if (canvas_poc04_session_presented_text_range_utf8(
          session, 0, length, nullptr, 0, &required) !=
      CANVAS_POC04_STATUS_BUFFER_TOO_SMALL) return @"";
  std::string value(required, '\0');
  if (canvas_poc04_session_presented_text_range_utf8(
          session, 0, length, value.data(), value.size(), &required) !=
      CANVAS_POC04_STATUS_OK) return @"";
  value.resize(required ? required - 1 : 0);
  return [[NSString alloc] initWithBytes:value.data()
                                  length:value.size()
                                encoding:NSUTF8StringEncoding] ?: @"";
}

NSArray<NSNumber*>* RangeValue(NSRange range) {
  return range.location == NSNotFound ? @[] : @[@(range.location), @(range.length)];
}

BOOL HasPinyinFlow(NSArray<NSDictionary<NSString*, id>*>* events) {
  // macOS Simplified Chinese may commit the first candidate as soon as the
  // space separating syllables is pressed. Accept that native event shape:
  // n → ni → 你, then h → ha → hao → 你好. This is the same controlled
  // semantic flow as ni hao → 你好, without requiring one synthetic event to
  // contain the entire ASCII pre-edit string.
  NSArray<NSString*>* expected = @[@"n", @"ni", @"你", @"你h", @"你ha",
                                   @"你hao", @"你好"];
  if (events.count < expected.count) return NO;
  NSUInteger cursor = 0;
  for (NSDictionary<NSString*, id>* event in events) {
    if (cursor == expected.count) break;
    if ([event[@"presented_text"] isEqualToString:expected[cursor]]) ++cursor;
  }
  return cursor == expected.count;
}

void WriteJson(NSDictionary<NSString*, id>* value, NSString* path) {
  NSError* error = nil;
  NSData* data = [NSJSONSerialization dataWithJSONObject:value
                                                   options:NSJSONWritingPrettyPrinted |
                                                           NSJSONWritingSortedKeys
                                                     error:&error];
  if (!data || error) return;
  if (path.length != 0) [data writeToFile:path options:NSDataWritingAtomic error:nil];
  std::cout.write(static_cast<const char*>(data.bytes), data.length);
  std::cout << "\n";
}

}  // namespace

@interface CanvasPoc04MacRecordingView : CanvasPoc04MacTextView
@property(nonatomic, strong) NSMutableArray<NSDictionary<NSString*, id>*>* events;
@property(nonatomic, copy) NSString* outputPath;
@property(nonatomic, assign) BOOL interactive;
- (void)writeReport;
@end

@implementation CanvasPoc04MacRecordingView

- (void)record:(NSString*)event {
  [self.events addObject:@{
    @"event": event,
    @"presented_text": SessionText(self.session),
    @"selection": RangeValue([self selectedRange]),
    @"marked_range": RangeValue([self markedRange]),
  }];
  if (self.interactive) [self writeReport];
}

- (void)insertText:(id)string replacementRange:(NSRange)replacementRange {
  [super insertText:string replacementRange:replacementRange];
  [self record:@"insertText"];
}

- (void)setMarkedText:(id)string selectedRange:(NSRange)selectedRange
      replacementRange:(NSRange)replacementRange {
  [super setMarkedText:string selectedRange:selectedRange replacementRange:replacementRange];
  [self record:@"setMarkedText"];
}

- (void)unmarkText {
  [super unmarkText];
  [self record:@"unmarkText"];
}

- (void)writeReport {
  NSString* text = SessionText(self.session);
  NSRange selection = [self selectedRange];
  NSRange marked = [self markedRange];
  canvas_poc04_rect_t caret{};
  if (selection.location != NSNotFound) {
    static_cast<void>(canvas_poc04_session_caret_rect_for_offset_utf16(
        self.session, selection.location, 1.0F, &caret));
  }
  NSString* finalEvent = self.events.lastObject[@"event"];
  const BOOL controlled = [text isEqualToString:@"你好"] && HasPinyinFlow(self.events) &&
                          marked.location == NSNotFound && self.events.count != 0 &&
                          ([finalEvent isEqualToString:@"unmarkText"] ||
                           [finalEvent isEqualToString:@"insertText"]);
  BOOL markedSeen = NO, commitSeen = NO;
  for (NSDictionary* event in self.events) {
    markedSeen |= [event[@"event"] isEqualToString:@"setMarkedText"];
    commitSeen |= [event[@"event"] isEqualToString:@"unmarkText"] ||
                  [event[@"event"] isEqualToString:@"insertText"];
  }
  NSString* digest = [NSString stringWithUTF8String:Digest(self.session).c_str()] ?: @"";
  WriteJson(@{
    @"schema_version": @1, @"platform": @"macos",
    @"protocol": @"NSTextInputClient", @"evidence": self.interactive
        ? @"controlled-system-input" : @"adapter-preflight",
    @"controlled_flow": @"ni hao -> 你好", @"controlled_flow_passed": @(controlled),
    @"final_text": text, @"selection": RangeValue(selection),
    @"marked_range": RangeValue(marked),
    @"caret": @[@(caret.x), @(caret.y), @(caret.width), @(caret.height)],
    @"digest": digest, @"event_count": @(self.events.count),
    @"observed_marked_text": @(markedSeen), @"observed_commit": @(commitSeen),
    @"events": self.events,
  }, self.outputPath);
}

@end

int main(int argc, const char* argv[]) {
  @autoreleasepool {
    BOOL interactive = NO;
    NSString* output = @"";
    NSTimeInterval timeout = 30.0;
    for (int index = 1; index < argc; ++index) {
      if (strcmp(argv[index], "--interactive") == 0) interactive = YES;
      else if (strcmp(argv[index], "--output") == 0 && index + 1 < argc)
        output = [NSString stringWithUTF8String:argv[++index]] ?: @"";
      else if (strcmp(argv[index], "--timeout-seconds") == 0 && index + 1 < argc)
        timeout = std::max(1.0, atof(argv[++index]));
    }
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    canvas_poc04_create_info_t info{sizeof(info), CANVAS_POC04_ABI_VERSION};
    canvas_poc04_handle_t session = 0;
    if (canvas_poc04_session_create(&info, &session) != CANVAS_POC04_STATUS_OK) return 1;
    CanvasPoc04MacRecordingView* view =
        [[CanvasPoc04MacRecordingView alloc] initWithFrame:NSMakeRect(0, 0, 720, 420)
                                                   session:session];
    view.events = [NSMutableArray array]; view.outputPath = output; view.interactive = interactive;
    NSWindow* window = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(120, 120, 720, 420)
                  styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                    backing:NSBackingStoreBuffered defer:NO];
    window.title = @"Canvas POC-04 macOS IME validation"; window.contentView = view;
    [window makeKeyAndOrderFront:nil]; [NSApp activateIgnoringOtherApps:YES];
    [window makeFirstResponder:view]; static_cast<void>(canvas_poc04_session_focus(session));
    if (!interactive) {
      [view insertText:@"hello" replacementRange:NSMakeRange(NSNotFound, 0)];
      [view setMarkedText:@"拼音" selectedRange:NSMakeRange(1, 1)
         replacementRange:NSMakeRange(NSNotFound, 0)];
      [view unmarkText]; [view doCommandBySelector:@selector(deleteBackward:)];
      [view writeReport]; [NSApp terminate:nil];
    } else {
      [view writeReport];
      [NSTimer scheduledTimerWithTimeInterval:timeout repeats:NO block:^(NSTimer* timer) {
        (void)timer; [view writeReport]; [NSApp terminate:nil];
      }];
      NSLog(@"POC-04 macOS: switch to Chinese Pinyin, type ni hao, commit 你好");
      [NSApp run];
    }
    static_cast<void>(canvas_poc04_session_destroy(session));
  }
  return 0;
}
