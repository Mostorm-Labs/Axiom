set(POC05_WINDOWS_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

set(CANVAS_POC05_SKIA_SDK_ROOT "${PROJECT_SOURCE_DIR}/.deps/skia-sdk/windows-x64-d3d12"
    CACHE PATH "Verified Canvas Skia SDK root for the POC-05 Windows canvas")

set(CANVAS_POC05_WEBVIEW2_SDK_ROOT "" CACHE PATH
    "Pinned Microsoft.Web.WebView2 native SDK root (1.0.2592.51)")
if(NOT CANVAS_POC05_WEBVIEW2_SDK_ROOT AND DEFINED ENV{WEBVIEW2_SDK_ROOT})
  set(CANVAS_POC05_WEBVIEW2_SDK_ROOT "$ENV{WEBVIEW2_SDK_ROOT}")
endif()
if(NOT CANVAS_POC05_WEBVIEW2_SDK_ROOT)
  # This is a discovery convenience only; CI and physical evidence commands
  # pass the cache variable explicitly so the SDK is auditable and reproducible.
  set(_poc05_default_webview2
      "$ENV{USERPROFILE}/.nuget/packages/microsoft.web.webview2/1.0.2592.51/build/native")
  if(EXISTS "${_poc05_default_webview2}/include/WebView2.h")
    set(CANVAS_POC05_WEBVIEW2_SDK_ROOT "${_poc05_default_webview2}")
  endif()
endif()

set(_poc05_webview2_include
    "${CANVAS_POC05_WEBVIEW2_SDK_ROOT}/include")
set(_poc05_webview2_loader
    "${CANVAS_POC05_WEBVIEW2_SDK_ROOT}/x64/WebView2LoaderStatic.lib")
if(NOT EXISTS "${_poc05_webview2_include}/WebView2.h" OR
   NOT EXISTS "${_poc05_webview2_loader}")
  message(WARNING
    "POC-05 Windows peer disabled: set CANVAS_POC05_WEBVIEW2_SDK_ROOT to "
    "Microsoft.Web.WebView2 1.0.2592.51/build/native")
  return()
endif()

add_library(canvas_poc05_windows STATIC
  windows_webview2_backend.cpp
  windows_rnw_fabric.cpp
  windows_d3d12_canvas.cpp
)
target_include_directories(canvas_poc05_windows
  PUBLIC
    ${POC05_WINDOWS_ROOT}
    ${POC05_ROOT}/include
  PRIVATE
    ${_poc05_webview2_include}
    ${PROJECT_SOURCE_DIR}/docs/api
)
target_compile_definitions(canvas_poc05_windows PRIVATE
  NOMINMAX WIN32_LEAN_AND_MEAN CANVAS_POC05_WEBVIEW2_SDK_VERSION="1.0.2592.51")
target_compile_options(canvas_poc05_windows PRIVATE /W4 /permissive- /WX /EHsc)
if(CANVAS_POC05_ENABLE_SKIA)
  if(NOT EXISTS
     "${CANVAS_POC05_SKIA_SDK_ROOT}/lib/cmake/CanvasSkia/CanvasSkiaConfig.cmake")
    message(FATAL_ERROR
      "POC-05 Skia is enabled but the verified Windows SDK is missing: "
      "${CANVAS_POC05_SKIA_SDK_ROOT}")
  endif()
  file(READ "${CANVAS_POC05_SKIA_SDK_ROOT}/manifest.json"
       _poc05_skia_manifest)
  file(READ "${PROJECT_SOURCE_DIR}/skia-sdk.lock.json" _poc05_skia_lock)
  string(JSON _poc05_skia_target ERROR_VARIABLE _poc05_skia_target_error
         GET "${_poc05_skia_manifest}" identity target)
  string(JSON _poc05_skia_sdk_id ERROR_VARIABLE _poc05_skia_id_error
         GET "${_poc05_skia_manifest}" sdk_id)
  string(JSON _poc05_skia_expected_id ERROR_VARIABLE _poc05_skia_lock_error
         GET "${_poc05_skia_lock}" targets windows-x64-d3d12 sdk_id)
  if(_poc05_skia_target_error OR _poc05_skia_id_error OR
     _poc05_skia_lock_error OR
     NOT _poc05_skia_target STREQUAL "windows-x64-d3d12" OR
     NOT _poc05_skia_sdk_id STREQUAL _poc05_skia_expected_id)
    message(FATAL_ERROR
      "POC-05 Skia SDK does not match skia-sdk.lock.json; run "
      "python tools/skia/fetch.py --target windows-x64-d3d12")
  endif()
  find_package(CanvasSkia CONFIG REQUIRED
    PATHS "${CANVAS_POC05_SKIA_SDK_ROOT}/lib/cmake/CanvasSkia"
    NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
  if(NOT TARGET CanvasSkia::Skia)
    message(FATAL_ERROR "Canvas Skia SDK did not define CanvasSkia::Skia")
  endif()
  target_sources(canvas_poc05_windows PRIVATE
    ${PROJECT_SOURCE_DIR}/pocs/large_scene/platform/skia/skia_large_scene_renderer.cpp)
  target_include_directories(canvas_poc05_windows PRIVATE
    ${PROJECT_SOURCE_DIR}/pocs/large_scene/include
    ${PROJECT_SOURCE_DIR}/pocs/large_scene/platform/skia)
  target_compile_definitions(canvas_poc05_windows PRIVATE
    CANVAS_POC05_HAS_SKIA=1 CANVAS_POC05_HAS_POC03_SCENE=1)
  target_link_libraries(canvas_poc05_windows PUBLIC
    CanvasSkia::Skia canvas_poc03_core)
endif()
target_link_libraries(canvas_poc05_windows PUBLIC
  canvas_poc05_core
  "${_poc05_webview2_loader}"
  ole32 oleaut32 user32 shcore version)

add_executable(canvas_poc05_windows_runner windows_runner.cpp)
target_include_directories(canvas_poc05_windows_runner PRIVATE
  ${POC05_WINDOWS_ROOT} ${POC05_ROOT}/include ${PROJECT_SOURCE_DIR}/docs/api)
target_compile_definitions(canvas_poc05_windows_runner PRIVATE
  NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(canvas_poc05_windows_runner PRIVATE /W4 /permissive- /WX /EHsc)
target_link_libraries(canvas_poc05_windows_runner PRIVATE
  canvas_poc05_windows d3d12 d3dcompiler dxgi dxguid ole32 oleaut32 user32
  gdi32 shcore version)

set_target_properties(canvas_poc05_windows_runner PROPERTIES WIN32_EXECUTABLE TRUE)
