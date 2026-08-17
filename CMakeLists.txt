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

# Skia's Windows GN toolchain is intentionally built against the static MSVC
# runtime. Keep every CMake target (including tests) on the same CRT so static
# archive link contracts remain consistent under clang-cl.
if(MSVC)
  if(CANVAS_POC01_ENABLE_SANITIZERS)
    # Clang's Windows AddressSanitizer runtime cannot link against the debug
    # static CRT. Retain a Debug build with symbols, but use /MT so the
    # sanitizer check matches the release Skia archive's CRT contract.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
      message(FATAL_ERROR "POC-01 Windows sanitizers require the pinned clang-cl toolchain")
    endif()
    execute_process(
      COMMAND ${CMAKE_CXX_COMPILER} -print-resource-dir
      RESULT_VARIABLE CANVAS_POC01_CLANG_RESOURCE_STATUS
      OUTPUT_VARIABLE CANVAS_POC01_CLANG_RESOURCE_DIR
      OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    file(TO_CMAKE_PATH "${CANVAS_POC01_CLANG_RESOURCE_DIR}"
         CANVAS_POC01_CLANG_RESOURCE_DIR)
    set(CANVAS_POC01_ASAN_RUNTIME
        "${CANVAS_POC01_CLANG_RESOURCE_DIR}/lib/windows/clang_rt.asan-x86_64.lib")
    if(NOT CANVAS_POC01_CLANG_RESOURCE_STATUS EQUAL 0 OR
       NOT EXISTS "${CANVAS_POC01_ASAN_RUNTIME}")
      message(FATAL_ERROR
        "clang-cl AddressSanitizer runtime was not found: ${CANVAS_POC01_ASAN_RUNTIME}")
    endif()
    # Apply instrumentation to the Runtime, tests, GoogleTest and C helpers.
    # Mixing annotated and unannotated MSVC STL objects is a link-time error.
    add_compile_options(/fsanitize=address)
    # CMake drives lld-link directly for the MSVC ABI, so supply the static-CRT
    # ASan runtime explicitly instead of forwarding a compiler-driver flag.
    add_link_options(
      /INCREMENTAL:NO
      "/wholearchive:${CANVAS_POC01_ASAN_RUNTIME}"
    )
  else()
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  endif()
endif()

include(CTest)
add_subdirectory(pocs/shared_engine)
