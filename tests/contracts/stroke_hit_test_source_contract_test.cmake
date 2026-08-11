cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

file(READ "${SOURCE_DIR}/include/canvas/document/stroke_hit_test.h"
     hit_header)
file(READ "${SOURCE_DIR}/src/document/stroke_hit_test.cpp"
     hit_source)
string(TOLOWER "${hit_header}${hit_source}" hit_text_lower)
string(REGEX REPLACE "//[^\n]*" "" hit_code_lower "${hit_text_lower}")

foreach(forbidden_token IN ITEMS
        "std::vector" "std::map" "std::unordered" "std::string"
        "std::deque" "std::list" "std::set" "std::multiset"
        "std::multimap" "std::pmr" "std::function" "std::any"
        "make_unique" "make_shared" "operator new" "new " "new("
        "malloc" "calloc" "realloc" "free(" "throw" "catch"
        "document.find" ".find(" "electron" "ipc" "skia"
        "platform/macos" "platform/windows")
    string(FIND "${hit_code_lower}" "${forbidden_token}" token_position)
    if(NOT token_position EQUAL -1)
        message(FATAL_ERROR
            "stroke hit-test must remain allocation/platform/IPC free: ${forbidden_token}")
    endif()
endforeach()

foreach(required_token IN ITEMS
        "StrokeSweepQuery" "workBudget" "StrokeHitToken" "nodeIndex"
        "cacheIdentity" "LayerClass" "BudgetExhausted" "noexcept")
    string(FIND "${hit_header}${hit_source}" "${required_token}"
           token_position)
    if(token_position EQUAL -1)
        message(FATAL_ERROR
            "stroke hit-test contract is missing: ${required_token}")
    endif()
endforeach()
