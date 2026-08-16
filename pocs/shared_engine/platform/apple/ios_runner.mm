#import <UIKit/UIKit.h>

#include <filesystem>
#include <dispatch/dispatch.h>
#include <exception>
#include <string>

#include "runner_support.h"

@interface CanvasPocAppDelegate : UIResponder <UIApplicationDelegate>
@property(nonatomic, strong) UIWindow* window;
@end

@implementation CanvasPocAppDelegate
- (BOOL)application:(UIApplication*)application
    didFinishLaunchingWithOptions:(NSDictionary*)launchOptions {
  (void)application;
  (void)launchOptions;
  self.window = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
  UIViewController* controller = [[UIViewController alloc] init];
  controller.view.backgroundColor = UIColor.systemBackgroundColor;
  UILabel* label = [[UILabel alloc] initWithFrame:controller.view.bounds];
  label.numberOfLines = 0;
  label.textAlignment = NSTextAlignmentCenter;
  label.autoresizingMask = UIViewAutoresizingFlexibleWidth |
                           UIViewAutoresizingFlexibleHeight;
  [controller.view addSubview:label];
  self.window.rootViewController = controller;
  [self.window makeKeyAndVisible];

  dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
    NSURL* documents = [[NSFileManager defaultManager]
        URLForDirectory:NSDocumentDirectory
               inDomain:NSUserDomainMask
      appropriateForURL:nil
                 create:YES
                  error:nil];
    try {
      NSBundle* bundle = NSBundle.mainBundle;
      const std::filesystem::path resources([bundle.resourcePath UTF8String]);
      const std::filesystem::path documentsPath([documents.path UTF8String]);
      const std::string platform =
          UIDevice.currentDevice.userInterfaceIdiom == UIUserInterfaceIdiomPad
              ? "ipados"
              : "ios";
      const std::string result = canvas::poc01::RunAppleAcceptance(
          resources, resources / "Roboto-Regular.ttf",
          documentsPath / "apple-actual.rgba", platform, 100, 60);
      NSURL* output =
          [documents URLByAppendingPathComponent:@"poc01-result.json"];
      [[NSString stringWithUTF8String:result.c_str()]
          writeToURL:output
          atomically:YES
          encoding:NSUTF8StringEncoding
          error:nil];
      NSLog(@"CANVAS_POC01_RESULT %s", result.c_str());
      dispatch_async(dispatch_get_main_queue(), ^{
        label.text = [NSString stringWithUTF8String:result.c_str()];
      });
    } catch (const std::exception& error) {
      const std::string message(error.what());
      NSURL* failure =
          [documents URLByAppendingPathComponent:@"poc01-failure.txt"];
      [[NSString stringWithUTF8String:message.c_str()]
          writeToURL:failure
          atomically:YES
          encoding:NSUTF8StringEncoding
          error:nil];
      NSLog(@"CANVAS_POC01_FAILURE %s", message.c_str());
      dispatch_async(dispatch_get_main_queue(), ^{
        label.text = [NSString stringWithUTF8String:message.c_str()];
      });
    }
  });
  return YES;
}
@end

int main(int argc, char* argv[]) {
  @autoreleasepool {
    return UIApplicationMain(argc, argv, nil,
                             NSStringFromClass(CanvasPocAppDelegate.class));
  }
}
