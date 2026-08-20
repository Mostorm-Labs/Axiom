find_library(AVFOUNDATION_FRAMEWORK AVFoundation REQUIRED)
find_library(COREMEDIA_FRAMEWORK CoreMedia REQUIRED)
find_library(COREVIDEO_FRAMEWORK CoreVideo REQUIRED)
find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
find_library(METAL_FRAMEWORK Metal REQUIRED)
find_library(QUARTZCORE_FRAMEWORK QuartzCore REQUIRED)
find_library(UIKIT_FRAMEWORK UIKit REQUIRED)
find_library(WEBKIT_FRAMEWORK WebKit REQUIRED)

add_executable(canvas_poc05_apple_runner MACOSX_BUNDLE apple_runner.mm)
target_compile_options(canvas_poc05_apple_runner PRIVATE -fobjc-arc)
target_link_libraries(canvas_poc05_apple_runner PRIVATE
  canvas_poc05_core
  canvas_poc03_skia
  ${AVFOUNDATION_FRAMEWORK}
  ${COREMEDIA_FRAMEWORK}
  ${COREVIDEO_FRAMEWORK}
  ${FOUNDATION_FRAMEWORK}
  ${METAL_FRAMEWORK}
  ${QUARTZCORE_FRAMEWORK}
  ${UIKIT_FRAMEWORK}
  ${WEBKIT_FRAMEWORK})
set_target_properties(canvas_poc05_apple_runner PROPERTIES
  MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in
  MACOSX_BUNDLE_GUI_IDENTIFIER "dev.mostorm.axiom.poc05.apple"
  XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "dev.mostorm.axiom.poc05.apple"
  XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
  XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO")
