if(NOT WIN32)
  message(FATAL_ERROR "POC-04 Windows adapter requires Win32")
endif()

add_library(canvas_poc04_windows_ime STATIC windows_ime_adapter.cpp)
target_include_directories(canvas_poc04_windows_ime PUBLIC .)
target_link_libraries(canvas_poc04_windows_ime PUBLIC canvas_poc04_core imm32)

add_executable(canvas_poc04_windows_demo windows_demo.cpp)
target_link_libraries(canvas_poc04_windows_demo PRIVATE canvas_poc04_windows_ime user32)
if(TARGET canvas_poc04_skparagraph AND CANVAS_POC04_ICU_DATA_PATH)
  add_custom_command(TARGET canvas_poc04_windows_demo POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      "${CANVAS_POC04_ICU_DATA_PATH}"
      "$<TARGET_FILE_DIR:canvas_poc04_windows_demo>/icudtl.dat")
endif()
