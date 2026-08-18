include(GoogleTest)

add_executable(canvas_poc02_tests
  document_replay_test.cpp
  frame_scheduler_test.cpp
  ink_engine_test.cpp
  input_queue_test.cpp
  preview_queue_test.cpp
  replay_fixture_test.cpp
  test_support.cpp
)
target_include_directories(canvas_poc02_tests PRIVATE
  ${POC02_ROOT}/src
  ${POC02_DEPS_ROOT}/nlohmann-json/single_include
)
target_compile_definitions(canvas_poc02_tests PRIVATE
  CANVAS_POC02_FIXTURE_DIR="${POC02_ROOT}/fixtures"
)
target_link_libraries(canvas_poc02_tests PRIVATE
  canvas_poc02_ink
  GTest::gtest_main
)
gtest_discover_tests(canvas_poc02_tests DISCOVERY_TIMEOUT 30)

if(TARGET canvas_poc02_replay)
  add_test(NAME canvas_poc02_determinism_10
    COMMAND canvas_poc02_replay --repeat=10)
  set_tests_properties(canvas_poc02_determinism_10 PROPERTIES
    PASS_REGULAR_EXPRESSION "\"repeat\":10")
endif()
