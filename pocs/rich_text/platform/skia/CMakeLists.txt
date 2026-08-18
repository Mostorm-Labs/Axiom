if(EMSCRIPTEN)
  set(CANVAS_POC04_SDK_TARGET "web-wasm-webgl2")
elseif(WIN32)
  set(CANVAS_POC04_SDK_TARGET "windows-x64-d3d12")
elseif(ANDROID)
  if(CMAKE_ANDROID_ARCH_ABI STREQUAL "arm64-v8a")
    set(CANVAS_POC04_SDK_TARGET "android-arm64-v8a-gles3")
  elseif(CMAKE_ANDROID_ARCH_ABI STREQUAL "x86_64")
    set(CANVAS_POC04_SDK_TARGET "android-x86_64-gles3")
  else()
    message(FATAL_ERROR "POC-04 Android supports arm64-v8a and x86_64")
  endif()
else()
  message(FATAL_ERROR "POC-04 SkParagraph target supports Web, Windows, or Android")
endif()

set(CANVAS_POC04_SKIA_SDK_ROOT
    "${CANVAS_REPOSITORY_ROOT}/.deps/skia-sdk-poc04/${CANVAS_POC04_SDK_TARGET}"
    CACHE PATH "Verified POC-04 RichText Skia SDK")
set(CANVAS_POC04_SKIA_LOCK
    "${CMAKE_CURRENT_SOURCE_DIR}/../../skia-sdk.lock.json" CACHE FILEPATH
    "POC-04 RichText SDK lock")
set(CANVAS_POC04_SKIA_EXPECTED_ID "" CACHE STRING
    "Producer-only expected SDK ID; consumers must use the committed lock")
if(NOT CANVAS_POC04_SKIA_EXPECTED_ID AND NOT EXISTS "${CANVAS_POC04_SKIA_LOCK}")
  message(FATAL_ERROR
    "POC-04 RichText SDK is not published/locked yet. Produce the SDK, then run "
    "tools/skia/update_lock.py --tag <tag> --output pocs/rich_text/skia-sdk.lock.json")
endif()
if(NOT EXISTS "${CANVAS_POC04_SKIA_SDK_ROOT}/lib/cmake/CanvasSkia/CanvasSkiaConfig.cmake")
  message(FATAL_ERROR
    "Missing verified RichText SDK. Run: python3 tools/skia/fetch.py --profile "
    "tools/skia/profiles/poc04-richtext-v1.json --lock pocs/rich_text/skia-sdk.lock.json "
    "--install-root .deps/skia-sdk-poc04 --target ${CANVAS_POC04_SDK_TARGET}")
endif()

file(READ "${CANVAS_POC04_SKIA_SDK_ROOT}/manifest.json" _poc04_manifest)
if(NOT CANVAS_POC04_SKIA_EXPECTED_ID)
  file(READ "${CANVAS_POC04_SKIA_LOCK}" _poc04_lock)
  string(JSON CANVAS_POC04_SKIA_EXPECTED_ID ERROR_VARIABLE _poc04_lock_error GET
         "${_poc04_lock}" targets "${CANVAS_POC04_SDK_TARGET}" sdk_id)
endif()
string(JSON _poc04_actual_id ERROR_VARIABLE _poc04_manifest_error GET
       "${_poc04_manifest}" sdk_id)
if(_poc04_lock_error OR _poc04_manifest_error OR
   NOT CANVAS_POC04_SKIA_EXPECTED_ID STREQUAL _poc04_actual_id)
  message(FATAL_ERROR "POC-04 RichText SDK manifest does not match its lock")
endif()

find_package(CanvasSkia CONFIG REQUIRED
  PATHS "${CANVAS_POC04_SKIA_SDK_ROOT}/lib/cmake/CanvasSkia"
  NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
add_library(canvas_poc04_skparagraph STATIC skparagraph_layout.cpp)
target_link_libraries(canvas_poc04_skparagraph PUBLIC
  canvas_poc04_core CanvasSkia::Skia)
target_include_directories(canvas_poc04_skparagraph PUBLIC .)
target_compile_definitions(canvas_poc04_skparagraph PRIVATE SK_ENABLE_PARAGRAPH)
add_library(canvas_poc04_canonical_behavior_fixture STATIC
  ../../tools/canonical_behavior_fixture.cpp)
target_include_directories(canvas_poc04_canonical_behavior_fixture PUBLIC
  ../../tools)
target_link_libraries(canvas_poc04_canonical_behavior_fixture PUBLIC
  canvas_poc04_skparagraph canvas_poc04_core)
set(CANVAS_POC04_FONT_PATH "${CanvasSkia_FONT_PATH}" CACHE INTERNAL "POC-04 font")
set(CANVAS_POC04_CJK_FONT_PATH "${CanvasSkia_CJK_FONT_PATH}" CACHE INTERNAL "POC-04 CJK font")
