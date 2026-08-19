#ifndef CANVAS_POC04_APPLE_IME_ADAPTER_H_
#define CANVAS_POC04_APPLE_IME_ADAPTER_H_

#import <TargetConditionals.h>

#if TARGET_OS_OSX
#import <AppKit/AppKit.h>
@class CanvasPoc04MacTextView;
typedef CanvasPoc04MacTextView CanvasPoc04AppleTextView;
#else
#import <UIKit/UIKit.h>
@class CanvasPoc04IosTextView;
typedef CanvasPoc04IosTextView CanvasPoc04AppleTextView;
#endif

#import "canvas_poc04/canvas_poc04.h"

@interface CanvasPoc04AppleTextViewBase : NSObject
- (instancetype)initWithSession:(canvas_poc04_handle_t)session;
@property(nonatomic, readonly) canvas_poc04_handle_t session;
@end

#if TARGET_OS_OSX
@interface CanvasPoc04MacTextView : NSView <NSTextInputClient>
- (instancetype)initWithFrame:(NSRect)frame
                       session:(canvas_poc04_handle_t)session;
@property(nonatomic, readonly) canvas_poc04_handle_t session;
@property(nonatomic, readonly) NSDictionary<NSString*, id>* behaviorReport;
@end
#else
@interface CanvasPoc04IosTextPosition : UITextPosition {
  NSUInteger _offset;
}
@property(nonatomic, readonly) NSUInteger offset;
- (instancetype)initWithOffset:(NSUInteger)offset;
@end

@interface CanvasPoc04IosTextRange : UITextRange {
  NSRange _utf16Range;
}
@property(nonatomic, readonly) NSRange utf16Range;
- (instancetype)initWithRange:(NSRange)range;
@end

@interface CanvasPoc04IosTextSelectionRect : UITextSelectionRect {
  CGRect _rect;
  BOOL _containsStart;
  BOOL _containsEnd;
}
- (instancetype)initWithRect:(CGRect)rect containsStart:(BOOL)containsStart
                   containsEnd:(BOOL)containsEnd;
@end

@interface CanvasPoc04IosTextView : UIView <UITextInput> {
  canvas_poc04_handle_t _session;
  UITextInputStringTokenizer* _tokenizer;
  __weak id<UITextInputDelegate> _inputDelegate;
  NSDictionary<NSAttributedStringKey, id>* _markedTextStyle;
  UITextStorageDirection _selectionAffinity;
  NSDictionary<NSString*, id>* _behaviorReport;
  void (^_inputEventHandler)(NSString* event);
}
- (instancetype)initWithFrame:(CGRect)frame
                       session:(canvas_poc04_handle_t)session;
@property(nonatomic, readonly) canvas_poc04_handle_t session;
@property(nonatomic, readonly) NSDictionary<NSString*, id>* behaviorReport;
@property(nonatomic, copy) void (^inputEventHandler)(NSString* event);
@end
#endif

#endif
