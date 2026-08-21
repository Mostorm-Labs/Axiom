#include "docs/api/canvas_runtime_api_v1.h"

int canvasRf01PublicAbiCppCompileTest() {
    return CANVAS_RUNTIME_ABI_VERSION == 1u ? 0 : 1;
}
