cmake_minimum_required(VERSION 3.30)
project(canvas_skia_sdk_probe LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(CanvasSkia CONFIG REQUIRED)

option(CANVAS_SKIA_PROBE_SKSG "Compile and link the RF-01 SkSG SDK payload" OFF)

if(NOT EXISTS "${CanvasSkia_FONT_PATH}")
  message(FATAL_ERROR "CanvasSkia package did not expose its font fixture")
endif()
if(NOT EXISTS "${CanvasSkia_MANIFEST_PATH}")
  message(FATAL_ERROR "CanvasSkia package did not expose its manifest")
endif()

set(_canvas_skia_probe_source main.cpp)
if(CANVAS_SKIA_PROBE_SKSG)
  set(_canvas_skia_probe_source sksg_main.cpp)
endif()
add_executable(canvas_skia_sdk_probe ${_canvas_skia_probe_source})
target_link_libraries(canvas_skia_sdk_probe PRIVATE CanvasSkia::Skia)
if(MSVC)
  # The official Windows SDK is built with Skia's static MSVC CRT contract.
  set_property(TARGET canvas_skia_sdk_probe PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
  set_target_properties(canvas_skia_sdk_probe PROPERTIES
    XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "dev.mostorm.canvas.skiasdkprobe"
  )
endif()
