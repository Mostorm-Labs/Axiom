#import "apple_ime_adapter.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace {

NSString* TextForRange(canvas_poc04_handle_t session, uint64_t location,
                       uint64_t length) {
  size_t required = 0;
  if (canvas_poc04_session_presented_text_range_utf8(
          session, location, length, nullptr, 0, &required) !=
      CANVAS_POC04_STATUS_BUFFER_TOO_SMALL) {
    return @"";
  }
  std::string utf8(required, '\0');
  if (canvas_poc04_session_presented_text_range_utf8(
          session, location, length, utf8.data(), utf8.size(), &required) !=
      CANVAS_POC04_STATUS_OK) {
    return @"";
  }
  utf8.resize(required ? required - 1 : 0);
  return [[NSString alloc] initWithBytes:utf8.data()
                                  length:utf8.size()
                                encoding:NSUTF8StringEncoding];
}

NSString* StringValue(id value) {
  if ([value isKindOfClass:[NSAttributedString class]]) {
    return [value string];
  }
  return [value isKindOfClass:[NSString class]] ? value : @"";
}

NSUInteger PresentedLength(canvas_poc04_handle_t session) {
  uint64_t length = 0;
  return canvas_poc04_session_presented_utf16_length(session, &length) ==
                 CANVAS_POC04_STATUS_OK
             ? static_cast<NSUInteger>(std::min<uint64_t>(length, NSUIntegerMax))
             : 0;
}

BOOL QueryMarked(canvas_poc04_handle_t session, NSRange* range) {
  canvas_poc04_utf16_range_t value{};
  uint32_t active = 0;
  if (canvas_poc04_session_composition_range_flat_utf16(
          session, &value, &active) != CANVAS_POC04_STATUS_OK || !active) {
    return NO;
  }
  if (range) *range = NSMakeRange((NSUInteger)value.location, (NSUInteger)value.length);
  return YES;
}

BOOL QuerySelection(canvas_poc04_handle_t session, NSRange* range) {
  canvas_poc04_utf16_range_t value{};
  if (canvas_poc04_session_selection_flat_utf16(session, &value) !=
      CANVAS_POC04_STATUS_OK) {
    return NO;
  }
  if (range) *range = NSMakeRange((NSUInteger)value.location, (NSUInteger)value.length);
  return YES;
}

void SetSelection(canvas_poc04_handle_t session, NSRange range) {
  static_cast<void>(canvas_poc04_session_set_selection_flat_utf16(
      session, range.location, range.location + range.length));
}

void Replace(canvas_poc04_handle_t session, NSRange range, NSString* text) {
  NSData* data = [text dataUsingEncoding:NSUTF8StringEncoding];
  const char* bytes = static_cast<const char*>(data.bytes);
  const size_t size = data.length;
  static_cast<void>(canvas_poc04_session_replace_range_utf8(
      session, range.location, range.length, bytes ? bytes : "", size));
}

BOOL CommitMarkedText(canvas_poc04_handle_t session, NSString* text) {
  NSRange marked{};
  if (!QueryMarked(session, &marked)) return NO;
  NSData* data = [text dataUsingEncoding:NSUTF8StringEncoding];
  const NSUInteger length = text.length;
  if (length > UINT32_MAX ||
      canvas_poc04_session_update_composition_utf8_with_selection(
          session, static_cast<const char*>(data.bytes), data.length,
          static_cast<uint32_t>(length), static_cast<uint32_t>(length)) !=
          CANVAS_POC04_STATUS_OK) {
    return NO;
  }
  return canvas_poc04_session_commit_composition(session) ==
         CANVAS_POC04_STATUS_OK;
}

CGRect CaretRect(canvas_poc04_handle_t session, NSUInteger offset, CGFloat width) {
  canvas_poc04_rect_t rect{};
  if (canvas_poc04_session_caret_rect_for_offset_utf16(
          session, offset, width, &rect) != CANVAS_POC04_STATUS_OK) {
    return CGRectZero;
  }
  return CGRectMake(rect.x, rect.y, rect.width, rect.height);
}

}  // namespace

