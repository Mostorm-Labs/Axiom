#import <UIKit/UIKit.h>

NS_ASSUME_NONNULL_BEGIN

/// Native owner for the Fabric component. React only updates the low-frequency
/// lifecycle/content props; placement, camera gestures and rendering stay here.
@interface AxiomAppleHybridSurfaceHost : UIView
- (void)setWebVisible:(BOOL)visible;
- (void)setFailureMode:(BOOL)failed;
- (void)setActivePage:(NSInteger)page;
- (void)setLifecycleGeneration:(NSInteger)generation;
+ (void)noteJsStallStarted:(NSTimeInterval)milliseconds;
+ (BOOL)isJsStallActive;
@end

NS_ASSUME_NONNULL_END
