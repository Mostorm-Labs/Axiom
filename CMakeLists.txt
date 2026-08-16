cmake_minimum_required(VERSION 3.30)

project(canvas_v2_pocs VERSION 0.1.0 LANGUAGES C CXX)

option(CANVAS_POC01_BUILD_TESTS "Build POC-01 unit and acceptance tests" ON)
option(CANVAS_POC01_ENABLE_SKIA "Build the Skia renderer and platform surfaces" OFF)
option(CANVAS_POC01_ENABLE_SANITIZERS "Enable available address/undefined behavior sanitizers" OFF)
option(CANVAS_POC01_WARNINGS_AS_ERRORS "Treat project warnings as errors" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CTest)
add_subdirectory(pocs/shared_engine)
