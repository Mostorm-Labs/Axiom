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
elseif(APPLE AND CMAKE_SYSTEM_NAME STREQUAL "iOS")
  if(CMAKE_OSX_SYSROOT MATCHES "simulator")
    set(CANVAS_POC04_SDK_TARGET "ios-simulator-arm64-metal")
  else()
    set(CANVAS_POC04_SDK_TARGET "ios-arm64-metal")
  endif()
elseif(APPLE)
  set(CANVAS_POC04_SDK_TARGET "macos-arm64-metal")
else()
  message(FATAL_ERROR
    "POC-04 SkParagraph target supports Web, Windows, Apple, or Android")
endif()

set(CANVAS_POC04_SKIA_SDK_ROOT
    "${CANVAS_REPOSITORY_ROOT}/.deps/skia-sdk/${CANVAS_POC04_SDK_TARGET}/release"
    CACHE PATH "Verified R1 Full release Skia SDK")
set(CANVAS_POC04_SKIA_PROFILE
    "${CANVAS_REPOSITORY_ROOT}/tools/skia/profiles/r1-full-v1.json"
    CACHE FILEPATH "R1 Full Skia SDK profile used for fetch guidance")
set(CANVAS_POC04_SKIA_LOCK
    "${CANVAS_REPOSITORY_ROOT}/r1-full-skia-sdk.lock.json" CACHE FILEPATH
    "R1 Full Skia SDK lock")
set(CANVAS_POC04_SKIA_EXPECTED_ID "" CACHE STRING
    "Producer-only expected SDK ID; consumers must use the committed lock")
if(NOT CANVAS_POC04_SKIA_EXPECTED_ID AND NOT EXISTS "${CANVAS_POC04_SKIA_LOCK}")
  message(FATAL_ERROR
    "R1 Full Skia SDK is not published/locked yet")
endif()
if(NOT EXISTS "${CANVAS_POC04_SKIA_SDK_ROOT}/lib/cmake/CanvasSkia/CanvasSkiaConfig.cmake")
  message(FATAL_ERROR
    "Missing verified RichText SDK. Run: python3 tools/skia/fetch.py --profile "
    "tools/skia/profiles/r1-full-v1.json --lock r1-full-skia-sdk.lock.json "
    "--target ${CANVAS_POC04_SDK_TARGET} --variant release")
endif()

file(READ "${CANVAS_POC04_SKIA_SDK_ROOT}/manifest.json" _poc04_manifest)
if(NOT CANVAS_POC04_SKIA_EXPECTED_ID)
  file(READ "${CANVAS_POC04_SKIA_LOCK}" _poc04_lock)
  string(JSON _poc04_lock_schema ERROR_VARIABLE _poc04_lock_schema_error GET
         "${_poc04_lock}" schema_version)
  if(_poc04_lock_schema EQUAL 2)
    set(_poc04_requires_release_variant TRUE)
    string(JSON CANVAS_POC04_SKIA_EXPECTED_ID ERROR_VARIABLE _poc04_lock_error GET
           "${_poc04_lock}" targets "${CANVAS_POC04_SDK_TARGET}" variants
           release sdk_id)
  else()
    set(_poc04_requires_release_variant FALSE)
    string(JSON CANVAS_POC04_SKIA_EXPECTED_ID ERROR_VARIABLE _poc04_lock_error GET
           "${_poc04_lock}" targets "${CANVAS_POC04_SDK_TARGET}" sdk_id)
  endif()
else()
  # Historical source-free reproduction passes the immutable v1 SDK ID
  # explicitly. Schema-v1 manifests have no variant field.
  set(_poc04_requires_release_variant FALSE)
  set(_poc04_lock_schema_error "")
  set(_poc04_lock_error "")
endif()
if(_poc04_requires_release_variant)
  string(JSON _poc04_actual_variant ERROR_VARIABLE _poc04_variant_error GET
         "${_poc04_manifest}" identity variant)
else()
  set(_poc04_actual_variant "release")
  set(_poc04_variant_error "")
endif()
string(JSON _poc04_actual_id ERROR_VARIABLE _poc04_manifest_error GET
       "${_poc04_manifest}" sdk_id)
if(_poc04_lock_schema_error OR _poc04_lock_error OR _poc04_variant_error OR
   _poc04_manifest_error OR
   NOT _poc04_actual_variant STREQUAL "release" OR
   NOT CANVAS_POC04_SKIA_EXPECTED_ID STREQUAL _poc04_actual_id)
  message(FATAL_ERROR
    "RichText SDK manifest does not match the locked R1 Full release SDK")
endif()

find_package(CanvasSkia CONFIG REQUIRED
  PATHS "${CANVAS_POC04_SKIA_SDK_ROOT}/lib/cmake/CanvasSkia"
  NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
if(NOT TARGET CanvasSkia::Paragraph)
  # Historical schema-v1 POC-04 packages predate capability aliases. Preserve
  # explicit reproductions while the active R1 Full package uses Paragraph.
  add_library(CanvasSkia::Paragraph INTERFACE IMPORTED)
  set_target_properties(CanvasSkia::Paragraph PROPERTIES
    INTERFACE_LINK_LIBRARIES "CanvasSkia::Skia")
endif()
add_library(canvas_poc04_skparagraph STATIC skparagraph_layout.cpp)
target_link_libraries(canvas_poc04_skparagraph PUBLIC
  canvas_poc04_core CanvasSkia::Paragraph)
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
if(CanvasSkia_ICU_DATA_PATH)
  set(CANVAS_POC04_ICU_DATA_PATH "${CanvasSkia_ICU_DATA_PATH}"
      CACHE INTERNAL "POC-04 Windows ICU data")
endif()
