if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

file(READ "${SOURCE_DIR}/src/platform/macos/composition_view.mm"
     composition_source)
file(READ "${SOURCE_DIR}/src/platform/macos/macos_tablet_input.h"
     tablet_header)
file(READ "${SOURCE_DIR}/src/platform/macos/macos_tablet_input.cpp"
     tablet_source)
file(READ "${SOURCE_DIR}/src/platform/macos/macos_tablet_pointer_bridge.h"
     bridge_header)
file(READ "${SOURCE_DIR}/src/platform/macos/macos_tablet_pointer_bridge.cpp"
     bridge_source)
file(READ "${SOURCE_DIR}/src/platform/macos/macos_whiteboard_input.h"
     controller_header)
file(READ "${SOURCE_DIR}/src/platform/macos/macos_whiteboard_input.cpp"
     controller_source)

foreach(required_tablet_model_token IN ITEMS
        "MacTabletPointSource"
        "MacTabletTool"
        "MacTabletIntent"
        "MacTabletDeviceIdentity"
        "capabilityMask"
        "tiltScaled"
        "EraserPending"
        "maxProximateDevices"
        "maxActiveContacts")
    string(FIND "${tablet_header}" "${required_tablet_model_token}"
           token_position)
    if(token_position EQUAL -1)
        message(FATAL_ERROR
            "The pure macOS tablet model is missing: ${required_tablet_model_token}")
    endif()
endforeach()

string(TOLOWER "${bridge_header}${bridge_source}" bridge_text_lower)
string(REGEX REPLACE "//[^\n]*" "" bridge_code_lower
       "${bridge_text_lower}")
foreach(forbidden_bridge_token IN ITEMS
        "std::vector" "std::map" "std::unordered"
        "make_unique" "make_shared" "electron" "ipc")
    string(FIND "${bridge_code_lower}" "${forbidden_bridge_token}"
           token_position)
    if(NOT token_position EQUAL -1)
        message(FATAL_ERROR
            "The tablet bridge contains a hot-path dependency/allocation token: ${forbidden_bridge_token}")
    endif()
endforeach()

foreach(required_controller_token IN ITEMS
        "sample.kind == input::PointerKind::Mouse"
        "sample.kind == input::PointerKind::Pen"
        "active_->kind != sample.kind"
        "mode_ != input::InputMode::Draw"
        "router_.route(sample.kind, hitEmbedded)")
    string(FIND "${controller_header}${controller_source}"
           "${required_controller_token}" token_position)
    if(token_position EQUAL -1)
        message(FATAL_ERROR
            "MacosWhiteboardInput is missing Pen-safe controller behavior: ${required_controller_token}")
    endif()
endforeach()

foreach(required_bridge_token IN ITEMS
        "MacosTabletPointerOutput"
        "MacosTabletPointerBridge"
        "sample.device.tool != MacTabletTool::Pen"
        "sample.intent != MacTabletIntent::Ink"
        "sample.pressureSupported()"
        "input::PointerKind::Pen"
        "pointer.tiltXDegrees = 0.0F"
        "pointer.tiltYDegrees = 0.0F"
        "pointer.predicted = false"
        "0.5F")
    string(FIND "${bridge_header}${bridge_source}" "${required_bridge_token}"
           token_position)
    if(token_position EQUAL -1)
        message(FATAL_ERROR
            "The tablet-to-pointer bridge is missing: ${required_bridge_token}")
    endif()
endforeach()

string(TOLOWER "${tablet_header}${tablet_source}" tablet_text_lower)
string(REGEX REPLACE "//[^\n]*" "" tablet_code_lower
       "${tablet_text_lower}")
foreach(forbidden_tablet_token IN ITEMS
        "tiltxdegrees" "tiltydegrees" "nsevent" "appkit"
        "electron" "ipc" "event monitor")
    string(FIND "${tablet_code_lower}" "${forbidden_tablet_token}"
           token_position)
    if(NOT token_position EQUAL -1)
        message(FATAL_ERROR
            "The pure tablet seam contains forbidden semantics/dependency: ${forbidden_tablet_token}")
    endif()
endforeach()

foreach(required_appkit_token IN ITEMS
        "tabletPoint:"
        "tabletProximity:"
        "NSEventSubtypeTabletPoint"
        "NSEventSubtypeTabletProximity"
        "event.deviceID"
        "event.pressure"
        "event.tilt"
        "event.buttonMask"
        "event.uniqueID"
        "event.pointingDeviceID"
        "event.systemTabletID"
        "event.capabilityMask"
        "event.pointingDeviceType"
        "tabletSession_.consumePoint"
        "tabletSession_.consumeProximity"
        "tabletSession_.reset"
        "dispatchTabletOutput:"
        "takeTabletCancellationOutput"
        "cancelActivePointerSessions"
        "MacosTabletPointerBridge::convertOutput")
    string(FIND "${composition_source}" "${required_appkit_token}"
           token_position)
    if(token_position EQUAL -1)
        message(FATAL_ERROR
            "The AppKit tablet extraction seam is missing: ${required_appkit_token}")
    endif()
