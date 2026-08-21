// Apple input adapters pass coalesced UITouch/NSEvent samples into the same
// protocol. Metal drawable/layer ownership remains on this platform target;
// prediction is an optional hint to Axiom InkEngine, never backend semantics.
#define ARC_PLATFORM_KIND ARC_APPLE_PLATFORM_KIND
#ifndef ARC_CREATE_BACKEND
#error "Apple target must define ARC_CREATE_BACKEND"
#endif
#ifndef ARC_CREATE_INPUT_SOURCE
#error "Apple target must define ARC_CREATE_INPUT_SOURCE"
#endif
#define ARC_REQUIRES_PLATFORM_HANDLE 1
#define ARC_INPUT_CAPABILITIES \
  (ARC_INPUT_CAPABILITY_PRESSURE | ARC_INPUT_CAPABILITY_TILT | \
   ARC_INPUT_CAPABILITY_COALESCED | ARC_INPUT_CAPABILITY_PREDICTION_HINT | \
   ARC_INPUT_CAPABILITY_HOVER | ARC_INPUT_CAPABILITY_ERASER)
#define ARC_PRESENTATION_CAPABILITIES \
  (ARC_PRESENTATION_CAPABILITY_INDEPENDENT_TARGET | \
   ARC_PRESENTATION_CAPABILITY_REPLACE_TRUNCATE | \
   ARC_PRESENTATION_CAPABILITY_PRESENT_RECEIPT | \
   ARC_PRESENTATION_CAPABILITY_SHARED_GPU_DEVICE)
#include "../backend_factory.inc"
