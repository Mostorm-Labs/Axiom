cmake_minimum_required(VERSION 3.30)
project(canvas_skia_sdk_probe LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(CanvasSkia CONFIG REQUIRED)

if(NOT EXISTS "${CanvasSkia_FONT_PATH}")
  message(FATAL_ERROR "CanvasSkia package did not expose its font fixture")
endif()
if(NOT EXISTS "${CanvasSkia_MANIFEST_PATH}")
  message(FATAL_ERROR "CanvasSkia package did not expose its manifest")
endif()

add_executable(canvas_skia_sdk_probe main.cpp)
target_link_libraries(canvas_skia_sdk_probe PRIVATE CanvasSkia::Skia)
