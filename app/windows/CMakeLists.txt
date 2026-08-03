add_executable(canvas_windows WIN32 main.cpp whiteboard_app.cpp)
target_include_directories(canvas_windows PRIVATE
                           ${CMAKE_CURRENT_SOURCE_DIR}
                           ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(canvas_windows PRIVATE canvas::core
                                            canvas::windows_platform
                                            ole32 shell32 user32)
canvas_enable_warnings(canvas_windows)

# TARGET-form build events must be declared in the directory that creates the
# target. Keep the relink-time web asset synchronization attached to the app.
add_custom_command(TARGET canvas_windows POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E rm -rf
            "$<TARGET_FILE_DIR:canvas_windows>/web"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${_canvas_web_dist}"
            "$<TARGET_FILE_DIR:canvas_windows>/web"
    COMMENT "Copying Canvas embedded web assets"
    VERBATIM)
