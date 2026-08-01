if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

file(READ "${SOURCE_DIR}/CMakeLists.txt" root_cmake)
file(READ "${SOURCE_DIR}/src/platform/macos/metal_host.mm" host_source)
file(READ "${SOURCE_DIR}/src/platform/macos/composition_view.h" composition_header)
file(READ "${SOURCE_DIR}/src/platform/macos/composition_view.mm" composition_source)
file(READ "${SOURCE_DIR}/app/macos/main.mm" app_source)

if(NOT root_cmake MATCHES "src/platform/macos/composition_view.mm")
    message(FATAL_ERROR "The macOS platform target must own the composition host")
endif()

foreach(required_header_token IN ITEMS
        "baseMetalView"
        "embeddedContainerView"
        "overlayMetalView"
        "embeddedInteractionEnabled")
    string(FIND "${composition_header}" "${required_header_token}" token_position)
    if(token_position EQUAL -1)
        message(FATAL_ERROR "CanvasCompositionView is missing API: ${required_header_token}")
    endif()
endforeach()

string(FIND "${composition_source}" "addSubview:baseMetalView_" add_base)
string(FIND "${composition_source}" "addSubview:embeddedContainerView_" add_embedded)
string(FIND "${composition_source}" "addSubview:overlayMetalView_" add_overlay)
if(add_base EQUAL -1 OR add_embedded EQUAL -1 OR add_overlay EQUAL -1 OR
   add_base GREATER add_embedded OR add_embedded GREATER add_overlay)
    message(FATAL_ERROR "The fixed AppKit siblings must be added back to front")
endif()

foreach(required_host_token IN ITEMS
        "skiaSurfaceFramePlan"
        "metalLayer.opaque = plan.opaque ? YES : NO"
        "canvas->clear(static_cast<SkColor>(plan.clearColorArgb))"
        "plan.layerCount"
        "plan.layers[layerIndex]")
    string(FIND "${host_source}" "${required_host_token}" token_position)
    if(token_position EQUAL -1)
        message(FATAL_ERROR "MetalHost is missing split-surface operation: ${required_host_token}")
    endif()
endforeach()

if(NOT composition_source MATCHES
       "CanvasEmbeddedContainerView")
    message(FATAL_ERROR "The embedded container must use the flipped AppKit host")
endif()

if(NOT composition_source MATCHES
       "- \\(BOOL\\)isFlipped")
    message(FATAL_ERROR "The embedded container must expose flipped coordinates")
endif()

if(NOT composition_source MATCHES
       "createMetalRenderResources\(\)")
    message(FATAL_ERROR "The two surfaces must be given one shared resource group")
endif()

if(NOT composition_source MATCHES
       "surfaceRole:canvas::macos::MetalSurfaceRole::Base[ \t\r\n]+renderResources:renderResources_")
    message(FATAL_ERROR "The base surface must use the shared resource group with the Base frame role")
endif()

if(NOT composition_source MATCHES
       "surfaceRole:canvas::macos::MetalSurfaceRole::Overlay[ \t\r\n]+renderResources:renderResources_")
    message(FATAL_ERROR "The overlay surface must use the same resource group with the Overlay frame role")
endif()

foreach(required_shared_resource_token IN ITEMS
        "attachment->resources = impl_->sharedResources"
        "metalLayer.device = impl_->sharedResources->device"
        "resources->skiaContext.get()"
        "[resources->commandQueue commandBuffer]"
        "resources->renderer.drawLayer")
    string(FIND "${host_source}" "${required_shared_resource_token}" token_position)
    if(token_position EQUAL -1)
        message(FATAL_ERROR
            "MetalHost is not using the shared resource group for: ${required_shared_resource_token}")
    endif()
endforeach()

if(NOT app_source MATCHES "CanvasCompositionView" OR
   NOT app_source MATCHES "setCanvasDocument:document")
    message(FATAL_ERROR "The macOS app must exercise the fixed composition host")
endif()
