#ifndef CANVAS_POC_PLATFORM_BRIDGE_INTERNAL_H_
#define CANVAS_POC_PLATFORM_BRIDGE_INTERNAL_H_

#include <memory>

#include "canvas_poc/canvas_poc.h"
#include "document.h"

namespace canvas::poc01 {

// Private escape hatch for POC platform adapters. It is intentionally absent
// from the public C ABI and will be replaced when R1 defines product ownership.
std::shared_ptr<Document> ResolveDocumentForPlatform(
    canvas_poc_handle_t document);

}  // namespace canvas::poc01

#endif  // CANVAS_POC_PLATFORM_BRIDGE_INTERNAL_H_
