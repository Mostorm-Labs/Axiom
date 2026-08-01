if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

file(READ "${SOURCE_DIR}/vcpkg.json" vcpkg_manifest)
file(READ "${SOURCE_DIR}/CMakeLists.txt" root_cmake)
file(READ "${SOURCE_DIR}/src/platform/macos/metal_host.h" host_header)
file(READ "${SOURCE_DIR}/src/platform/macos/metal_host.mm" host_source)
file(READ "${SOURCE_DIR}/src/platform/macos/metal_view.mm" view_source)
file(READ "${SOURCE_DIR}/app/macos/main.mm" app_source)

if(NOT vcpkg_manifest MATCHES
       "\"features\"[ \t\r\n]*:[ \t\r\n]*\\[[ \t\r\n]*\"metal\"[ \t\r\n]*,[ \t\r\n]*\"png\"[ \t\r\n]*\\]")
    message(FATAL_ERROR "The Apple Skia dependency must explicitly enable Metal")
endif()

string(REGEX MATCH
       "target_link_libraries\\(canvas_macos_platform PRIVATE[^\\)]*\\)"
       macos_private_links "${root_cmake}")
if(NOT macos_private_links MATCHES "canvas::core" OR
   NOT macos_private_links MATCHES "unofficial::skia::skia")
    message(FATAL_ERROR "The macOS platform target must link the shared renderer and Skia")
endif()

if(NOT host_header MATCHES
       "setDocument\\(std::shared_ptr<const document::Document> document\\)")
    message(FATAL_ERROR "MetalHost must retain a lifetime-safe shared document source")
endif()

foreach(required_token IN ITEMS
        "GrDirectContexts::MakeMetal"
        "metalLayer.framebufferOnly = NO"
        "GrBackendRenderTargets::MakeMtl"
        "SkSurfaces::WrapBackendRenderTarget"
        "skiaSurfaceFramePlan"
        "canvas->scale"
        "renderer.drawLayer"
        "SkSurfaces::BackendSurfaceAccess::kPresent"
        "context->submit(GrSyncCpu::kNo)"
        "presentDrawable:drawable")
    string(FIND "${host_source}" "${required_token}" token_position)
    if(token_position EQUAL -1)
        message(FATAL_ERROR "MetalHost is missing Skia/Metal operation: ${required_token}")
    endif()
endforeach()

if(host_source MATCHES "renderCommandEncoderWithDescriptor")
    message(FATAL_ERROR "The host must render through shared SkiaRenderer, not a parallel Metal clear pass")
endif()

string(FIND "${host_source}" "renderer.drawLayer" draw_layer)
string(FIND "${host_source}" "context->flush" flush_surface)
string(FIND "${host_source}" "context->submit" submit_context)
string(FIND "${host_source}" "presentDrawable:drawable" present_drawable)
string(FIND "${host_source}" "[presentationBuffer commit]" commit_presentation)
if(draw_layer GREATER flush_surface OR flush_surface GREATER submit_context OR
   submit_context GREATER present_drawable OR
   present_drawable GREATER commit_presentation)
    message(FATAL_ERROR "Skia draw/flush/submit must finish before the drawable is presented")
endif()

if(NOT app_source MATCHES "setCanvasDocument:document")
    message(FATAL_ERROR "The AppKit vertical slice must bind an actual Document to MetalHost")
endif()

foreach(required_view_token IN ITEMS
        "wantsUpdateLayer"
        "updateLayer"
        "displayLayer:"
        "metalHost_->drawIfNeeded()"
        "metalHost_->reschedulePendingFrame()")
    string(FIND "${view_source}" "${required_view_token}" view_token_position)
    if(view_token_position EQUAL -1)
        message(FATAL_ERROR "CanvasMetalView is missing event-driven AppKit scheduling operation: ${required_view_token}")
    endif()
endforeach()

if(view_source MATCHES "drawRect:")
    message(FATAL_ERROR "The custom layer-hosting view must not rely on drawRect for Metal frames")
endif()