#if TARGET_OS_OSX

@interface CanvasPoc04MacTextView ()
@property(nonatomic, readwrite) NSDictionary<NSString*, id>* behaviorReport;
@end

@implementation CanvasPoc04MacTextView {
  canvas_poc04_handle_t _session;
}

- (instancetype)initWithFrame:(NSRect)frame
                       session:(canvas_poc04_handle_t)session {
  self = [super initWithFrame:frame];
  if (self) {
    _session = session;
    _behaviorReport = @{
      @"platform": @"macos",
      @"protocol": @"NSTextInputClient",
      @"utf16_offsets": @YES,
      @"composition": @YES,
      @"geometry": @YES,
    };
  }
  return self;
}

- (canvas_poc04_handle_t)session { return _session; }
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)becomeFirstResponder {
  const BOOL result = [super becomeFirstResponder];
  if (result) static_cast<void>(canvas_poc04_session_focus(_session));
  return result;
}
- (BOOL)resignFirstResponder {
  static_cast<void>(canvas_poc04_session_blur(_session));
  return [super resignFirstResponder];
}

- (void)insertText:(id)string replacementRange:(NSRange)replacementRange {
  NSString* text = StringValue(string);
  if (CommitMarkedText(_session, text)) {
    [self setNeedsDisplay:YES];
    return;
  }
  NSRange selection{};
  if (replacementRange.location != NSNotFound) {
    Replace(_session, replacementRange, text);
  } else if (QuerySelection(_session, &selection)) {
    Replace(_session, selection, text);
  }
  [self setNeedsDisplay:YES];
}

- (void)doCommandBySelector:(SEL)selector {
  NSRange selection{};
  if (selector == @selector(deleteBackward:)) {
    static_cast<void>(canvas_poc04_session_delete_surrounding_utf16(_session, 1, 0));
  } else if (selector == @selector(deleteForward:)) {
    static_cast<void>(canvas_poc04_session_delete_surrounding_utf16(_session, 0, 1));
  } else if (selector == @selector(undo:)) {
    static_cast<void>(canvas_poc04_session_undo(_session));
  } else if (selector == @selector(redo:)) {
    static_cast<void>(canvas_poc04_session_redo(_session));
  } else if (selector == @selector(cancelOperation:)) {
    static_cast<void>(canvas_poc04_session_cancel_composition(_session));
  } else if (QuerySelection(_session, &selection) && selector == @selector(moveLeft:)) {
    if (selection.location) SetSelection(_session, NSMakeRange(selection.location - 1, 0));
  }
  [self setNeedsDisplay:YES];
}

- (void)setMarkedText:(id)string
        selectedRange:(NSRange)selectedRange
      replacementRange:(NSRange)replacementRange {
  NSRange marked{};
  if (!QueryMarked(_session, &marked)) {
    NSRange selection{};
    if (replacementRange.location == NSNotFound) {
      QuerySelection(_session, &selection);
    } else {
      selection = replacementRange;
    }
    SetSelection(_session, selection);
    if (canvas_poc04_session_begin_composition(_session) != CANVAS_POC04_STATUS_OK) return;
  }
  NSString* text = StringValue(string);
  if (selectedRange.location > UINT32_MAX ||
      NSMaxRange(selectedRange) > UINT32_MAX) return;
  NSData* data = [text dataUsingEncoding:NSUTF8StringEncoding];
  static_cast<void>(canvas_poc04_session_update_composition_utf8_with_selection(
      _session, static_cast<const char*>(data.bytes), data.length,
      static_cast<uint32_t>(selectedRange.location),
      static_cast<uint32_t>(NSMaxRange(selectedRange))));
  [self setNeedsDisplay:YES];
}

