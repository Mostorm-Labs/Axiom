if(EMSCRIPTEN)
  set(CANVAS_POC01_SDK_TARGET "web-wasm-webgl2")
elseif(WIN32)
  set(CANVAS_POC01_SDK_TARGET "windows-x64-d3d12")
elseif(ANDROID)
  if(CMAKE_ANDROID_ARCH_ABI STREQUAL "arm64-v8a")
    set(CANVAS_POC01_SDK_TARGET "android-arm64-v8a-gles3")
  elseif(CMAKE_ANDROID_ARCH_ABI STREQUAL "x86_64")
    set(CANVAS_POC01_SDK_TARGET "android-x86_64-gles3")
  else()
    message(FATAL_ERROR "POC-01 Android supports only arm64-v8a and x86_64")
  endif()
elseif(APPLE AND CMAKE_SYSTEM_NAME STREQUAL "iOS")
  if(CMAKE_OSX_SYSROOT MATCHES "simulator")
    set(CANVAS_POC01_SDK_TARGET "ios-simulator-arm64-metal")
  else()
    set(CANVAS_POC01_SDK_TARGET "ios-arm64-metal")
  endif()
elseif(APPLE)
  if(CMAKE_OSX_ARCHITECTURES STREQUAL "x86_64" OR
     CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
    set(CANVAS_POC01_SDK_TARGET "macos-x64-metal")
  else()
    set(CANVAS_POC01_SDK_TARGET "macos-arm64-metal")
  endif()
else()
  message(FATAL_ERROR "POC-01 Skia SDK platform is unsupported on this host")
endif()

set(CANVAS_SKIA_SDK_ROOT
    "${PROJECT_SOURCE_DIR}/.deps/skia-sdk/${CANVAS_POC01_SDK_TARGET}" CACHE PATH
    "Verified Canvas Skia SDK root; run tools/skia/fetch.py when missing")
set(CANVAS_SKIA_SDK_VARIANT "release" CACHE STRING
    "Explicit Skia SDK variant: release, debug, or asan")
set_property(CACHE CANVAS_SKIA_SDK_VARIANT PROPERTY STRINGS release debug asan)
if(NOT CANVAS_SKIA_SDK_VARIANT MATCHES "^(release|debug|asan)$")
  message(FATAL_ERROR "CANVAS_SKIA_SDK_VARIANT must be release, debug, or asan")
endif()
set(CANVAS_SKIA_SDK_LOCK "" CACHE FILEPATH
    "Consumer lock for the selected Skia SDK profile")

if(NOT CANVAS_SKIA_SDK_ROOT OR
   NOT EXISTS "${CANVAS_SKIA_SDK_ROOT}/lib/cmake/CanvasSkia/CanvasSkiaConfig.cmake")
  if(EXISTS "${CANVAS_SKIA_SDK_ROOT}/${CANVAS_SKIA_SDK_VARIANT}/lib/cmake/CanvasSkia/CanvasSkiaConfig.cmake")
    set(CANVAS_SKIA_SDK_ROOT
        "${CANVAS_SKIA_SDK_ROOT}/${CANVAS_SKIA_SDK_VARIANT}" CACHE PATH
        "Verified Canvas Skia SDK root" FORCE)
  endif()
endif()
if(NOT CANVAS_SKIA_SDK_ROOT OR
   NOT EXISTS "${CANVAS_SKIA_SDK_ROOT}/lib/cmake/CanvasSkia/CanvasSkiaConfig.cmake")
  message(FATAL_ERROR
    "Missing verified Canvas Skia SDK. Run: python3 tools/skia/fetch.py --target "
    "${CANVAS_POC01_SDK_TARGET}")
endif()

set(_canvas_skia_manifest "${CANVAS_SKIA_SDK_ROOT}/manifest.json")
if(NOT EXISTS "${_canvas_skia_manifest}")
  message(FATAL_ERROR "Skia SDK manifest is missing; run the fetch command above")
endif()
file(READ "${_canvas_skia_manifest}" _canvas_skia_manifest_json)
string(JSON _canvas_skia_manifest_id ERROR_VARIABLE _canvas_skia_manifest_error
  GET "${_canvas_skia_manifest_json}" sdk_id)
string(JSON _canvas_skia_manifest_target ERROR_VARIABLE _canvas_skia_target_error
  GET "${_canvas_skia_manifest_json}" identity target)
string(JSON _canvas_skia_manifest_profile ERROR_VARIABLE _canvas_skia_profile_error
  GET "${_canvas_skia_manifest_json}" identity profile)
string(JSON _canvas_skia_manifest_variant ERROR_VARIABLE _canvas_skia_variant_error
  GET "${_canvas_skia_manifest_json}" identity variant)
if(NOT CANVAS_SKIA_SDK_LOCK)
  if(NOT _canvas_skia_profile_error AND
     _canvas_skia_manifest_profile STREQUAL "r1-full-v1" AND
     EXISTS "${PROJECT_SOURCE_DIR}/r1-full-skia-sdk.lock.json")
    set(CANVAS_SKIA_SDK_LOCK "${PROJECT_SOURCE_DIR}/r1-full-skia-sdk.lock.json"
        CACHE FILEPATH "Consumer lock for the selected Skia SDK profile" FORCE)
  else()
    set(CANVAS_SKIA_SDK_LOCK "${PROJECT_SOURCE_DIR}/skia-sdk.lock.json"
        CACHE FILEPATH "Consumer lock for the selected Skia SDK profile" FORCE)
  endif()
endif()
set(CANVAS_SKIA_SDK_EXPECTED_ID "" CACHE STRING
    "Producer-only expected SDK ID; normal consumers must leave this empty")
if(NOT CANVAS_SKIA_SDK_EXPECTED_ID)
  set(_canvas_skia_lock "${CANVAS_SKIA_SDK_LOCK}")
  if(NOT EXISTS "${_canvas_skia_lock}")
    message(FATAL_ERROR "Missing skia-sdk.lock.json; run the fetch command above")
  endif()
  file(READ "${_canvas_skia_lock}" _canvas_skia_lock_json)
  string(JSON CANVAS_SKIA_SDK_EXPECTED_ID ERROR_VARIABLE _canvas_skia_lock_error
    GET "${_canvas_skia_lock_json}" targets "${CANVAS_POC01_SDK_TARGET}"
    variants "${CANVAS_SKIA_SDK_VARIANT}" sdk_id)
  if(_canvas_skia_lock_error)
    if(NOT CANVAS_SKIA_SDK_VARIANT STREQUAL "release")
      message(FATAL_ERROR
        "the selected Skia lock does not contain ${CANVAS_POC01_SDK_TARGET}/"
        "${CANVAS_SKIA_SDK_VARIANT}")
    endif()
    string(JSON CANVAS_SKIA_SDK_EXPECTED_ID ERROR_VARIABLE _canvas_skia_lock_error
      GET "${_canvas_skia_lock_json}" targets "${CANVAS_POC01_SDK_TARGET}" sdk_id)
    if(_canvas_skia_lock_error)
      message(FATAL_ERROR
        "the selected Skia lock does not contain ${CANVAS_POC01_SDK_TARGET}")
    endif()
  endif()
endif()
if(_canvas_skia_manifest_error OR _canvas_skia_target_error OR
   (NOT _canvas_skia_variant_error AND
    NOT _canvas_skia_manifest_variant STREQUAL CANVAS_SKIA_SDK_VARIANT) OR
   NOT _canvas_skia_manifest_target STREQUAL CANVAS_POC01_SDK_TARGET OR
   NOT _canvas_skia_manifest_id STREQUAL CANVAS_SKIA_SDK_EXPECTED_ID)
  message(FATAL_ERROR
    "Skia SDK manifest does not match skia-sdk.lock.json for "
    "${CANVAS_POC01_SDK_TARGET}; run tools/skia/fetch.py again")
endif()
find_package(CanvasSkia CONFIG REQUIRED
  PATHS "${CANVAS_SKIA_SDK_ROOT}/lib/cmake/CanvasSkia"
  NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
if(NOT TARGET CanvasSkia::Skia)
  message(FATAL_ERROR "Canvas Skia SDK did not define CanvasSkia::Skia")
endif()
if(NOT EXISTS "${CanvasSkia_FONT_PATH}")
  message(FATAL_ERROR "Canvas Skia SDK font fixture is missing: ${CanvasSkia_FONT_PATH}")
endif()
set(CANVAS_POC01_SKIA_FONT_PATH "${CanvasSkia_FONT_PATH}" CACHE INTERNAL
    "Verified Roboto fixture path exported by CanvasSkia")

add_library(canvas_poc01_skia STATIC
  skia/skia_scene_renderer.cpp
)
target_include_directories(canvas_poc01_skia
  PUBLIC skia
  PRIVATE ${POC01_ROOT}/src
)
target_link_libraries(canvas_poc01_skia
  PUBLIC canvas_poc01_core CanvasSkia::Skia
)

if(NOT EMSCRIPTEN AND NOT ANDROID AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
  add_executable(canvas_poc01_golden skia/skia_golden.cpp)
  target_include_directories(canvas_poc01_golden PRIVATE
    skia ${POC01_ROOT}/src
  )
  target_link_libraries(canvas_poc01_golden PRIVATE canvas_poc01_skia)
  target_compile_definitions(canvas_poc01_golden PRIVATE
    CANVAS_POC01_FIXTURE_DIR="${POC01_ROOT}/fixtures"
    CANVAS_POC01_FONT_PATH="${CANVAS_POC01_SKIA_FONT_PATH}"
  )
endif()

if(EMSCRIPTEN)
  add_subdirectory(web)
elseif(WIN32)
  add_subdirectory(windows)
elseif(ANDROID)
  add_subdirectory(android)
elseif(APPLE)
  enable_language(OBJCXX)
  add_subdirectory(apple)
endif()
