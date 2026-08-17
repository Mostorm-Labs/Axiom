#ifndef CANVAS_POC_OPERATIONS_H_
#define CANVAS_POC_OPERATIONS_H_

#include <string_view>

#include "document.h"

namespace canvas::poc01 {

// Experimental replay schema. The entire NDJSON batch is transactional.
canvas_poc_status_t ApplyOperations(Document& document,
                                    std::string_view ndjson);

}  // namespace canvas::poc01

#endif  // CANVAS_POC_OPERATIONS_H_
