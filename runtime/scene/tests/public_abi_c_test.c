#include "docs/api/canvas_runtime_api_v1.h"

int canvas_rf01_public_abi_c_compile_test(void) {
    return CANVAS_RUNTIME_ABI_VERSION == 1u ? 0 : 1;
}
