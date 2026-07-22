set(CANVAS_WEBVIEW2_ROOT
    "${CMAKE_SOURCE_DIR}/third_party/nuget/Microsoft.Web.WebView2"
    CACHE PATH "Root of the restored Microsoft.Web.WebView2 NuGet package")

set(_canvas_webview2_include "${CANVAS_WEBVIEW2_ROOT}/build/native/include")
set(_canvas_webview2_header "${_canvas_webview2_include}/WebView2.h")
set(_canvas_webview2_loader
    "${CANVAS_WEBVIEW2_ROOT}/build/native/x64/WebView2LoaderStatic.lib")

if(NOT EXISTS "${_canvas_webview2_header}")
    message(FATAL_ERROR
            "WebView2.h not found at ${_canvas_webview2_header}. "
            "Run scripts/Restore-WebView2.ps1 before configuring.")
endif()
if(NOT EXISTS "${_canvas_webview2_loader}")
    message(FATAL_ERROR
            "WebView2 static loader not found at ${_canvas_webview2_loader}. "
            "Run scripts/Restore-WebView2.ps1 before configuring.")
endif()

if(NOT TARGET canvas_webview2_loader)
    add_library(canvas_webview2_loader STATIC IMPORTED GLOBAL)
    set_target_properties(
        canvas_webview2_loader
        PROPERTIES
            IMPORTED_LOCATION "${_canvas_webview2_loader}"
            INTERFACE_INCLUDE_DIRECTORIES "${_canvas_webview2_include}")
endif()

if(NOT TARGET canvas::webview2_loader)
    add_library(canvas::webview2_loader ALIAS canvas_webview2_loader)
endif()

unset(_canvas_webview2_include)
unset(_canvas_webview2_header)
unset(_canvas_webview2_loader)
