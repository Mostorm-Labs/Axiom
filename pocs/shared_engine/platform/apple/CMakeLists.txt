find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
find_library(METAL_FRAMEWORK Metal REQUIRED)
find_library(QUARTZCORE_FRAMEWORK QuartzCore REQUIRED)

add_library(canvas_poc01_apple_adapter STATIC
  apple_metal_adapter.mm
  runner_support.mm
)
target_include_directories(canvas_poc01_apple_adapter
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${POC01_ROOT}/src ${POC01_ROOT}/platform/skia
)
target_compile_options(canvas_poc01_apple_adapter PRIVATE -fobjc-arc)
target_link_libraries(canvas_poc01_apple_adapter PUBLIC
  canvas_poc01_skia
  ${FOUNDATION_FRAMEWORK}
  ${METAL_FRAMEWORK}
  ${QUARTZCORE_FRAMEWORK}
)

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
  find_library(UIKIT_FRAMEWORK UIKit REQUIRED)
  add_executable(canvas_poc01_ios_runner MACOSX_BUNDLE ios_runner.mm)
  target_include_directories(canvas_poc01_ios_runner PRIVATE
    ${POC01_ROOT}/src ${POC01_ROOT}/platform/skia
  )
  target_compile_options(canvas_poc01_ios_runner PRIVATE -fobjc-arc)
  target_link_libraries(canvas_poc01_ios_runner PRIVATE
    canvas_poc01_apple_adapter ${UIKIT_FRAMEWORK}
  )
  target_compile_definitions(canvas_poc01_ios_runner PRIVATE
    CANVAS_POC01_APPLE_MOBILE=1
  )
  set_target_properties(canvas_poc01_ios_runner PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in
    MACOSX_BUNDLE_GUI_IDENTIFIER "dev.mostorm.canvas.poc01"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "dev.mostorm.canvas.poc01"
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
    XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
  )
  foreach(resource
      ${POC01_ROOT}/fixtures/scene.ndjson
      ${POC01_ROOT}/fixtures/checker.png
      ${CANVAS_POC01_SKIA_FONT_PATH})
    target_sources(canvas_poc01_ios_runner PRIVATE ${resource})
    set_source_files_properties(${resource} PROPERTIES
      MACOSX_PACKAGE_LOCATION Resources
    )
  endforeach()
else()
  add_executable(canvas_poc01_macos_runner macos_runner.mm)
  target_include_directories(canvas_poc01_macos_runner PRIVATE
    ${POC01_ROOT}/src ${POC01_ROOT}/platform/skia
  )
  target_compile_options(canvas_poc01_macos_runner PRIVATE -fobjc-arc)
  target_link_libraries(canvas_poc01_macos_runner PRIVATE
    canvas_poc01_apple_adapter
  )
  target_compile_definitions(canvas_poc01_macos_runner PRIVATE
    CANVAS_POC01_FIXTURE_DIR="${POC01_ROOT}/fixtures"
    CANVAS_POC01_FONT_PATH="${CANVAS_POC01_SKIA_FONT_PATH}"
  )
endif()
