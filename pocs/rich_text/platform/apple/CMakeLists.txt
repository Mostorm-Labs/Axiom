if(NOT APPLE)
  message(FATAL_ERROR "POC-04 Apple adapter requires an Apple platform")
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
  find_library(CANVAS_POC04_UIKIT UIKit REQUIRED)
  find_library(CANVAS_POC04_CORE_GRAPHICS CoreGraphics REQUIRED)
  add_library(canvas_poc04_apple_ime STATIC apple_ime_adapter.mm)
  target_compile_definitions(canvas_poc04_apple_ime PRIVATE CANVAS_POC04_IOS=1)
  target_link_libraries(canvas_poc04_apple_ime PUBLIC
    canvas_poc04_core ${CANVAS_POC04_UIKIT} ${CANVAS_POC04_CORE_GRAPHICS})
  target_compile_options(canvas_poc04_apple_ime PRIVATE -fobjc-arc)
  target_include_directories(canvas_poc04_apple_ime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

  add_executable(canvas_poc04_ios_ime_recorder MACOSX_BUNDLE ios_ime_recorder.mm)
  target_compile_definitions(canvas_poc04_ios_ime_recorder PRIVATE CANVAS_POC04_IOS=1)
  target_compile_options(canvas_poc04_ios_ime_recorder PRIVATE -fobjc-arc)
  target_link_libraries(canvas_poc04_ios_ime_recorder PRIVATE
    canvas_poc04_apple_ime ${CANVAS_POC04_UIKIT})
  set_target_properties(canvas_poc04_ios_ime_recorder PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in
    MACOSX_BUNDLE_GUI_IDENTIFIER "dev.mostorm.canvas.poc04.ime"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "dev.mostorm.canvas.poc04.ime"
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
    XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO")
else()
  find_library(CANVAS_POC04_APPKIT AppKit REQUIRED)
  find_library(CANVAS_POC04_CORE_GRAPHICS CoreGraphics REQUIRED)
  add_library(canvas_poc04_apple_ime STATIC apple_ime_adapter.mm)
  target_compile_definitions(canvas_poc04_apple_ime PRIVATE CANVAS_POC04_MACOS=1)
  target_link_libraries(canvas_poc04_apple_ime PUBLIC
    canvas_poc04_core ${CANVAS_POC04_APPKIT} ${CANVAS_POC04_CORE_GRAPHICS})
  target_compile_options(canvas_poc04_apple_ime PRIVATE -fobjc-arc)
  target_include_directories(canvas_poc04_apple_ime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

  add_executable(canvas_poc04_macos_ime_recorder macos_ime_recorder.mm)
  target_compile_definitions(canvas_poc04_macos_ime_recorder PRIVATE CANVAS_POC04_MACOS=1)
  target_compile_options(canvas_poc04_macos_ime_recorder PRIVATE -fobjc-arc)
  target_link_libraries(canvas_poc04_macos_ime_recorder PRIVATE
    canvas_poc04_apple_ime ${CANVAS_POC04_APPKIT})
endif()
