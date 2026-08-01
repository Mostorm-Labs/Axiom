if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

file(READ "${SOURCE_DIR}/CMakeLists.txt" root_cmake)
file(READ "${SOURCE_DIR}/src/platform/macos/wkwebview_surface.h" surface_header)
file(READ "${SOURCE_DIR}/src/platform/macos/wkwebview_surface.mm" surface_source)
file(READ "${SOURCE_DIR}/src/platform/macos/wkwebview_navigation_seam.h" navigation_seam)

if(NOT root_cmake MATCHES "find_library\\(CANVAS_WEBKIT_FRAMEWORK WebKit REQUIRED\\)")
    message(FATAL_ERROR "The macOS platform target must locate WebKit")
endif()

if(NOT root_cmake MATCHES "src/platform/macos/wkwebview_surface.mm")
    message(FATAL_ERROR "The macOS platform target must own WKWebViewSurface")
endif()

if(surface_header MATCHES "#(import|include)[ \\t]+<WebKit/")
    message(FATAL_ERROR "WKWebViewSurface must hide WebKit from its public header")
endif()

foreach(required_header_token IN ITEMS
        "class WKWebViewSurface final : public embed::EmbeddedSurface"
        "bool attach(void* nativeContainer)"
        "void detach()"
        "void close()")
    string(FIND "${surface_header}" "${required_header_token}" token_position)
    if(token_position EQUAL -1)
        message(FATAL_ERROR "WKWebViewSurface is missing API: ${required_header_token}")
    endif()
endforeach()

foreach(required_source_token IN ITEMS
        "#import <WebKit/WebKit.h>"
        "void requireMainThread()"
        "CanvasCompositionView remains the"
        "[container addSubview:impl_->webView]"
        "[impl_->webView removeFromSuperview]"
        "impl_->webView.canvasInteractionEnabled = interactive ? YES : NO"
        "navigationDelegate"
        "decidePolicyForNavigationAction"
        "decidePolicyForNavigationResponse"
        "URLByResolvingSymlinksInPath"
        "std::weak_ptr<WKWebViewSurface::Impl>"
        "navigationDispatching"
        "stopLoading")
    string(FIND "${surface_source}" "${required_source_token}" token_position)
    if(token_position EQUAL -1)
        message(FATAL_ERROR "WKWebViewSurface is missing lifecycle contract: ${required_source_token}")
    endif()
endforeach()

foreach(required_navigation_token IN ITEMS
        "NavigationTracker"
        "NavigationClass::Https"
        "allowTestDataUrls"
        "kMaxUriBytes")
    string(FIND "${navigation_seam}" "${required_navigation_token}" token_position)
    if(token_position EQUAL -1)
        message(FATAL_ERROR "macOS navigation seam is missing: ${required_navigation_token}")
    endif()
endforeach()
