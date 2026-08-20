cmake_minimum_required(VERSION 3.30)

project(canvas_v2_pocs VERSION 0.1.0 LANGUAGES C CXX)

option(CANVAS_POC01_BUILD_TESTS "Build POC-01 unit and acceptance tests" ON)
option(CANVAS_POC01_ENABLE_SKIA "Build the Skia renderer and platform surfaces" OFF)
option(CANVAS_POC01_ENABLE_SANITIZERS "Enable available address/undefined behavior sanitizers" OFF)
option(CANVAS_POC01_WARNINGS_AS_ERRORS "Treat project warnings as errors" ON)
option(CANVAS_BUILD_POC01 "Build the POC-01 Shared Engine experiment" ON)
option(CANVAS_BUILD_POC02 "Build the POC-02 Ink Engine experiment" OFF)
option(CANVAS_POC02_BUILD_TESTS "Build POC-02 unit and conformance tests" ON)
option(CANVAS_POC02_ENABLE_SKIA "Build the POC-02 Skia preview adapter" OFF)
option(CANVAS_POC02_BUILD_PLATFORM_SHELLS "Build standalone POC-02 platform shells" ON)
option(CANVAS_POC02_ENABLE_SANITIZERS "Enable POC-02 address/undefined sanitizers" OFF)
option(CANVAS_POC02_WARNINGS_AS_ERRORS "Treat POC-02 warnings as errors" ON)
option(CANVAS_BUILD_POC03 "Build the POC-03 100K Scene experiment" OFF)
option(CANVAS_POC03_BUILD_TESTS "Build POC-03 unit and acceptance tests" ON)
option(CANVAS_POC03_ENABLE_SKIA "Build the POC-03 Skia compositor probe" OFF)
option(CANVAS_POC03_ENABLE_INK_INTEGRATION "Integrate the experimental POC-02 Ink contract" OFF)
option(CANVAS_POC03_ENABLE_SANITIZERS "Enable POC-03 host sanitizers" OFF)
option(CANVAS_BUILD_POC05 "Build the POC-05 Hybrid Surface experiment" OFF)
option(CANVAS_POC05_BUILD_TESTS "Build POC-05 unit and contract tests" ON)
option(CANVAS_POC05_ENABLE_SANITIZERS "Enable POC-05 host sanitizers" OFF)
option(CANVAS_POC05_ENABLE_SKIA "Render the POC-05 Windows canvas with Skia" OFF)
option(CANVAS_BUILD_ARC "Build the Arc input-to-display module" OFF)
option(CANVAS_BUILD_POC06 "Build the POC-06 Arc/FastInk experiment" OFF)

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
if(CANVAS_BUILD_POC01)
  add_subdirectory(pocs/shared_engine)
endif()
if(CANVAS_BUILD_POC02)
  add_subdirectory(pocs/ink_engine)
endif()
if(CANVAS_BUILD_POC03 OR CANVAS_BUILD_POC05)
  add_subdirectory(pocs/large_scene)
endif()
if(CANVAS_BUILD_POC05)
  add_subdirectory(pocs/hybrid_surface)
endif()
if(CANVAS_BUILD_ARC)
  add_subdirectory(arc)
endif()
if(CANVAS_BUILD_POC06)
  if(NOT CANVAS_BUILD_POC02 OR NOT CANVAS_BUILD_ARC)
    message(FATAL_ERROR "POC-06 requires CANVAS_BUILD_POC02=ON and CANVAS_BUILD_ARC=ON")
  endif()
  add_subdirectory(pocs/fastink)
endif()
