#import "apple_ime_adapter.h"

#import <UIKit/UIKit.h>

#include <string>

NSString* Digest(canvas_poc04_handle_t session) {
  size_t required = 0;
  if (canvas_poc04_document_digest(session, nullptr, 0, &required) !=
      CANVAS_POC04_STATUS_BUFFER_TOO_SMALL) return @"";
  std::string result(required, '\0');
  if (canvas_poc04_document_digest(session, result.data(), result.size(),
                                   &required) != CANVAS_POC04_STATUS_OK) return @"";
  result.resize(required - 1);
  return [NSString stringWithUTF8String:result.c_str()];
}
@interface CanvasPoc04ImeRecorderController : UIViewController
@property(nonatomic, strong) CanvasPoc04IosTextView* textView;
@property(nonatomic, copy) NSString* report;
@property(nonatomic, strong) NSMutableArray<NSDictionary<NSString*, id>*>* systemInputEvents;
@property(nonatomic, strong) UILabel* instructionLabel;
@property(nonatomic, strong) UILabel* textLabel;
@end

namespace {

void WriteJson(NSDictionary<NSString*, id>* value, NSString* filename) {
  NSError* error = nil;
  NSData* data = [NSJSONSerialization dataWithJSONObject:value
                                                 options:NSJSONWritingPrettyPrinted |
                                                         NSJSONWritingSortedKeys
                                                   error:&error];
  if (!data || error) return;
  NSURL* directory = [[[NSFileManager defaultManager]
      URLsForDirectory:NSDocumentDirectory inDomains:NSUserDomainMask] firstObject];
  [data writeToURL:[directory URLByAppendingPathComponent:filename]
           options:NSDataWritingAtomic error:nil];
}

NSArray<NSNumber*>* RangeValue(UITextRange* range) {
  if (![range isKindOfClass:CanvasPoc04IosTextRange.class]) return @[];
  NSRange value = [(CanvasPoc04IosTextRange*)range utf16Range];
  return @[@(value.location), @(value.length)];
}

}

@implementation CanvasPoc04ImeRecorderController
- (void)recordSystemInputEvent:(NSString*)event {
  UITextRange* documentRange = [self.textView
      textRangeFromPosition:self.textView.beginningOfDocument
                 toPosition:self.textView.endOfDocument];
  NSString* text = [self.textView textInRange:documentRange] ?: @"";
  // The editor surface renders the same presented text directly from the C++
  // session.  Keep this label as a small callback indicator rather than a
  // second text renderer, otherwise the test UI can hide a rendering bug.
  self.textLabel.text = [NSString stringWithFormat:@"Last system callback: %@", event];
  NSDictionary<NSString*, id>* entry = @{
    @"event": event,
    @"presented_text": text,
    @"selection": RangeValue(self.textView.selectedTextRange),
    @"marked_range": RangeValue(self.textView.markedTextRange),
  };
  [self.systemInputEvents addObject:entry];

  BOOL observedMarkedText = NO;
  BOOL observedCommit = NO;
  for (NSDictionary<NSString*, id>* item in self.systemInputEvents) {
    observedMarkedText |= [item[@"event"] isEqual:@"setMarkedText"];
    observedCommit |= [item[@"event"] isEqual:@"unmarkText"] ||
                      [item[@"event"] isEqual:@"insertText"];
  }
  WriteJson(@{
    @"platform": UIDevice.currentDevice.userInterfaceIdiom == UIUserInterfaceIdiomPad
        ? @"ipados" : @"ios",
    @"protocol": @"UITextInput+UIKeyInput",
    @"evidence": @"system-keyboard-callback-capture",
    @"event_count": @(self.systemInputEvents.count),
    @"observed_marked_text": @(observedMarkedText),
    @"observed_commit": @(observedCommit),
    @"presented_text": text,
    @"selection": RangeValue(self.textView.selectedTextRange),
    @"marked_range": RangeValue(self.textView.markedTextRange),
    @"digest": Digest(self.textView.session),
    @"events": self.systemInputEvents,
  }, @"poc04-ime-system-input.json");
}

