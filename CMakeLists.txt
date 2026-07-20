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

find_package(GTest CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

add_library(canvas_core STATIC src/core/version.cpp)
add_library(canvas::core ALIAS canvas_core)
target_include_directories(canvas_core PUBLIC include)
target_link_libraries(canvas_core PUBLIC nlohmann_json::nlohmann_json)
canvas_enable_warnings(canvas_core)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
