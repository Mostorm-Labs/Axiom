if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

file(READ "${SOURCE_DIR}/src/platform/macos/composition_view.mm"
     composition_source)
file(READ "${SOURCE_DIR}/src/platform/macos/macos_tablet_input.h"
     tablet_header)
file(READ "${SOURCE_DIR}/src/platform/macos/macos_tablet_input.cpp"
     tablet_source)

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
        "tabletSession_.reset")
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
   "- [(]void[)]mouseDown:[^}]*consumeTabletMouseEvent:event[^}]*return;[^}]*resetTabletSession[^}]*consumeEvent:event")
    message(FATAL_ERROR
        "Ordinary mouse Down must reset stale tablet state before MacosMouseSession")
endif()
