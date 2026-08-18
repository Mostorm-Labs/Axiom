set(POC02_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
set(POC02_DEPS_ROOT ${PROJECT_SOURCE_DIR}/.deps CACHE PATH "POC-02 dependency root")

if(NOT EXISTS ${POC02_DEPS_ROOT}/xxhash/xxhash.c)
  message(FATAL_ERROR "Missing xxHash. Run: python3 tools/bootstrap_deps.py --core")
endif()
if(NOT EXISTS ${POC02_DEPS_ROOT}/nlohmann-json/single_include/nlohmann/json.hpp)
  message(FATAL_ERROR "Missing nlohmann/json. Run: python3 tools/bootstrap_deps.py --core")
endif()

add_library(canvas_poc02_xxhash STATIC ${POC02_DEPS_ROOT}/xxhash/xxhash.c)
target_include_directories(canvas_poc02_xxhash PUBLIC ${POC02_DEPS_ROOT}/xxhash)
set_target_properties(canvas_poc02_xxhash PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(canvas_poc02_ink STATIC
  src/document.cpp
  src/foundation.cpp
  src/frame_scheduler.cpp
  src/ink_engine.cpp
  src/input.cpp
  src/preview.cpp
  src/replay.cpp
)
target_include_directories(canvas_poc02_ink
  PUBLIC include
  PRIVATE src ${POC02_DEPS_ROOT}/nlohmann-json/single_include
)
target_link_libraries(canvas_poc02_ink PRIVATE canvas_poc02_xxhash)
set_target_properties(canvas_poc02_ink PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MSVC)
  target_compile_options(canvas_poc02_ink PRIVATE /W4 /permissive- /EHsc /fp:strict)
  if(CANVAS_POC02_WARNINGS_AS_ERRORS)
    target_compile_options(canvas_poc02_ink PRIVATE /WX)
  endif()
else()
  target_compile_options(canvas_poc02_ink PRIVATE -Wall -Wextra -Wpedantic -ffp-contract=off)
  if(EMSCRIPTEN)
    # The numeric boundary corpus intentionally throws and catches invalid
    # canonical values. Compile the owning library with WASM exception support,
    # not only the final Web adapter.
    target_compile_options(canvas_poc02_ink PRIVATE -fexceptions)
  endif()
  if(CANVAS_POC02_WARNINGS_AS_ERRORS)
    target_compile_options(canvas_poc02_ink PRIVATE -Werror)
  endif()
endif()

if(CANVAS_POC02_ENABLE_SANITIZERS)
  if(APPLE)
    target_compile_options(canvas_poc02_ink PRIVATE -fsanitize=undefined -fno-omit-frame-pointer)
    target_link_options(canvas_poc02_ink PUBLIC -fsanitize=undefined)
  elseif(NOT MSVC)
    target_compile_options(canvas_poc02_ink PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(canvas_poc02_ink PUBLIC -fsanitize=address,undefined)
  endif()
endif()

if(NOT EMSCRIPTEN AND NOT ANDROID AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
  add_executable(canvas_poc02_replay src/replay_cli.cpp)
  target_link_libraries(canvas_poc02_replay PRIVATE canvas_poc02_ink)
  target_compile_definitions(canvas_poc02_replay PRIVATE
    CANVAS_POC02_FIXTURE_DIR="${POC02_ROOT}/fixtures"
  )
endif()

if(EMSCRIPTEN AND CANVAS_POC02_WEB)
  if(NOT CANVAS_POC02_ENABLE_SKIA)
    message(FATAL_ERROR "POC-02 Web Playground requires CANVAS_POC02_ENABLE_SKIA=ON")
  endif()
endif()

if(CANVAS_POC02_ENABLE_SKIA)
  add_subdirectory(platform)
endif()

if(CANVAS_POC02_BUILD_TESTS AND BUILD_TESTING)
  if(NOT EXISTS ${POC02_DEPS_ROOT}/googletest/CMakeLists.txt)
    message(FATAL_ERROR "Missing GoogleTest. Run: python3 tools/bootstrap_deps.py --core")
  endif()
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  set(gtest_force_shared_crt OFF CACHE BOOL "" FORCE)
  add_subdirectory(${POC02_DEPS_ROOT}/googletest ${CMAKE_BINARY_DIR}/_deps/googletest EXCLUDE_FROM_ALL)
  add_subdirectory(tests)
endif()
