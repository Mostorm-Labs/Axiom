if(EMSCRIPTEN)
  set(CANVAS_POC03_SDK_TARGET "web-wasm-webgl2")
elseif(WIN32)
  set(CANVAS_POC03_SDK_TARGET "windows-x64-d3d12")
elseif(ANDROID)
  if(CMAKE_ANDROID_ARCH_ABI STREQUAL "arm64-v8a")
    set(CANVAS_POC03_SDK_TARGET "android-arm64-v8a-gles3")
  elseif(CMAKE_ANDROID_ARCH_ABI STREQUAL "x86_64")
    set(CANVAS_POC03_SDK_TARGET "android-x86_64-gles3")
  else()
    message(FATAL_ERROR "POC-03 Android supports arm64-v8a and x86_64")
  endif()
elseif(APPLE)
  if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(CANVAS_POC03_SDK_TARGET "ios-arm64-metal")
  else()
    set(CANVAS_POC03_SDK_TARGET "macos-arm64-metal")
  endif()
else()
  message(FATAL_ERROR "POC-03 Skia probe platform is unsupported")
endif()

set(CANVAS_SKIA_SDK_ROOT
    "${PROJECT_SOURCE_DIR}/.deps/skia-sdk/${CANVAS_POC03_SDK_TARGET}" CACHE PATH
    "Verified Canvas Skia SDK root for this POC-03 worktree")
if(NOT EXISTS
   "${CANVAS_SKIA_SDK_ROOT}/lib/cmake/CanvasSkia/CanvasSkiaConfig.cmake")
  message(FATAL_ERROR
    "Missing verified Canvas Skia SDK. Run: python3 tools/skia/fetch.py --target ${CANVAS_POC03_SDK_TARGET}")
endif()

file(READ "${CANVAS_SKIA_SDK_ROOT}/manifest.json" _poc03_sdk_manifest)
string(JSON _poc03_manifest_target ERROR_VARIABLE _poc03_manifest_error
       GET "${_poc03_sdk_manifest}" identity target)
if(_poc03_manifest_error OR
   NOT _poc03_manifest_target STREQUAL CANVAS_POC03_SDK_TARGET)
  message(FATAL_ERROR "POC-03 Skia SDK manifest target does not match this build")
endif()

find_package(CanvasSkia CONFIG REQUIRED
  PATHS "${CANVAS_SKIA_SDK_ROOT}/lib/cmake/CanvasSkia"
  NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
if(NOT TARGET CanvasSkia::Skia)
  message(FATAL_ERROR "Canvas Skia SDK did not define CanvasSkia::Skia")
endif()

add_library(canvas_poc03_skia STATIC
  skia_large_scene_renderer.cpp)
target_include_directories(canvas_poc03_skia PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(canvas_poc03_skia
  PUBLIC canvas_poc03_core CanvasSkia::Skia)
if(CANVAS_POC03_ENABLE_INK_INTEGRATION)
  if(NOT TARGET canvas_poc02_skia)
    message(FATAL_ERROR
      "POC-03 Skia Ink integration requires CANVAS_POC02_ENABLE_SKIA=ON")
  endif()
  target_link_libraries(canvas_poc03_skia PUBLIC
    canvas_poc03_ink_integration canvas_poc02_skia)
endif()

if(ANDROID)
  add_subdirectory(../android ${CMAKE_CURRENT_BINARY_DIR}/android)
elseif(EMSCRIPTEN)
  add_executable(canvas_poc03_web_probe web_probe.cpp)
  target_link_libraries(canvas_poc03_web_probe PRIVATE canvas_poc03_skia)
  target_compile_options(canvas_poc03_web_probe PRIVATE -fexceptions)
  set_target_properties(canvas_poc03_web_probe PROPERTIES SUFFIX ".js")
  target_link_options(canvas_poc03_web_probe PRIVATE
    "-fexceptions"
    "-sDISABLE_EXCEPTION_CATCHING=0"
    "--no-entry"
    "-sALLOW_MEMORY_GROWTH=1"
    "-sENVIRONMENT=web"
    "-sFILESYSTEM=0"
    "-sMODULARIZE=1"
    "-sEXPORT_NAME=createCanvasPoc03Module"
    "-sEXPORT_ES6=1"
    "-sNO_EXIT_RUNTIME=1"
    "-sMIN_WEBGL_VERSION=2"
    "-sMAX_WEBGL_VERSION=2"
    "-sGL_SUPPORT_AUTOMATIC_ENABLE_EXTENSIONS=0"
    "-sEXPORTED_FUNCTIONS=['_malloc','_free','_canvas_poc03_web_run','_canvas_poc03_web_prepare','_canvas_poc03_web_prepare_scale','_canvas_poc03_web_render_frame','_canvas_poc03_web_finish','_canvas_poc03_web_reset','_canvas_poc03_web_ink_begin','_canvas_poc03_web_ink_push','_canvas_poc03_web_ink_visible','_canvas_poc03_web_ink_cancel','_canvas_poc03_web_transform','_canvas_poc03_web_select_begin','_canvas_poc03_web_select_move','_canvas_poc03_web_select_end']"
    "-sEXPORTED_RUNTIME_METHODS=['UTF8ToString','HEAPU8','HEAPU32','HEAPF32']")
elseif(WIN32)
  add_executable(canvas_poc03_windows_probe windows_probe.cpp)
  target_compile_definitions(canvas_poc03_windows_probe PRIVATE
    NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0602 WINVER=0x0602)
  target_link_libraries(canvas_poc03_windows_probe PRIVATE
    canvas_poc03_skia d3d12 dxgi)
elseif(APPLE AND CMAKE_SYSTEM_NAME STREQUAL "iOS")
  add_subdirectory(../apple ${CMAKE_CURRENT_BINARY_DIR}/apple)
else()
  add_executable(canvas_poc03_skia_raster_probe raster_probe.cpp)
  target_link_libraries(canvas_poc03_skia_raster_probe PRIVATE canvas_poc03_skia)
endif()