- (void)unmarkText {
  static_cast<void>(canvas_poc04_session_commit_composition(_session));
  [self setNeedsDisplay:YES];
}
- (NSRange)selectedRange {
  NSRange range{};
  return QuerySelection(_session, &range) ? range : NSMakeRange(NSNotFound, 0);
}
- (NSRange)markedRange {
  NSRange range{};
  return QueryMarked(_session, &range) ? range : NSMakeRange(NSNotFound, 0);
}
- (BOOL)hasMarkedText { return QueryMarked(_session, nullptr); }
- (NSAttributedString*)attributedSubstringForProposedRange:(NSRange)range
                                                   actualRange:(NSRangePointer)actualRange {
  const NSUInteger length = PresentedLength(_session);
  if (range.location == NSNotFound || range.location > length) return nil;
  const NSUInteger actualLength = std::min(range.length, length - range.location);
  if (actualRange) *actualRange = NSMakeRange(range.location, actualLength);
  return [[NSAttributedString alloc] initWithString:
      TextForRange(_session, range.location, actualLength)];
}
- (NSArray<NSAttributedStringKey>*)validAttributesForMarkedText { return @[]; }
- (NSRect)firstRectForCharacterRange:(NSRange)range actualRange:(NSRangePointer)actualRange {
  canvas_poc04_rect_t value{};
  canvas_poc04_utf16_range_t actual{};
  if (canvas_poc04_session_first_rect_for_range_utf16(
          _session, range.location, range.length, self.bounds.size.width,
          &value, &actual) != CANVAS_POC04_STATUS_OK) return NSZeroRect;
  if (actualRange) *actualRange = NSMakeRange(actual.location, actual.length);
  NSRect local = NSMakeRect(value.x, value.y, value.width, value.height);
  NSRect windowRect = [self convertRect:local toView:nil];
  return [self.window convertRectToScreen:windowRect];
}
- (NSUInteger)characterIndexForPoint:(NSPoint)point {
  NSPoint local = [self convertPoint:point fromView:nil];
  uint64_t offset = 0;
  if (canvas_poc04_session_character_offset_for_point(
          _session, local.x, local.y, self.bounds.size.width, &offset) !=
      CANVAS_POC04_STATUS_OK) return NSNotFound;
  return (NSUInteger)offset;
}
@end

#else

@implementation CanvasPoc04IosTextPosition
- (instancetype)initWithOffset:(NSUInteger)offset {
  self = [super init];
  if (self) _offset = offset;
  return self;
}
- (NSUInteger)offset { return _offset; }
@end

@implementation CanvasPoc04IosTextRange
- (instancetype)initWithRange:(NSRange)range {
  self = [super init];
  if (self) _utf16Range = range;
  return self;
}
- (NSRange)utf16Range { return _utf16Range; }
- (UITextPosition*)start { return [[CanvasPoc04IosTextPosition alloc] initWithOffset:_utf16Range.location]; }
- (UITextPosition*)end { return [[CanvasPoc04IosTextPosition alloc] initWithOffset:NSMaxRange(_utf16Range)]; }
- (BOOL)isEmpty { return _utf16Range.length == 0; }
@end

@implementation CanvasPoc04IosTextSelectionRect
- (instancetype)initWithRect:(CGRect)rect containsStart:(BOOL)containsStart
                   containsEnd:(BOOL)containsEnd {
  self = [super init];
  if (self) { _rect = rect; _containsStart = containsStart; _containsEnd = containsEnd; }
  return self;
}
- (CGRect)rect { return _rect; }
- (NSWritingDirection)writingDirection { return NSWritingDirectionLeftToRight; }
- (BOOL)containsStart { return _containsStart; }
- (BOOL)containsEnd { return _containsEnd; }
- (BOOL)isVertical { return NO; }
@end

@implementation CanvasPoc04IosTextView

@synthesize inputDelegate = _inputDelegate;
@synthesize markedTextStyle = _markedTextStyle;
@synthesize selectionAffinity = _selectionAffinity;
@synthesize behaviorReport = _behaviorReport;
@synthesize inputEventHandler = _inputEventHandler;

