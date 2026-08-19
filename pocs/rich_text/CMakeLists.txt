cmake_minimum_required(VERSION 3.30)

project(canvas_poc04_rich_text VERSION 0.1.0 LANGUAGES C CXX)

option(CANVAS_POC04_BUILD_TESTS "Build POC-04 tests" ON)
option(CANVAS_POC04_ENABLE_SKPARAGRAPH "Build the SkParagraph layout backend" OFF)
option(CANVAS_POC04_BUILD_WEB "Build the Web/WASM IME bridge" OFF)
option(CANVAS_POC04_BUILD_WINDOWS "Build the Win32 IME adapter" OFF)
option(CANVAS_POC04_BUILD_ANDROID "Build the Android JNI adapter" OFF)
option(CANVAS_POC04_BUILD_APPLE "Build the AppKit/UIKit IME adapter" OFF)
option(CANVAS_POC04_WARNINGS_AS_ERRORS "Treat POC-04 warnings as errors" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
  # Official Skia SDK archives use the static MSVC runtime. Keep all POC-04
  # consumers, including GoogleTest and the Win32 adapter, on that ABI contract.
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

get_filename_component(CANVAS_REPOSITORY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(CANVAS_POC04_DEPS_ROOT "${CANVAS_REPOSITORY_ROOT}/.deps" CACHE PATH
    "POC-04 dependency root")

if(NOT EXISTS "${CANVAS_POC04_DEPS_ROOT}/nlohmann-json/single_include/nlohmann/json.hpp" OR
   NOT EXISTS "${CANVAS_POC04_DEPS_ROOT}/xxhash/xxhash.c")
  message(FATAL_ERROR
    "Missing POC-04 core dependencies. Run: python3 tools/bootstrap_deps.py --core")
endif()

add_library(canvas_poc04_xxhash STATIC
  "${CANVAS_POC04_DEPS_ROOT}/xxhash/xxhash.c")
target_include_directories(canvas_poc04_xxhash PUBLIC
  "${CANVAS_POC04_DEPS_ROOT}/xxhash")
set_target_properties(canvas_poc04_xxhash PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(canvas_poc04_core STATIC
  src/c_bridge.cpp
  src/font_resources.cpp
  src/foundation.cpp
  src/sha256.cpp
  src/text_document.cpp
  src/text_edit_session.cpp
  src/text_layout.cpp
  src/text_operations.cpp
)
target_include_directories(canvas_poc04_core
  PUBLIC include
  PRIVATE src "${CANVAS_POC04_DEPS_ROOT}/nlohmann-json/single_include")
target_link_libraries(canvas_poc04_core PRIVATE canvas_poc04_xxhash)
set_target_properties(canvas_poc04_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(EMSCRIPTEN)
  target_compile_options(canvas_poc04_core PRIVATE -fexceptions)
endif()

if(MSVC)
  target_compile_options(canvas_poc04_core PRIVATE /W4 /permissive- /EHsc)
  if(CANVAS_POC04_WARNINGS_AS_ERRORS)
    target_compile_options(canvas_poc04_core PRIVATE /WX)
  endif()
else()
  target_compile_options(canvas_poc04_core PRIVATE -Wall -Wextra -Wpedantic)
  if(CANVAS_POC04_WARNINGS_AS_ERRORS)
    target_compile_options(canvas_poc04_core PRIVATE -Werror)
  endif()
endif()

if(CANVAS_POC04_ENABLE_SKPARAGRAPH)
  add_subdirectory(platform/skia)
endif()

if(CANVAS_POC04_BUILD_WEB)
  add_subdirectory(platform/web)
endif()
if(CANVAS_POC04_BUILD_WINDOWS)
  add_subdirectory(platform/windows)
endif()
if(CANVAS_POC04_BUILD_ANDROID)
  add_subdirectory(platform/android)
endif()
if(CANVAS_POC04_BUILD_APPLE)
  if(NOT APPLE)
    message(FATAL_ERROR "POC-04 Apple adapter requires an Apple platform")
  endif()
  add_subdirectory(platform/apple)
endif()

if(NOT EMSCRIPTEN AND NOT ANDROID)
  add_executable(canvas_poc04_cli src/host_cli.cpp)
  target_link_libraries(canvas_poc04_cli PRIVATE canvas_poc04_core)
  target_include_directories(canvas_poc04_cli PRIVATE src)
endif()

if(NOT EMSCRIPTEN AND NOT ANDROID)
  add_executable(canvas_poc04_behavior_report tools/host_behavior_report.cpp)
  target_link_libraries(canvas_poc04_behavior_report PRIVATE canvas_poc04_core)
endif()

if(CANVAS_POC04_ENABLE_SKPARAGRAPH AND NOT EMSCRIPTEN AND NOT ANDROID)
  add_executable(canvas_poc04_canonical_behavior_report
    tools/canonical_behavior_report.cpp)
  target_link_libraries(canvas_poc04_canonical_behavior_report PRIVATE
    canvas_poc04_canonical_behavior_fixture)
  target_compile_definitions(canvas_poc04_canonical_behavior_report PRIVATE
    CANVAS_POC04_FONT_PATH="${CANVAS_POC04_FONT_PATH}"
    CANVAS_POC04_CJK_FONT_PATH="${CANVAS_POC04_CJK_FONT_PATH}")
  if(WIN32 AND CANVAS_POC04_ICU_DATA_PATH)
    add_custom_command(TARGET canvas_poc04_canonical_behavior_report POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CANVAS_POC04_ICU_DATA_PATH}"
        "$<TARGET_FILE_DIR:canvas_poc04_canonical_behavior_report>/icudtl.dat")
  endif()
endif()

include(CTest)
if(CANVAS_POC04_BUILD_TESTS AND BUILD_TESTING)
  if(NOT EXISTS "${CANVAS_POC04_DEPS_ROOT}/googletest/CMakeLists.txt")
    message(FATAL_ERROR
      "Missing GoogleTest. Run: python3 tools/bootstrap_deps.py --core")
  endif()
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  set(gtest_force_shared_crt OFF CACHE BOOL "" FORCE)
  add_subdirectory("${CANVAS_POC04_DEPS_ROOT}/googletest"
                   "${CMAKE_BINARY_DIR}/_deps/googletest" EXCLUDE_FROM_ALL)
  add_subdirectory(tests)
endif()
