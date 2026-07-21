cmake_minimum_required(VERSION 3.21)

if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

project(canvas VERSION 0.1.0 LANGUAGES CXX)

find_package(unofficial-skia CONFIG REQUIRED)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

include(CTest)
include(cmake/CompilerWarnings.cmake)

add_library(canvas_core STATIC src/core/version.cpp src/document/document.cpp
            src/stroke/stroke_builder.cpp src/input/input_router.cpp
            src/render/skia_renderer.cpp src/embed/embedded_surface_manager.cpp)
add_library(canvas::core ALIAS canvas_core)
target_include_directories(canvas_core PUBLIC include)
target_link_libraries(canvas_core PUBLIC unofficial::skia::skia)
canvas_enable_warnings(canvas_core)

if(WIN32)
    add_library(canvas_windows_platform STATIC
                src/platform/windows/dcomp_host.cpp
                src/platform/windows/win_pointer_adapter.cpp)
    add_library(canvas::windows_platform ALIAS canvas_windows_platform)
    target_include_directories(canvas_windows_platform PUBLIC
                               ${PROJECT_SOURCE_DIR}/include
                               ${PROJECT_SOURCE_DIR}/src)
    target_link_libraries(canvas_windows_platform PUBLIC dcomp dxgi d3d12
                                                         user32 ole32)
    canvas_enable_warnings(canvas_windows_platform)

    add_subdirectory(app/windows)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
