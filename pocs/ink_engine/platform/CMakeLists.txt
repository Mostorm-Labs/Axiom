if(EMSCRIPTEN)
  set(CANVAS_POC02_SDK_TARGET "web-wasm-webgl2")
elseif(WIN32)
  set(CANVAS_POC02_SDK_TARGET "windows-x64-d3d12")
elseif(ANDROID)
  if(CMAKE_ANDROID_ARCH_ABI STREQUAL "arm64-v8a")
    set(CANVAS_POC02_SDK_TARGET "android-arm64-v8a-gles3")
  elseif(CMAKE_ANDROID_ARCH_ABI STREQUAL "x86_64")
    set(CANVAS_POC02_SDK_TARGET "android-x86_64-gles3")
  else()
    message(FATAL_ERROR "POC-02 Android supports arm64-v8a and x86_64")
  endif()
elseif(APPLE AND CMAKE_SYSTEM_NAME STREQUAL "iOS")
  if(CMAKE_OSX_SYSROOT MATCHES "simulator")
    set(CANVAS_POC02_SDK_TARGET "ios-simulator-arm64-metal")
  else()
    set(CANVAS_POC02_SDK_TARGET "ios-arm64-metal")
  endif()
elseif(APPLE)
  set(CANVAS_POC02_SDK_TARGET "macos-arm64-metal")
else()
  message(FATAL_ERROR "POC-02 Skia target is unsupported on this host")
endif()

set(CANVAS_SKIA_SDK_ROOT
    "${PROJECT_SOURCE_DIR}/.deps/skia-sdk/${CANVAS_POC02_SDK_TARGET}" CACHE PATH
    "Verified Canvas Skia SDK root")
set(_canvas_poc02_skia_config
    "${CANVAS_SKIA_SDK_ROOT}/lib/cmake/CanvasSkia/CanvasSkiaConfig.cmake")
if(NOT EXISTS "${_canvas_poc02_skia_config}")
  message(FATAL_ERROR
    "Missing verified Canvas Skia SDK. Run: python3 tools/skia/fetch.py --target "
    "${CANVAS_POC02_SDK_TARGET}")
endif()

file(READ "${CANVAS_SKIA_SDK_ROOT}/manifest.json" _canvas_poc02_manifest)
file(READ "${PROJECT_SOURCE_DIR}/skia-sdk.lock.json" _canvas_poc02_lock)
string(JSON _canvas_poc02_manifest_id GET "${_canvas_poc02_manifest}" sdk_id)
string(JSON _canvas_poc02_manifest_target GET "${_canvas_poc02_manifest}" identity target)
string(JSON _canvas_poc02_locked_id GET "${_canvas_poc02_lock}" targets
       "${CANVAS_POC02_SDK_TARGET}" sdk_id)
if(NOT _canvas_poc02_manifest_target STREQUAL CANVAS_POC02_SDK_TARGET OR
   NOT _canvas_poc02_manifest_id STREQUAL _canvas_poc02_locked_id)
  message(FATAL_ERROR
    "Canvas Skia SDK identity does not match skia-sdk.lock.json for "
    "${CANVAS_POC02_SDK_TARGET}")
endif()

find_package(CanvasSkia CONFIG REQUIRED
  PATHS "${CANVAS_SKIA_SDK_ROOT}/lib/cmake/CanvasSkia"
  NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
if(NOT TARGET CanvasSkia::Skia)
  message(FATAL_ERROR "Canvas Skia SDK did not export CanvasSkia::Skia")
endif()

add_library(canvas_poc02_skia STATIC skia/ink_skia_renderer.cpp)
target_include_directories(canvas_poc02_skia PUBLIC skia)
target_link_libraries(canvas_poc02_skia PUBLIC canvas_poc02_ink CanvasSkia::Skia)

if(CANVAS_POC02_BUILD_PLATFORM_SHELLS AND NOT EMSCRIPTEN AND NOT ANDROID AND
   NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
  add_executable(canvas_poc02_golden skia/golden_tool.cpp)
  target_link_libraries(canvas_poc02_golden PRIVATE canvas_poc02_skia)
  target_compile_definitions(canvas_poc02_golden PRIVATE
    CANVAS_POC02_FIXTURE_DIR="${POC02_ROOT}/fixtures"
    CANVAS_POC02_GOLDEN_DIR="${POC02_ROOT}/goldens"
  )
endif()

if(NOT CANVAS_POC02_BUILD_PLATFORM_SHELLS)
  # POC-03 consumes only canvas_poc02_skia and owns the combined platform
  # surface. Avoid building a second standalone shell in that configuration.
elseif(EMSCRIPTEN)
  add_subdirectory(web)
elseif(WIN32)
  add_subdirectory(windows)
elseif(ANDROID)
  add_subdirectory(android)
endif()