- (instancetype)initWithFrame:(CGRect)frame
                       session:(canvas_poc04_handle_t)session {
  self = [super initWithFrame:frame];
  if (self) {
    _session = session;
    _tokenizer = [[UITextInputStringTokenizer alloc] initWithTextInput:self];
    _selectionAffinity = UITextStorageDirectionForward;
    _behaviorReport = @{
      @"platform": UIDevice.currentDevice.userInterfaceIdiom == UIUserInterfaceIdiomPad ? @"ipados" : @"ios",
      @"protocol": @"UITextInput+UIKeyInput",
      @"utf16_offsets": @YES,
      @"composition": @YES,
      @"geometry": @YES,
    };
    self.backgroundColor = UIColor.systemBackgroundColor;
  }
  return self;
}

- (canvas_poc04_handle_t)session { return _session; }
- (BOOL)canBecomeFirstResponder { return YES; }
- (BOOL)becomeFirstResponder {
  const BOOL result = [super becomeFirstResponder];
  if (result) static_cast<void>(canvas_poc04_session_focus(_session));
  return result;
}
- (BOOL)resignFirstResponder {
  static_cast<void>(canvas_poc04_session_blur(_session));
  return [super resignFirstResponder];
}
- (BOOL)hasText { return PresentedLength(_session) != 0; }
- (void)insertText:(NSString*)text {
  [self.inputDelegate textWillChange:self];
  [self.inputDelegate selectionWillChange:self];
  if (CommitMarkedText(_session, text)) {
    [self.inputDelegate selectionDidChange:self];
    [self.inputDelegate textDidChange:self];
    [self setNeedsDisplay];
    if (self.inputEventHandler) self.inputEventHandler(@"insertText");
    return;
  }
  NSRange selection{};
  if (QuerySelection(_session, &selection)) Replace(_session, selection, text);
  [self.inputDelegate selectionDidChange:self];
  [self.inputDelegate textDidChange:self];
  [self setNeedsDisplay];
  if (self.inputEventHandler) self.inputEventHandler(@"insertText");
}
- (void)deleteBackward {
  [self.inputDelegate textWillChange:self];
  [self.inputDelegate selectionWillChange:self];
  static_cast<void>(canvas_poc04_session_delete_surrounding_utf16(_session, 1, 0));
  [self.inputDelegate selectionDidChange:self];
  [self.inputDelegate textDidChange:self];
  [self setNeedsDisplay];
  if (self.inputEventHandler) self.inputEventHandler(@"deleteBackward");
}
- (NSString*)textInRange:(UITextRange*)range {
  CanvasPoc04IosTextRange* value = (CanvasPoc04IosTextRange*)range;
  return TextForRange(_session, value.utf16Range.location, value.utf16Range.length);
}
- (void)replaceRange:(UITextRange*)range withText:(NSString*)text {
  CanvasPoc04IosTextRange* value = (CanvasPoc04IosTextRange*)range;
  [self.inputDelegate textWillChange:self];
  [self.inputDelegate selectionWillChange:self];
  Replace(_session, value.utf16Range, text);
  [self.inputDelegate selectionDidChange:self];
  [self.inputDelegate textDidChange:self];
  [self setNeedsDisplay];
  if (self.inputEventHandler) self.inputEventHandler(@"replaceRange");
}
- (UITextRange*)selectedTextRange {
  NSRange range{};
  return QuerySelection(_session, &range)
             ? [[CanvasPoc04IosTextRange alloc] initWithRange:range]
             : nil;
}
- (void)setSelectedTextRange:(UITextRange*)range {
  if (!range) return;
  CanvasPoc04IosTextRange* value = (CanvasPoc04IosTextRange*)range;
  [self.inputDelegate selectionWillChange:self];
  SetSelection(_session, value.utf16Range);
  [self.inputDelegate selectionDidChange:self];
  if (self.inputEventHandler) self.inputEventHandler(@"setSelectedTextRange");
}
- (UITextRange*)markedTextRange {
  NSRange range{};
  return QueryMarked(_session, &range)
             ? [[CanvasPoc04IosTextRange alloc] initWithRange:range]
             : nil;
}
- (void)setMarkedText:(NSString*)markedText selectedRange:(NSRange)selectedRange {
  [self.inputDelegate textWillChange:self];
  [self.inputDelegate selectionWillChange:self];
  NSRange marked{};
  if (!QueryMarked(_session, &marked)) {
    NSRange selection{};
    QuerySelection(_session, &selection);
    SetSelection(_session, selection);
    if (canvas_poc04_session_begin_composition(_session) != CANVAS_POC04_STATUS_OK) {
      [self.inputDelegate selectionDidChange:self];
      [self.inputDelegate textDidChange:self];
      return;
    }
  }
  if (selectedRange.location > UINT32_MAX ||
      NSMaxRange(selectedRange) > UINT32_MAX) {
    [self.inputDelegate selectionDidChange:self];
    [self.inputDelegate textDidChange:self];
    return;
  }
  NSData* data = [markedText ?: @"" dataUsingEncoding:NSUTF8StringEncoding];
  static_cast<void>(canvas_poc04_session_update_composition_utf8_with_selection(
      _session, static_cast<const char*>(data.bytes), data.length,
      static_cast<uint32_t>(selectedRange.location),
      static_cast<uint32_t>(NSMaxRange(selectedRange))));
  [self.inputDelegate selectionDidChange:self];
  [self.inputDelegate textDidChange:self];
  [self setNeedsDisplay];
  if (self.inputEventHandler) self.inputEventHandler(@"setMarkedText");
}
- (void)unmarkText {
  [self.inputDelegate textWillChange:self];
  [self.inputDelegate selectionWillChange:self];
  static_cast<void>(canvas_poc04_session_commit_composition(_session));
  [self.inputDelegate selectionDidChange:self];
  [self.inputDelegate textDidChange:self];
  [self setNeedsDisplay];
  if (self.inputEventHandler) self.inputEventHandler(@"unmarkText");
}
- (UITextPosition*)beginningOfDocument { return [[CanvasPoc04IosTextPosition alloc] initWithOffset:0]; }
- (UITextPosition*)endOfDocument { return [[CanvasPoc04IosTextPosition alloc] initWithOffset:PresentedLength(_session)]; }
- (UITextRange*)textRangeFromPosition:(UITextPosition*)fromPosition
                         toPosition:(UITextPosition*)toPosition {
  NSUInteger start = [(CanvasPoc04IosTextPosition*)fromPosition offset];
  NSUInteger end = [(CanvasPoc04IosTextPosition*)toPosition offset];
  return [[CanvasPoc04IosTextRange alloc] initWithRange:NSMakeRange(std::min(start, end), start <= end ? end - start : start - end)];
}
- (UITextPosition*)positionFromPosition:(UITextPosition*)position offset:(NSInteger)offset {
  NSInteger value = (NSInteger)[(CanvasPoc04IosTextPosition*)position offset] + offset;
  value = std::clamp<NSInteger>(value, 0, (NSInteger)PresentedLength(_session));
  return [[CanvasPoc04IosTextPosition alloc] initWithOffset:(NSUInteger)value];
}
- (UITextPosition*)positionFromPosition:(UITextPosition*)position
                            inDirection:(UITextLayoutDirection)direction
                                  offset:(NSInteger)offset {
  const BOOL backwards = direction == UITextLayoutDirectionLeft || direction == UITextLayoutDirectionUp;
  return [self positionFromPosition:position offset:backwards ? -offset : offset];
}
- (NSComparisonResult)comparePosition:(UITextPosition*)position toPosition:(UITextPosition*)other {
  NSUInteger left = [(CanvasPoc04IosTextPosition*)position offset];
  NSUInteger right = [(CanvasPoc04IosTextPosition*)other offset];
  return left < right ? NSOrderedAscending : (left > right ? NSOrderedDescending : NSOrderedSame);
}
- (NSInteger)offsetFromPosition:(UITextPosition*)from toPosition:(UITextPosition*)to {
  return (NSInteger)[(CanvasPoc04IosTextPosition*)to offset] - (NSInteger)[(CanvasPoc04IosTextPosition*)from offset];
}
- (id<UITextInputTokenizer>)tokenizer { return _tokenizer; }
- (UITextPosition*)positionWithinRange:(UITextRange*)range farthestInDirection:(UITextLayoutDirection)direction {
  CanvasPoc04IosTextRange* value = (CanvasPoc04IosTextRange*)range;
  return direction == UITextLayoutDirectionLeft || direction == UITextLayoutDirectionUp
             ? value.start : value.end;
}
- (UITextRange*)characterRangeByExtendingPosition:(UITextPosition*)position inDirection:(UITextLayoutDirection)direction {
  UITextPosition* other = [self positionFromPosition:position inDirection:direction offset:1];
  return [self textRangeFromPosition:position toPosition:other];
}
- (NSWritingDirection)baseWritingDirectionForPosition:(UITextPosition*)position inDirection:(UITextStorageDirection)direction { return NSWritingDirectionLeftToRight; }
- (void)setBaseWritingDirection:(NSWritingDirection)writingDirection forRange:(UITextRange*)range {}
- (CGRect)caretRectForPosition:(UITextPosition*)position {
  return CaretRect(_session, [(CanvasPoc04IosTextPosition*)position offset], self.bounds.size.width);
}
- (CGRect)firstRectForRange:(UITextRange*)range {
  CanvasPoc04IosTextRange* value = (CanvasPoc04IosTextRange*)range;
  canvas_poc04_rect_t rect{};
  canvas_poc04_utf16_range_t actual{};
  if (canvas_poc04_session_first_rect_for_range_utf16(
          _session, value.utf16Range.location, value.utf16Range.length,
          self.bounds.size.width, &rect, &actual) != CANVAS_POC04_STATUS_OK) return CGRectZero;
  return CGRectMake(rect.x, rect.y, rect.width, rect.height);
}
- (NSArray<UITextSelectionRect*>*)selectionRectsForRange:(UITextRange*)range {
  CanvasPoc04IosTextRange* value = (CanvasPoc04IosTextRange*)range;
  CGRect rect = [self firstRectForRange:value];
  return @[[[CanvasPoc04IosTextSelectionRect alloc] initWithRect:rect
                                                   containsStart:YES
                                                     containsEnd:YES]];
}
- (UITextPosition*)closestPositionToPoint:(CGPoint)point {
  uint64_t offset = 0;
  if (canvas_poc04_session_character_offset_for_point(
          _session, point.x, point.y, self.bounds.size.width, &offset) != CANVAS_POC04_STATUS_OK) return nil;
  return [[CanvasPoc04IosTextPosition alloc] initWithOffset:offset];
}
- (UITextPosition*)closestPositionToPoint:(CGPoint)point withinRange:(UITextRange*)range {
  UITextPosition* position = [self closestPositionToPoint:point];
  if (!position) return nil;
  CanvasPoc04IosTextRange* value = (CanvasPoc04IosTextRange*)range;
  NSUInteger offset = std::clamp<NSUInteger>([(CanvasPoc04IosTextPosition*)position offset], value.utf16Range.location, NSMaxRange(value.utf16Range));
  return [[CanvasPoc04IosTextPosition alloc] initWithOffset:offset];
}
- (UITextRange*)characterRangeAtPoint:(CGPoint)point {
  UITextPosition* position = [self closestPositionToPoint:point];
  if (!position) return nil;
  NSUInteger offset = [(CanvasPoc04IosTextPosition*)position offset];
  NSUInteger length = PresentedLength(_session);
  return [[CanvasPoc04IosTextRange alloc] initWithRange:NSMakeRange(offset, offset < length ? 1 : 0)];
}
- (UIView*)textInputView { return self; }
- (NSDictionary<NSAttributedStringKey, id>*)textStylingAtPosition:(UITextPosition*)position inDirection:(UITextStorageDirection)direction { return @{}; }
@end

#endif
