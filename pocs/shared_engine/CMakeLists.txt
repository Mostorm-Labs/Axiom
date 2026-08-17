set(POC01_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
set(POC01_DEPS_ROOT ${PROJECT_SOURCE_DIR}/.deps CACHE PATH "POC-01 dependency root")

if(NOT EXISTS ${POC01_DEPS_ROOT}/nlohmann-json/single_include/nlohmann/json.hpp)
  message(FATAL_ERROR "Missing nlohmann/json. Run: python3 tools/bootstrap_deps.py --core")
endif()
if(NOT EXISTS ${POC01_DEPS_ROOT}/xxhash/xxhash.c)
  message(FATAL_ERROR "Missing xxHash. Run: python3 tools/bootstrap_deps.py --core")
endif()

add_library(canvas_poc01_xxhash STATIC ${POC01_DEPS_ROOT}/xxhash/xxhash.c)
target_include_directories(canvas_poc01_xxhash PUBLIC ${POC01_DEPS_ROOT}/xxhash)
set_target_properties(canvas_poc01_xxhash PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(canvas_poc01_core STATIC
  src/c_bridge.cpp
  src/conformance.cpp
  src/document.cpp
  src/foundation.cpp
  src/operations.cpp
  src/runtime.cpp
  src/scene_compiler.cpp
  src/software_probe_renderer.cpp
)
target_include_directories(canvas_poc01_core
  PUBLIC include
  PRIVATE src ${POC01_DEPS_ROOT}/nlohmann-json/single_include
)
target_link_libraries(canvas_poc01_core PRIVATE canvas_poc01_xxhash)
set_target_properties(canvas_poc01_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(EMSCRIPTEN)
  # Operation parsing converts schema/JSON exceptions into ABI status codes.
  # Keep catching enabled inside the Runtime; exceptions never cross the ABI.
  target_compile_options(canvas_poc01_core PRIVATE -fexceptions)
endif()

if(MSVC)
  target_compile_options(canvas_poc01_core PRIVATE /W4 /permissive- /EHsc)
  if(CANVAS_POC01_WARNINGS_AS_ERRORS)
    target_compile_options(canvas_poc01_core PRIVATE /WX)
  endif()
else()
  target_compile_options(canvas_poc01_core PRIVATE -Wall -Wextra -Wpedantic)
  if(CANVAS_POC01_WARNINGS_AS_ERRORS)
    target_compile_options(canvas_poc01_core PRIVATE -Werror)
  endif()
endif()

if(CANVAS_POC01_ENABLE_SANITIZERS)
  if(APPLE)
    # Xcode 26's host ASan runtime hangs before main on some desktop hosts;
    # retain UBSan locally and exercise ASan in the Windows Clang matrix.
    target_compile_options(canvas_poc01_core PRIVATE -fsanitize=undefined -fno-omit-frame-pointer)
    target_link_options(canvas_poc01_core PUBLIC -fsanitize=undefined)
  elseif(MSVC AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # The top-level project instruments every target and links compiler-rt so
    # MSVC STL annotation metadata remains consistent across test libraries.
  elseif(NOT MSVC)
    target_compile_options(canvas_poc01_core PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(canvas_poc01_core PUBLIC -fsanitize=address,undefined)
  endif()
endif()

if(NOT EMSCRIPTEN AND NOT ANDROID AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
  add_executable(canvas_poc01_cli src/host_cli.cpp)
  target_link_libraries(canvas_poc01_cli PRIVATE canvas_poc01_core)
  target_include_directories(canvas_poc01_cli PRIVATE src)
  target_compile_definitions(canvas_poc01_cli PRIVATE
    CANVAS_POC01_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
  )
endif()

if(CANVAS_POC01_ENABLE_SKIA)
  add_subdirectory(platform)
endif()

if(CANVAS_POC01_BUILD_TESTS AND BUILD_TESTING)
  if(NOT EXISTS ${POC01_DEPS_ROOT}/googletest/CMakeLists.txt)
    message(FATAL_ERROR "Missing GoogleTest. Run: python3 tools/bootstrap_deps.py --core")
  endif()
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  set(gtest_force_shared_crt OFF CACHE BOOL "" FORCE)
  add_subdirectory(${POC01_DEPS_ROOT}/googletest ${CMAKE_BINARY_DIR}/_deps/googletest EXCLUDE_FROM_ALL)
  add_subdirectory(tests)
endif()
