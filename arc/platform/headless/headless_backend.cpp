// Deterministic protocol oracle. It intentionally has no display or OS
// dependency; POC-06 uses it for replay, state-machine, fault and fuzz tests.
#define ARC_PLATFORM_KIND ARC_PLATFORM_HEADLESS
#define ARC_CREATE_BACKEND CreateHeadlessBackend
#define ARC_CREATE_INPUT_SOURCE CreateHeadlessInputSource
#define ARC_INPUT_CAPABILITIES 0
#define ARC_PRESENTATION_CAPABILITIES \
  (ARC_PRESENTATION_CAPABILITY_INDEPENDENT_TARGET | \
   ARC_PRESENTATION_CAPABILITY_REPLACE_TRUNCATE | \
   ARC_PRESENTATION_CAPABILITY_PRESENT_RECEIPT)
#include "../backend_factory.inc"
