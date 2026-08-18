find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
find_library(METAL_FRAMEWORK Metal REQUIRED)
find_library(QUARTZCORE_FRAMEWORK QuartzCore REQUIRED)
find_library(UIKIT_FRAMEWORK UIKit REQUIRED)

add_executable(canvas_poc03_ipados_runner MACOSX_BUNDLE ios_runner.mm)
target_compile_options(canvas_poc03_ipados_runner PRIVATE -fobjc-arc)
target_link_libraries(canvas_poc03_ipados_runner PRIVATE
  canvas_poc03_skia
  ${FOUNDATION_FRAMEWORK}
  ${METAL_FRAMEWORK}
  ${QUARTZCORE_FRAMEWORK}
  ${UIKIT_FRAMEWORK})
set_target_properties(canvas_poc03_ipados_runner PROPERTIES
  MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in
  MACOSX_BUNDLE_GUI_IDENTIFIER "dev.mostorm.canvas.poc03.ipados"
  XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "dev.mostorm.canvas.poc03.ipados"
  XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
  XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO")
