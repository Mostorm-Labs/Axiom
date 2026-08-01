if(NOT DEFINED APP_SOURCE OR NOT EXISTS "${APP_SOURCE}")
    message(FATAL_ERROR "APP_SOURCE must point to whiteboard_app.cpp")
endif()

file(READ "${APP_SOURCE}" source)

set(required_contracts
    "wParam == app->embeddedCompletionTimeoutTimerId_"
    "SetTimer(window_, embeddedCompletionTimeoutTimerId_"
    "KillTimer(window_, embeddedCompletionTimeoutTimerId_"
    "sendOpenDocumentAdmission();"
    "isLoadCountWithinLimit("
    "embeddedCount + 1U)"
    "cancelActiveInputForDocumentTransition();"
    "inputRouter_.setActiveEmbeddedNode(std::nullopt);"
    "activeRoute_ = input::RouteResult{};"
    "if (app->pendingOpen_ != nullptr) return 0;"
    "document open in progress")

foreach(contract IN LISTS required_contracts)
    string(FIND "${source}" "${contract}" position)
    if(position LESS 0)
        message(FATAL_ERROR "Missing atomic open contract: ${contract}")
    endif()
endforeach()