- (void)viewDidAppear:(BOOL)animated {
  [super viewDidAppear:animated];
  [self.textView becomeFirstResponder];
  [self.textView insertText:@"hello"];
  [self.textView setMarkedText:@"拼音" selectedRange:NSMakeRange(1, 1)];
  CanvasPoc04IosTextRange* marked = (CanvasPoc04IosTextRange*)self.textView.markedTextRange;
  CanvasPoc04IosTextRange* selected = (CanvasPoc04IosTextRange*)self.textView.selectedTextRange;
  CGRect caret = [self.textView caretRectForPosition:selected.end];
  [self.textView unmarkText];
  [self.textView deleteBackward];
  const NSRange marked_range = marked.utf16Range;
  const NSRange selected_range = selected.utf16Range;
  self.report = [NSString stringWithFormat:
      @"{\"platform\":\"%@\",\"protocol\":\"UITextInput+UIKeyInput\","
       "\"marked_range\":[%lu,%lu],\"selection\":[%lu,%lu],"
       "\"caret\":[%.2f,%.2f,%.2f,%.2f],\"digest\":\"%@\"}\n",
      UIDevice.currentDevice.userInterfaceIdiom == UIUserInterfaceIdiomPad ? @"ipados" : @"ios",
      (unsigned long)marked_range.location, (unsigned long)marked_range.length,
      (unsigned long)selected_range.location, (unsigned long)selected_range.length,
      caret.origin.x, caret.origin.y, caret.size.width, caret.size.height,
      Digest(self.textView.session)];
  NSURL* url = [[[NSFileManager defaultManager] URLsForDirectory:NSDocumentDirectory
                                                         inDomains:NSUserDomainMask] firstObject];
  [self.report writeToURL:[url URLByAppendingPathComponent:@"poc04-ime-result.json"]
                atomically:YES encoding:NSUTF8StringEncoding error:nil];
  NSLog(@"CANVAS_POC04_IME_RESULT %@", self.report);

  UITextRange* allText = [self.textView
      textRangeFromPosition:self.textView.beginningOfDocument
                 toPosition:self.textView.endOfDocument];
  [self.textView replaceRange:allText withText:@""];
  self.systemInputEvents = [NSMutableArray array];
  __weak CanvasPoc04ImeRecorderController* weakSelf = self;
  self.textView.inputEventHandler = ^(NSString* event) {
    [weakSelf recordSystemInputEvent:event];
  };
}
@end

@interface CanvasPoc04ImeRecorderAppDelegate : UIResponder <UIApplicationDelegate>
@property(nonatomic, strong) UIWindow* window;
@end

@implementation CanvasPoc04ImeRecorderAppDelegate
- (BOOL)application:(UIApplication*)application
    didFinishLaunchingWithOptions:(NSDictionary*)launchOptions {
  (void)application;
  (void)launchOptions;
  canvas_poc04_create_info_t info{sizeof(info), CANVAS_POC04_ABI_VERSION};
  canvas_poc04_handle_t session = 0;
  if (canvas_poc04_session_create(&info, &session) != CANVAS_POC04_STATUS_OK) return NO;
  static_cast<void>(canvas_poc04_session_focus(session));
  self.window = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
  CanvasPoc04ImeRecorderController* controller =
      [[CanvasPoc04ImeRecorderController alloc] init];
  controller.view = [[UIView alloc] initWithFrame:self.window.bounds];
  controller.view.backgroundColor = UIColor.systemBackgroundColor;
  // Construct the editor with the real window bounds. At app launch the
  // controller has no view yet, so reading controller.view.bounds here would
  // create a zero-sized view and make the recorder appear blank on hardware.
  controller.textView = [[CanvasPoc04IosTextView alloc]
      initWithFrame:CGRectMake(24.0, 136.0,
                               controller.view.bounds.size.width - 48.0,
                               controller.view.bounds.size.height - 176.0)
          session:session];
  controller.textView.autoresizingMask = UIViewAutoresizingFlexibleWidth |
                                         UIViewAutoresizingFlexibleHeight;
  [controller.view addSubview:controller.textView];
  controller.instructionLabel = [[UILabel alloc] initWithFrame:CGRectMake(
      24.0, 48.0, controller.view.bounds.size.width - 48.0, 60.0)];
  controller.instructionLabel.numberOfLines = 2;
  controller.instructionLabel.text =
      @"POC-04 system-keyboard capture\nSwitch to Chinese Pinyin and enter: 中文拼音";
  controller.instructionLabel.font = [UIFont preferredFontForTextStyle:UIFontTextStyleHeadline];
  controller.instructionLabel.textColor = UIColor.secondaryLabelColor;
  controller.instructionLabel.userInteractionEnabled = NO;
  controller.instructionLabel.autoresizingMask = UIViewAutoresizingFlexibleWidth;
  [controller.view addSubview:controller.instructionLabel];
  controller.textLabel = [[UILabel alloc] initWithFrame:CGRectMake(
      24.0, 108.0, controller.view.bounds.size.width - 48.0, 24.0)];
  controller.textLabel.numberOfLines = 1;
  controller.textLabel.text = @"Waiting for system keyboard input";
  controller.textLabel.font = [UIFont systemFontOfSize:13.0];
  controller.textLabel.textColor = UIColor.labelColor;
  controller.textLabel.userInteractionEnabled = NO;
  controller.textLabel.autoresizingMask = UIViewAutoresizingFlexibleWidth |
                                            UIViewAutoresizingFlexibleHeight;
  [controller.view addSubview:controller.textLabel];
  self.window.rootViewController = controller;
  [self.window makeKeyAndVisible];
  return YES;
}
@end

int main(int argc, char* argv[]) {
  @autoreleasepool {
    return UIApplicationMain(argc, argv, nil,
                             NSStringFromClass(CanvasPoc04ImeRecorderAppDelegate.class));
  }
}
