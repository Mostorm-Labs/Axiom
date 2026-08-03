if(NOT DEFINED SURFACE_SOURCE)
    message(FATAL_ERROR "SURFACE_SOURCE is required")
endif()

file(READ "${SURFACE_SOURCE}" surface_source)

function(extract_section start_marker end_marker output_variable)
    string(FIND "${surface_source}" "${start_marker}" section_start)
    if(section_start EQUAL -1)
        message(FATAL_ERROR "Section start was not found: ${start_marker}")
    endif()
    string(SUBSTRING "${surface_source}" ${section_start} -1 section_tail)
    string(FIND "${section_tail}" "${end_marker}" section_end)
    if(section_end EQUAL -1)
        message(FATAL_ERROR "Section end was not found: ${end_marker}")
    endif()
    string(SUBSTRING "${section_tail}" 0 ${section_end} section_body)
    set(${output_variable} "${section_body}" PARENT_SCOPE)
endfunction()

# get_Source is only a same-document optimization. A transient/opaque source
# must conservatively require NavigationStarting instead of failing the load.
extract_section(
    "  HRESULT nativeNavigationExpectsStarting("
    "\n  HRESULT navigatePrepared("
    preflight_body)

foreach(required_fragment IN ITEMS
        "if (FAILED(sourceResult) || rawSource == nullptr)"
        "if (!sourceView)"
        "if (FAILED(canonicalResult)) return S_OK;")
    string(FIND "${preflight_body}" "${required_fragment}" fragment_at)
    if(fragment_at EQUAL -1)
        message(FATAL_ERROR
                "WebView2 source preflight must fail soft: ${required_fragment}")
    endif()
endforeach()
foreach(forbidden_fragment IN ITEMS
        "return FAILED(sourceResult) ? sourceResult : E_POINTER;"
        "return E_INVALIDARG;"
        "if (FAILED(canonicalResult)) return canonicalResult;")
    string(FIND "${preflight_body}" "${forbidden_fragment}" fragment_at)
    if(NOT fragment_at EQUAL -1)
        message(FATAL_ERROR
                "WebView2 source preflight retains a terminal source error: ${forbidden_fragment}")
    endif()
endforeach()

# SourceChanged is advisory for document identity. Its getter/parser failures
# may be transient for data: navigation and must never terminally fail a load.
extract_section(
    "    auto sourceChanged = Callback<ICoreWebView2SourceChangedEventHandler>("
    "\n    if (!sourceChanged)"
    source_changed_body)
string(FIND "${source_changed_body}" "ignoreUnusableSource" ignore_at)
if(ignore_at EQUAL -1)
    message(FATAL_ERROR
            "SourceChanged must explicitly ignore unusable source observations")
endif()

# A fragment whose source preflight was unavailable owns a temporary native
# start slot. Only the documented same-document SourceChanged evidence may
# release that slot; without this path the serial scheduler can starve every
# later navigation forever.
foreach(required_fragment IN ITEMS
        "RequiredOrSameDocumentSource"
        "args->get_IsNewDocument(&isNewDocument)"
        "nativeNavigationSourceChangedAction("
        "resolveSameDocumentNavigationForRequest("
        "sameDocumentSourceObserved")
    string(FIND "${source_changed_body}" "${required_fragment}" fragment_at)
    if(fragment_at EQUAL -1)
        message(FATAL_ERROR
                "SourceChanged must resolve uncertain fragment admission: ${required_fragment}")
    endif()
endforeach()
string(FIND "${source_changed_body}" "failInitialNavigation" source_fail_at)
if(NOT source_fail_at EQUAL -1)
    message(FATAL_ERROR
            "SourceChanged must not fail the navigation lifecycle")
endif()

# Keep the authoritative policy and terminal event fail-closed while making
# only the advisory source observation fail-soft.
extract_section(
    "  HRESULT onNavigationCompleted("
    "\n  HRESULT onNavigationStarting("
    completed_body)
foreach(required_fragment IN ITEMS
        "if (FAILED(successResult)) return failInitialNavigation(successResult);"
        "if (succeeded == FALSE) return failInitialNavigation(E_FAIL);")
    string(FIND "${completed_body}" "${required_fragment}" fragment_at)
    if(fragment_at EQUAL -1)
        message(FATAL_ERROR
                "NavigationCompleted must retain terminal failure handling")
    endif()
endforeach()

extract_section(
    "  HRESULT onNavigationStarting("
    "\n  HRESULT configureSecurity("
    starting_body)
foreach(required_fragment IN ITEMS
        "return abandonCurrentStart(E_INVALIDARG);"
        "return abandonCurrentStart(canonicalResult);")
    string(FIND "${starting_body}" "${required_fragment}" fragment_at)
    if(fragment_at EQUAL -1)
        message(FATAL_ERROR
                "NavigationStarting must retain fail-closed URI handling")
    endif()
endforeach()
