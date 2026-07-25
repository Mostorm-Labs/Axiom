if(NOT DEFINED SURFACE_SOURCE)
    message(FATAL_ERROR "SURFACE_SOURCE is required")
endif()

file(READ "${SURFACE_SOURCE}" surface_source)

# Extract exactly WebView2Surface::Impl::fail(), stopping at its adjacent
# initial-navigation wrapper. Keeping this narrow avoids matching unrelated
# fail helpers or shared_from_this() calls elsewhere in the translation unit.
set(fail_signature "  HRESULT fail(HRESULT result) {")
string(FIND "${surface_source}" "${fail_signature}" fail_start)
if(fail_start EQUAL -1)
    message(FATAL_ERROR "Impl::fail(HRESULT) was not found")
endif()

string(SUBSTRING "${surface_source}" ${fail_start} -1 fail_tail)
set(next_signature
    "\n  HRESULT failInitialNavigation(HRESULT result) {")
string(FIND "${fail_tail}" "${next_signature}" fail_end)
if(fail_end EQUAL -1)
    message(FATAL_ERROR
            "Impl::fail could not be bounded by failInitialNavigation")
endif()
string(SUBSTRING "${fail_tail}" 0 ${fail_end} fail_body)

# Exercise the negative contract deterministically on every authoring host.
# This simulates the reviewed UAF shape without weakening or mutating the
# production source used by the ordinary CTest entry.
if(PROBE_OLD_FAIL)
    set(fail_body
        "  HRESULT fail(HRESULT result) {\n"
        "    lastResult = FAILED(result) ? result : E_FAIL;\n"
        "    state = State::Failed;\n"
        "    (void)initialLoad.completeFailure(lastResult);\n"
        "    return lastResult;\n"
        "  }\n")
endif()

set(local_failure "const HRESULT failure = FAILED(result) ? result : E_FAIL;")
set(keep_alive "const auto keepAlive = shared_from_this();")
set(dispatch "initialLoad.completeFailure(static_cast<std::int32_t>(failure))")
set(local_return "return failure;")

string(FIND "${fail_body}" "${local_failure}" local_failure_at)
string(FIND "${fail_body}" "${keep_alive}" keep_alive_at)
string(FIND "${fail_body}" "${dispatch}" dispatch_at)
string(FIND "${fail_body}" "${local_return}" local_return_at)
string(FIND "${fail_body}" "return lastResult;" member_return_at)

if(local_failure_at EQUAL -1)
    message(FATAL_ERROR "Impl::fail must normalize into local failure")
endif()
if(keep_alive_at EQUAL -1)
    message(FATAL_ERROR "Impl::fail must retain shared_from_this()")
endif()
if(dispatch_at EQUAL -1)
    message(FATAL_ERROR
            "Impl::fail must dispatch completeFailure from local failure")
endif()
if(local_return_at EQUAL -1)
    message(FATAL_ERROR "Impl::fail must return local failure")
endif()
if(NOT member_return_at EQUAL -1)
    message(FATAL_ERROR
            "Impl::fail must not read lastResult after callback dispatch")
endif()
if(NOT local_failure_at LESS keep_alive_at OR
   NOT keep_alive_at LESS dispatch_at OR
   NOT dispatch_at LESS local_return_at)
    message(FATAL_ERROR
            "Impl::fail must normalize, retain, dispatch, then return locally")
endif()
