if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

set(pointer_sources
    "${SOURCE_DIR}/src/platform/macos/macos_pointer_adapter.h"
    "${SOURCE_DIR}/src/platform/macos/macos_pointer_adapter.cpp"
    "${SOURCE_DIR}/src/platform/macos/macos_mouse_session.h"
    "${SOURCE_DIR}/src/platform/macos/macos_mouse_session.cpp"
    "${SOURCE_DIR}/src/platform/macos/macos_whiteboard_input.h"
    "${SOURCE_DIR}/src/platform/macos/macos_whiteboard_input.cpp")

foreach(pointer_source IN LISTS pointer_sources)
    file(READ "${pointer_source}" pointer_text)
    string(TOLOWER "${pointer_text}" pointer_text_lower)
    # Dependency names are allowed in explanatory comments, but not in the
    # compiled seam. This remains a direct-source gate, not a general-purpose
    # dependency scanner.
    string(REGEX REPLACE "//[^\n]*" "" pointer_code_lower
           "${pointer_text_lower}")
    foreach(forbidden_token IN ITEMS
            "appkit" "nsevent" "ipc" "json" "electron" "webkit"
            "named_pipe" "namedpipe" "windows.h")
        string(FIND "${pointer_code_lower}" "${forbidden_token}" token_position)
        if(NOT token_position EQUAL -1)
            message(FATAL_ERROR
                    "${pointer_source} contains forbidden dependency: ${forbidden_token}")
        endif()
    endforeach()
endforeach()

file(READ "${SOURCE_DIR}/CMakeLists.txt" root_cmake)
string(REGEX MATCH
       "add_library[(]canvas_macos_platform STATIC[^)]*[)]"
       macos_platform_target "${root_cmake}")
if(NOT macos_platform_target MATCHES
   "macos_whiteboard_input[.]cpp")
    message(FATAL_ERROR
            "The macOS platform target must compile the whiteboard input controller")
endif()

string(REGEX MATCH
       "add_library[(]canvas_windows_platform STATIC[^)]*[)]"
       windows_platform_target "${root_cmake}")
if(windows_platform_target MATCHES
   "macos_(pointer_adapter|mouse_session|whiteboard_input)")
    message(FATAL_ERROR
            "The Windows platform target must not compile macOS pointer sources")
endif()

file(GLOB_RECURSE windows_sources
     "${SOURCE_DIR}/src/platform/windows/*"
     "${SOURCE_DIR}/app/windows/*")
foreach(windows_source IN LISTS windows_sources)
    if(IS_DIRECTORY "${windows_source}")
        continue()
    endif()
    file(READ "${windows_source}" windows_text)
    if(windows_text MATCHES
       "macos_(pointer_adapter|mouse_session|whiteboard_input)")
        message(FATAL_ERROR
                "Windows source must not reference the macOS pointer seam: ${windows_source}")
    endif()
endforeach()
