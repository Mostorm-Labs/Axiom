add_executable(canvas_macos MACOSX_BUNDLE main.mm)
target_include_directories(canvas_macos PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(canvas_macos PRIVATE canvas::macos_platform canvas::core)
canvas_enable_warnings(canvas_macos)
target_compile_options(canvas_macos PRIVATE -fobjc-arc)

set_target_properties(canvas_macos PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.mostorm.canvas"
    MACOSX_BUNDLE_BUNDLE_NAME "Canvas"
    MACOSX_BUNDLE_BUNDLE_VERSION "0.1.0"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1.0")
