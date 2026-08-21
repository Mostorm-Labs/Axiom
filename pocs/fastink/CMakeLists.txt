set(POC06_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

add_library(canvas_poc06_poc02_adapter STATIC adapter/poc02_adapter.cpp)
target_include_directories(canvas_poc06_poc02_adapter
  PUBLIC adapter
  PRIVATE ${PROJECT_SOURCE_DIR}/pocs/ink_engine/include)
target_link_libraries(canvas_poc06_poc02_adapter PUBLIC Arc::Core canvas_poc02_ink)

add_executable(canvas_poc06_headless harness/headless_harness.cpp)
target_include_directories(canvas_poc06_headless PRIVATE
  ${PROJECT_SOURCE_DIR}/pocs/ink_engine/include)
target_link_libraries(canvas_poc06_headless PRIVATE
  canvas_poc06_poc02_adapter Arc::Headless canvas_poc02_ink)
target_compile_definitions(canvas_poc06_headless PRIVATE
  CANVAS_POC06_FIXTURE_DIR="${POC06_ROOT}/fixtures")

if(MSVC)
  target_compile_options(canvas_poc06_poc02_adapter PRIVATE /W4 /permissive- /EHsc)
  target_compile_options(canvas_poc06_headless PRIVATE /W4 /permissive- /EHsc)
else()
  target_compile_options(canvas_poc06_poc02_adapter PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(canvas_poc06_headless PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
  add_executable(canvas_poc06_adapter_tests tests/poc02_adapter_test.cpp)
  target_include_directories(canvas_poc06_adapter_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/pocs/ink_engine/include)
  target_link_libraries(canvas_poc06_adapter_tests PRIVATE
    canvas_poc06_poc02_adapter Arc::Headless canvas_poc02_ink)
  add_test(NAME canvas_poc06_adapter_tests COMMAND canvas_poc06_adapter_tests)
  add_test(NAME canvas_poc06_headless COMMAND canvas_poc06_headless)
  add_executable(canvas_poc06_fault_lifecycle_test tests/fault_lifecycle_test.cpp)
  target_link_libraries(canvas_poc06_fault_lifecycle_test PRIVATE Arc::Core Arc::Headless)
  add_test(NAME canvas_poc06_fault_lifecycle COMMAND canvas_poc06_fault_lifecycle_test)
endif()
