#import <React/RCTBridgeModule.h>

#import "AxiomAppleHybridSurfaceHost.h"

/// Experimental probe used only by the POC acceptance corpus. It marks a
/// JavaScript stall window; CADisplayLink and native placement continue on the
/// native data plane and expose the observed frame count in the host label.
@interface AxiomPoc05Probe : NSObject <RCTBridgeModule>
@end

@implementation AxiomPoc05Probe

RCT_EXPORT_MODULE(AxiomPoc05Probe)

RCT_EXPORT_METHOD(beginJsStall:(double)milliseconds) {
  [AxiomAppleHybridSurfaceHost noteJsStallStarted:milliseconds];
}

RCT_EXPORT_METHOD(endJsStall) {
  // The deadline is intentionally retained so a native frame scheduled just
  // after JS returns still belongs to the same measured stall window.
}

@end
