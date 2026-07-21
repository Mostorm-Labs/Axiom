cmake_minimum_required(VERSION 3.21)

if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

project(canvas VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

include(CTest)
include(cmake/CompilerWarnings.cmake)

add_library(canvas_core STATIC src/core/version.cpp src/document/document.cpp
            src/stroke/stroke_builder.cpp)
add_library(canvas::core ALIAS canvas_core)
target_include_directories(canvas_core PUBLIC include)
canvas_enable_warnings(canvas_core)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
