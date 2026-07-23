if(NOT DEFINED RESTORE_SCRIPT)
    message(FATAL_ERROR "RESTORE_SCRIPT is required")
endif()

file(READ "${RESTORE_SCRIPT}" restore_script)

string(REGEX MATCHALL
       "Test-Path -LiteralPath \\$header -PathType Leaf"
       header_checks "${restore_script}")
list(LENGTH header_checks header_check_count)
if(header_check_count LESS 2)
    message(FATAL_ERROR
            "Restore-WebView2.ps1 must check the header before and after restore")
endif()

string(REGEX MATCHALL
       "Test-Path -LiteralPath \\$staticLoader -PathType Leaf"
       loader_checks "${restore_script}")
list(LENGTH loader_checks loader_check_count)
if(loader_check_count LESS 2)
    message(FATAL_ERROR
            "Restore-WebView2.ps1 must check the static loader before and after restore")
endif()

foreach(required_text
        "1.0.4078.44"
        "build/native/x64/WebView2LoaderStatic.lib"
        "Remove-Item -LiteralPath $packageRoot -Recurse -Force")
    string(FIND "${restore_script}" "${required_text}" found_at)
    if(found_at EQUAL -1)
        message(FATAL_ERROR
                "Restore-WebView2.ps1 is missing contract text: ${required_text}")
    endif()
endforeach()