endforeach()

if(composition_source MATCHES
   "add(Global|Local)MonitorForEventsMatchingMask")
    message(FATAL_ERROR
        "Tablet input must stay on the responder path, without event monitors")
endif()

foreach(tablet_output_name IN ITEMS
        "associatedTabletOutput"
        "associatedProximityOutput"
        "nativeTabletOutput"
        "nativeProximityOutput")
    string(FIND "${composition_source}"
           "const auto ${tablet_output_name}" output_capture_position)
    string(FIND "${composition_source}"
           "[self dispatchTabletOutput:${tablet_output_name}]"
           output_dispatch_position)
    if(output_capture_position EQUAL -1 OR
       output_dispatch_position EQUAL -1 OR
       NOT output_capture_position LESS output_dispatch_position)
        message(FATAL_ERROR
            "${tablet_output_name} must be synchronously dispatched in production order")
    endif()
endforeach()

if(NOT composition_source MATCHES
   "- [(]void[)]cancelActivePointerSessions[^}]*takeTabletCancellationOutput[^}]*dispatchTabletOutput:[^}]*takeMouseCancellationOutput[^}]*dispatchOutput:")
    message(FATAL_ERROR
        "Every lifecycle cancel must synchronously retire tablet and mouse sessions")
endif()

foreach(required_teardown_token IN ITEMS
        "pointerView.pointerInputDelegate = nil"
        "tabletCancellation = [pointerView takeTabletCancellationOutput]"
        "mouseCancellation = [pointerView takeMouseCancellationOutput]"
        "MacosTabletPointerBridge::convertOutput"
        "whiteboardInput_->consume(tabletPointers[index])"
        "whiteboardInput_->consume(mouseCancellation[index])")
    string(FIND "${composition_source}" "${required_teardown_token}"
           token_position)
    if(token_position EQUAL -1)
        message(FATAL_ERROR
            "Composition teardown is missing direct pointer rollback: ${required_teardown_token}")
    endif()
endforeach()

if(composition_source MATCHES
   "[(]void[)]tabletSession_[.]consume(Point|Proximity)" OR
   composition_source MATCHES
   "[(]void[)]tabletSession_[.]reset")
    message(FATAL_ERROR
        "Eligible tablet output must be synchronously bridged instead of discarded")
endif()

foreach(mouse_method IN ITEMS "mouseDown" "mouseDragged" "mouseUp")
    if(NOT composition_source MATCHES
       "- [(]void[)]${mouse_method}:[^}]*consumeTabletMouseEvent:event[^}]*return;[^}]*consumeEvent:event")
        message(FATAL_ERROR
            "${mouse_method}: must return tablet subtypes before MacosMouseSession")
    endif()
endforeach()

string(FIND "${composition_source}" "case NSEventSubtypeTabletPoint:"
       tablet_point_case_position)
string(FIND "${composition_source}"
       "const auto mouseCancellation = [self takeMouseCancellationOutput];"
       mouse_cancellation_position)
string(FIND "${composition_source}" "[self dispatchOutput:mouseCancellation];"
       mouse_cancellation_dispatch_position)
string(FIND "${composition_source}" "tabletSession_.consumePoint"
       tablet_consume_position)
if(tablet_point_case_position EQUAL -1 OR
   mouse_cancellation_position EQUAL -1 OR
   mouse_cancellation_dispatch_position EQUAL -1 OR
   tablet_consume_position EQUAL -1 OR
   NOT tablet_point_case_position LESS mouse_cancellation_position OR
   NOT mouse_cancellation_position LESS mouse_cancellation_dispatch_position OR
   NOT mouse_cancellation_dispatch_position LESS tablet_consume_position)
    message(FATAL_ERROR
        "Associated tablet point input must cancel and dispatch any active mouse session before tablet consumption")
endif()

if(NOT composition_source MATCHES
   "- [(]void[)]mouseDown:[^}]*consumeTabletMouseEvent:event[^}]*return;[^}]*takeTabletCancellationOutput[^}]*dispatchTabletOutput:[^}]*consumeEvent:event")
    message(FATAL_ERROR
        "Ordinary mouse Down must dispatch tablet cancellation before MacosMouseSession")
endif()
