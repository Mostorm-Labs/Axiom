set(CANVAS_POC01_SKIA_ROOT "${PROJECT_SOURCE_DIR}/.deps/skia" CACHE PATH
    "Pinned Skia source directory")

if(EMSCRIPTEN)
  set(_poc01_skia_default "${CANVAS_POC01_SKIA_ROOT}/out/poc01-web/libskia.wasm.a")
elseif(WIN32)
  set(_poc01_skia_default "${CANVAS_POC01_SKIA_ROOT}/out/poc01-windows/skia.lib")
elseif(ANDROID)
  if(CMAKE_ANDROID_ARCH_ABI STREQUAL "x86_64")
    set(_poc01_android_skia_cpu "x64")
  elseif(CMAKE_ANDROID_ARCH_ABI STREQUAL "arm64-v8a")
    set(_poc01_android_skia_cpu "arm64")
  else()
    message(FATAL_ERROR "POC-01 Android supports only x86_64 and arm64-v8a")
  endif()
  set(_poc01_skia_default
      "${CANVAS_POC01_SKIA_ROOT}/out/poc01-android-${_poc01_android_skia_cpu}/libskia.a")
elseif(APPLE AND CMAKE_SYSTEM_NAME STREQUAL "iOS")
  if(CMAKE_OSX_SYSROOT MATCHES "simulator")
    set(_poc01_skia_default "${CANVAS_POC01_SKIA_ROOT}/out/poc01-ios-simulator/libskia.a")
  else()
    set(_poc01_skia_default "${CANVAS_POC01_SKIA_ROOT}/out/poc01-ios/libskia.a")
  endif()
elseif(APPLE)
  set(_poc01_skia_default "${CANVAS_POC01_SKIA_ROOT}/out/poc01-macos/libskia.a")
else()
  message(FATAL_ERROR "POC-01 Skia platform is unsupported on this host")
endif()

set(CANVAS_POC01_SKIA_LIBRARY "${_poc01_skia_default}" CACHE FILEPATH
    "Pinned Skia static library")
get_filename_component(_poc01_skia_output_dir
  "${CANVAS_POC01_SKIA_LIBRARY}" DIRECTORY)
if(WIN32)
  set(_poc01_skia_transitive
    "${_poc01_skia_output_dir}/skcms.lib"
    "${_poc01_skia_output_dir}/freetype2.lib"
    "${_poc01_skia_output_dir}/zlib.lib"
  )
elseif(EMSCRIPTEN)
  set(_poc01_skia_transitive
    "${_poc01_skia_output_dir}/libskcms.wasm.a"
    "${_poc01_skia_output_dir}/libfreetype2.wasm.a"
    "${_poc01_skia_output_dir}/libpng.wasm.a"
    "${_poc01_skia_output_dir}/libzlib.wasm.a"
  )
else()
  set(_poc01_skia_transitive
    "${_poc01_skia_output_dir}/libskcms.a"
    "${_poc01_skia_output_dir}/libfreetype2.a"
    "${_poc01_skia_output_dir}/libpng.a"
    "${_poc01_skia_output_dir}/libzlib.a"
  )
endif()
if(NOT EXISTS "${CANVAS_POC01_SKIA_ROOT}/include/core/SkCanvas.h")
  message(FATAL_ERROR "Missing pinned Skia checkout. Run tools/bootstrap_deps.py --skia --sync-skia")
endif()
if(NOT EXISTS "${CANVAS_POC01_SKIA_LIBRARY}")
  message(FATAL_ERROR "Missing Skia library: ${CANVAS_POC01_SKIA_LIBRARY}. Run tools/build_skia.py for this target")
endif()
foreach(_poc01_dependency IN LISTS _poc01_skia_transitive)
  if(NOT EXISTS "${_poc01_dependency}")
    message(FATAL_ERROR "Missing Skia dependency: ${_poc01_dependency}")
  endif()
endforeach()

add_library(canvas_poc01_skia STATIC
  skia/skia_scene_renderer.cpp
)
target_include_directories(canvas_poc01_skia
  PUBLIC skia ${CANVAS_POC01_SKIA_ROOT}
  PRIVATE ${POC01_ROOT}/src
)
target_link_libraries(canvas_poc01_skia
  PUBLIC canvas_poc01_core "${CANVAS_POC01_SKIA_LIBRARY}"
         ${_poc01_skia_transitive}
)
if(APPLE)
  find_library(COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
  find_library(COREGRAPHICS_FRAMEWORK CoreGraphics REQUIRED)
  find_library(CORETEXT_FRAMEWORK CoreText REQUIRED)
  target_link_libraries(canvas_poc01_skia PUBLIC
    ${COREFOUNDATION_FRAMEWORK}
    ${COREGRAPHICS_FRAMEWORK}
    ${CORETEXT_FRAMEWORK}
  )
endif()

if(NOT EMSCRIPTEN AND NOT ANDROID AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
  add_executable(canvas_poc01_golden skia/skia_golden.cpp)
  target_include_directories(canvas_poc01_golden PRIVATE
    skia ${POC01_ROOT}/src ${CANVAS_POC01_SKIA_ROOT}
  )
  target_link_libraries(canvas_poc01_golden PRIVATE canvas_poc01_skia)
  target_compile_definitions(canvas_poc01_golden PRIVATE
    CANVAS_POC01_FIXTURE_DIR="${POC01_ROOT}/fixtures"
    CANVAS_POC01_FONT_PATH="${CANVAS_POC01_SKIA_ROOT}/resources/fonts/Roboto-Regular.ttf"
  )
endif()

if(EMSCRIPTEN)
  add_subdirectory(web)
elseif(WIN32)
  add_subdirectory(windows)
elseif(ANDROID)
  add_subdirectory(android)
elseif(APPLE)
  enable_language(OBJCXX)
  add_subdirectory(apple)
endif()
