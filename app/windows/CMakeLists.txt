add_executable(canvas_windows WIN32 main.cpp whiteboard_app.cpp)
target_include_directories(canvas_windows PRIVATE
                           ${CMAKE_CURRENT_SOURCE_DIR}
                           ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(canvas_windows PRIVATE canvas::core
                                            canvas::windows_platform
                                            ole32 user32)
canvas_enable_warnings(canvas_windows)
