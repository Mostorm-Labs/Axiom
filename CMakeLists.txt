cmake_minimum_required(VERSION 3.21)

if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

project(canvas VERSION 0.1.0 LANGUAGES CXX)

if(APPLE)
    enable_language(OBJCXX)
endif()

if(WIN32)
    # Keep the Win32 min/max macros out of every first-party translation unit.
    # This directory-level policy also reaches app/windows and tests, including
    # targets that include Windows headers without linking windows_platform.
    add_compile_definitions(NOMINMAX)
endif()

find_package(unofficial-skia CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

include(CTest)
include(cmake/CompilerWarnings.cmake)

add_library(canvas_core STATIC src/core/version.cpp src/document/document.cpp
            src/app/embedded_load_tracker.cpp
            src/storage/document_codec.cpp
            src/stroke/stroke_builder.cpp src/input/input_router.cpp
            src/render/skia_renderer.cpp src/embed/embedded_surface_manager.cpp)
add_library(canvas::core ALIAS canvas_core)
target_include_directories(canvas_core PUBLIC include)
target_link_libraries(canvas_core PUBLIC unofficial::skia::skia
                       PRIVATE nlohmann_json::nlohmann_json)
canvas_enable_warnings(canvas_core)

add_library(canvas_ipc STATIC src/ipc/protocol.cpp src/ipc/client_session.cpp)
add_library(canvas::ipc ALIAS canvas_ipc)
target_include_directories(canvas_ipc PUBLIC include)
target_link_libraries(canvas_ipc PUBLIC nlohmann_json::nlohmann_json)
canvas_enable_warnings(canvas_ipc)

if(WIN32)
    find_program(CANVAS_NODE_EXECUTABLE NAMES node.exe node)
    if(NOT CANVAS_NODE_EXECUTABLE)
        message(FATAL_ERROR
            "Node.js is required to build Canvas embedded web assets.")
    endif()
    execute_process(
        COMMAND "${CANVAS_NODE_EXECUTABLE}" --version
        RESULT_VARIABLE _canvas_node_version_result
        OUTPUT_VARIABLE _canvas_node_version
        ERROR_VARIABLE _canvas_node_version_error
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT _canvas_node_version_result EQUAL 0)
        message(FATAL_ERROR
            "Unable to execute Node.js: ${_canvas_node_version_error}")
    endif()
    if(NOT _canvas_node_version MATCHES "^v([0-9]+)\\.([0-9]+)\\.")
        message(FATAL_ERROR
            "Unable to parse Node.js version: ${_canvas_node_version}")
    endif()
    set(_canvas_node_major "${CMAKE_MATCH_1}")
    set(_canvas_node_minor "${CMAKE_MATCH_2}")
    if(_canvas_node_major LESS 22 OR
       (_canvas_node_major EQUAL 22 AND _canvas_node_minor LESS 12))
        message(FATAL_ERROR
            "Canvas web assets require Node.js 22.12 or newer; found ${_canvas_node_version}.")
    endif()
    find_program(CANVAS_NPM_EXECUTABLE NAMES npm.cmd npm)
    if(NOT CANVAS_NPM_EXECUTABLE)
        message(FATAL_ERROR
            "npm is required to build Canvas embedded web assets. Install npm with Node.js.")
    endif()

    set(_canvas_web_root "${CMAKE_SOURCE_DIR}/web")
    set(_canvas_web_dist "${_canvas_web_root}/dist")
    set(_canvas_web_stamp "${_canvas_web_dist}/.canvas-assets.stamp")
    file(GLOB_RECURSE _canvas_web_sources CONFIGURE_DEPENDS
         "${_canvas_web_root}/src/*.ts"
         "${_canvas_web_root}/src/*.css")
    set(_canvas_web_inputs
        "${_canvas_web_root}/package.json"
        "${_canvas_web_root}/package-lock.json"
        "${_canvas_web_root}/tsconfig.json"
        "${_canvas_web_root}/vite.config.ts"
        "${_canvas_web_root}/richtext.html"
        "${_canvas_web_root}/video.html"
        ${_canvas_web_sources})
    add_custom_command(
        OUTPUT "${_canvas_web_stamp}"
        COMMAND "${CANVAS_NPM_EXECUTABLE}" ci
        COMMAND "${CANVAS_NPM_EXECUTABLE}" run build
        COMMAND "${CMAKE_COMMAND}" -E touch "${_canvas_web_stamp}"
        WORKING_DIRECTORY "${_canvas_web_root}"
        DEPENDS ${_canvas_web_inputs}
        BYPRODUCTS
            "${_canvas_web_dist}/richtext.html"
            "${_canvas_web_dist}/video.html"
        COMMENT "Building locked Canvas embedded web assets"
        VERBATIM)
    add_custom_target(canvas_web_assets ALL DEPENDS "${_canvas_web_stamp}")

    include(cmake/WebView2.cmake)
    find_package(spdlog CONFIG REQUIRED)
    add_library(canvas_windows_platform STATIC
                src/platform/windows/dcomp_host.cpp
                src/platform/windows/webview2_surface.cpp
                src/platform/windows/win_pointer_adapter.cpp
                src/platform/windows/qpc_clock.cpp
                src/platform/windows/skia_d3d12_context.cpp
                src/platform/windows/skia_swap_chain_layer.cpp
                src/platform/windows/document_store.cpp
                src/platform/windows/named_pipe_server.cpp)
    add_library(canvas::windows_platform ALIAS canvas_windows_platform)
    target_include_directories(canvas_windows_platform PUBLIC
                               ${PROJECT_SOURCE_DIR}/include
                               ${PROJECT_SOURCE_DIR}/src)
    target_link_libraries(canvas_windows_platform PUBLIC dcomp dxgi d3d12 advapi32
                                                         user32 ole32
                                                         unofficial::skia::skia
                                                         canvas::core
                                                         canvas::ipc
                                                         PRIVATE
                                                         canvas::webview2_loader
                                                         d3dcompiler
                                                         shlwapi version
                                                         urlmon oleaut32
                                                         spdlog::spdlog)
    canvas_enable_warnings(canvas_windows_platform)

    add_subdirectory(app/windows)
    add_dependencies(canvas_windows canvas_web_assets)
    # A POST_BUILD command is sufficient when the executable relinks, but a
    # web-only edit can rebuild canvas_web_assets while leaving the executable
    # up to date. This per-configuration target makes the runtime copy explicit
    # in that case and keeps Debug/Release output directories independent.
    add_custom_target(canvas_web_runtime_assets ALL
        DEPENDS canvas_windows canvas_web_assets
        COMMAND "${CMAKE_COMMAND}" -E rm -rf
                "$<TARGET_FILE_DIR:canvas_windows>/web"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "${_canvas_web_dist}"
                "$<TARGET_FILE_DIR:canvas_windows>/web"
        COMMENT "Synchronizing Canvas embedded web assets for this configuration"
        VERBATIM)
endif()

if(APPLE)
    find_library(CANVAS_APPKIT_FRAMEWORK AppKit REQUIRED)
    find_library(CANVAS_METAL_FRAMEWORK Metal REQUIRED)
    find_library(CANVAS_QUARTZCORE_FRAMEWORK QuartzCore REQUIRED)

    add_library(canvas_macos_platform STATIC
                src/platform/macos/composition_view.mm
                src/platform/macos/metal_host.mm
                src/platform/macos/metal_view.mm)
    add_library(canvas::macos_platform ALIAS canvas_macos_platform)
    target_include_directories(canvas_macos_platform PUBLIC
                               ${PROJECT_SOURCE_DIR}/src)
    target_link_libraries(canvas_macos_platform PUBLIC
                          ${CANVAS_APPKIT_FRAMEWORK}
                          ${CANVAS_METAL_FRAMEWORK}
                          ${CANVAS_QUARTZCORE_FRAMEWORK})
    target_link_libraries(canvas_macos_platform PRIVATE
                          canvas::core
                          unofficial::skia::skia)
    target_compile_options(canvas_macos_platform PRIVATE -fobjc-arc)
    canvas_enable_warnings(canvas_macos_platform)

    # This target binds the shared C++/Skia renderer to CAMetalLayer. WebKit,
    # network, input, and Electron control remain separate later increments.
    add_subdirectory(app/macos)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
