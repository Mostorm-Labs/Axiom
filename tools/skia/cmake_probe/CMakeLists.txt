cmake_minimum_required(VERSION 3.30)
project(canvas_skia_sdk_probe LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(CanvasSkia CONFIG REQUIRED)

if(NOT EXISTS "${CanvasSkia_FONT_PATH}")
  message(FATAL_ERROR "CanvasSkia package did not expose its font fixture")
endif()
if(NOT EXISTS "${CanvasSkia_MANIFEST_PATH}")
  message(FATAL_ERROR "CanvasSkia package did not expose its manifest")
endif()

function(canvas_add_skia_probe name source imported_target)
  if(NOT TARGET "${imported_target}")
    message(FATAL_ERROR "CanvasSkia package did not define ${imported_target}")
  endif()
  add_executable("${name}" "${source}")
  target_link_libraries("${name}" PRIVATE "${imported_target}")
  if(MSVC)
    if(CanvasSkia_VARIANT STREQUAL "asan")
      set_property(TARGET "${name}" PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
      target_compile_options("${name}" PRIVATE /fsanitize=address)
      target_link_options("${name}" PRIVATE /fsanitize=address /INCREMENTAL:NO)
    elseif(CanvasSkia_VARIANT STREQUAL "debug")
      # Skia's GN debug archive uses the static release CRT and disables MSVC
      # iterator debugging even though Skia assertions/debug symbols remain on.
      set_property(TARGET "${name}" PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
    else()
      set_property(TARGET "${name}" PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
    endif()
  elseif(CanvasSkia_VARIANT STREQUAL "asan")
    target_compile_options("${name}" PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options("${name}" PRIVATE -fsanitize=address)
  endif()
  if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set_target_properties("${name}" PROPERTIES
      XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
      XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "dev.mostorm.canvas.${name}"
    )
  endif()
endfunction()

canvas_add_skia_probe(canvas_skia_sdk_probe main.cpp CanvasSkia::Skia)

if(DEFINED CanvasSkia_CAPABILITIES)
  canvas_add_skia_probe(canvas_skia_paragraph_probe probe_paragraph.cpp CanvasSkia::Paragraph)
  canvas_add_skia_probe(canvas_skia_skottie_probe probe_skottie.cpp CanvasSkia::Skottie)
  canvas_add_skia_probe(canvas_skia_svg_probe probe_svg.cpp CanvasSkia::Svg)
  canvas_add_skia_probe(canvas_skia_pathops_probe probe_pathops.cpp CanvasSkia::PathOps)
  canvas_add_skia_probe(canvas_skia_media_probe probe_media.cpp CanvasSkia::Media)
  canvas_add_skia_probe(canvas_skia_pdf_probe probe_pdf.cpp CanvasSkia::Skia)
  add_custom_target(canvas_skia_capability_probes DEPENDS
    canvas_skia_sdk_probe
    canvas_skia_paragraph_probe
    canvas_skia_skottie_probe
    canvas_skia_svg_probe
    canvas_skia_pathops_probe
    canvas_skia_media_probe
    canvas_skia_pdf_probe
  )
endif()
