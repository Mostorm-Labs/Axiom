set(POC03_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
set(POC03_DEPS_ROOT ${PROJECT_SOURCE_DIR}/.deps CACHE PATH
    "POC-03 dependency root for this worktree")

if(NOT EXISTS ${POC03_DEPS_ROOT}/xxhash/xxhash.c)
  message(FATAL_ERROR "Missing xxHash. Run: python3 tools/bootstrap_deps.py --core")
endif()

add_library(canvas_poc03_xxhash STATIC ${POC03_DEPS_ROOT}/xxhash/xxhash.c)
target_include_directories(canvas_poc03_xxhash PUBLIC ${POC03_DEPS_ROOT}/xxhash)
set_target_properties(canvas_poc03_xxhash PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(canvas_poc03_core STATIC
  src/document.cpp
  src/foundation.cpp
  src/frame_graph.cpp
  src/scene_compiler.cpp
  src/scheduler.cpp
  src/spatial_index.cpp
  src/tile_cache.cpp
  src/view_query.cpp
)
target_include_directories(canvas_poc03_core
  PUBLIC include
  PRIVATE src
)
target_link_libraries(canvas_poc03_core PRIVATE canvas_poc03_xxhash)
set_target_properties(canvas_poc03_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(EMSCRIPTEN)
  target_compile_options(canvas_poc03_core PRIVATE -fexceptions)
endif()

if(MSVC)
  target_compile_options(canvas_poc03_core PRIVATE /W4 /permissive- /WX /EHsc)
else()
  target_compile_options(canvas_poc03_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(CANVAS_POC03_ENABLE_SANITIZERS)
  if(MSVC)
    message(FATAL_ERROR "POC-03 sanitizer preset is host-only; Windows CI uses the POC-01 pinned sanitizer lane")
  elseif(APPLE)
    target_compile_options(canvas_poc03_core PRIVATE
      -fsanitize=undefined -fno-omit-frame-pointer)
    target_link_options(canvas_poc03_core PUBLIC -fsanitize=undefined)
  else()
    target_compile_options(canvas_poc03_core PRIVATE
      -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(canvas_poc03_core PUBLIC -fsanitize=address,undefined)
  endif()
endif()

add_executable(canvas_poc03_benchmark src/benchmark_cli.cpp)
target_link_libraries(canvas_poc03_benchmark PRIVATE canvas_poc03_core)
if(WIN32)
  target_link_libraries(canvas_poc03_benchmark PRIVATE psapi)
endif()
if(EMSCRIPTEN)
  target_compile_options(canvas_poc03_benchmark PRIVATE -fexceptions)
  target_link_options(canvas_poc03_benchmark PRIVATE
    "-fexceptions" "-sDISABLE_EXCEPTION_CATCHING=0")
endif()

if(CANVAS_POC03_ENABLE_SKIA)
  add_subdirectory(platform/skia)
endif()

if(CANVAS_POC03_BUILD_TESTS AND BUILD_TESTING)
  if(NOT EXISTS ${POC03_DEPS_ROOT}/googletest/CMakeLists.txt)
    message(FATAL_ERROR "Missing GoogleTest. Run: python3 tools/bootstrap_deps.py --core")
  endif()
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  set(gtest_force_shared_crt OFF CACHE BOOL "" FORCE)
  add_subdirectory(${POC03_DEPS_ROOT}/googletest
                   ${CMAKE_BINARY_DIR}/_deps/googletest EXCLUDE_FROM_ALL)
  add_subdirectory(tests)
endif()
