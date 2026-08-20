set(POC05_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

add_library(canvas_poc05_core STATIC src/hybrid_surface.cpp)
target_include_directories(canvas_poc05_core
  PUBLIC
    include
    ${PROJECT_SOURCE_DIR}/docs/api
)
set_target_properties(canvas_poc05_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MSVC)
  target_compile_options(canvas_poc05_core PRIVATE /W4 /permissive- /WX /EHsc)
else()
  target_compile_options(canvas_poc05_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(CANVAS_POC05_ENABLE_SANITIZERS)
  if(MSVC)
    message(FATAL_ERROR "POC-05 sanitizer preset is host-only")
  elseif(APPLE)
    target_compile_options(canvas_poc05_core PRIVATE
      -fsanitize=undefined -fno-omit-frame-pointer)
    target_link_options(canvas_poc05_core PUBLIC -fsanitize=undefined)
  else()
    target_compile_options(canvas_poc05_core PRIVATE
      -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(canvas_poc05_core PUBLIC -fsanitize=address,undefined)
  endif()
endif()

if(CANVAS_POC05_BUILD_TESTS AND BUILD_TESTING)
  if(NOT EXISTS ${PROJECT_SOURCE_DIR}/.deps/googletest/CMakeLists.txt)
    message(FATAL_ERROR "Missing GoogleTest. Run: python3 tools/bootstrap_deps.py --core")
  endif()
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  set(gtest_force_shared_crt OFF CACHE BOOL "" FORCE)
  add_subdirectory(${PROJECT_SOURCE_DIR}/.deps/googletest
                   ${CMAKE_BINARY_DIR}/_deps/googletest-poc05 EXCLUDE_FROM_ALL)
  add_executable(canvas_poc05_tests
    tests/hybrid_surface_test.cpp
    tests/poc03_frame_contract_test.cpp
  )
  target_link_libraries(canvas_poc05_tests PRIVATE
    canvas_poc05_core
    canvas_poc03_core
    gtest_main
  )
  if(MSVC)
    target_compile_options(canvas_poc05_tests PRIVATE /W4 /permissive- /WX /EHsc)
  else()
    target_compile_options(canvas_poc05_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
  include(GoogleTest)
  gtest_discover_tests(canvas_poc05_tests)
endif()

if(ANDROID)
  add_subdirectory(platform/react_native/android/native)
endif()
