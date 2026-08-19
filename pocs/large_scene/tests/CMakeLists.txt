include(GoogleTest)

add_executable(canvas_poc03_tests large_scene_test.cpp)
target_link_libraries(canvas_poc03_tests PRIVATE
  canvas_poc03_core GTest::gtest_main)
if(TARGET canvas_poc03_rf01_adapter)
  target_sources(canvas_poc03_tests PRIVATE rf01_scene_source_test.cpp)
  target_link_libraries(canvas_poc03_tests PRIVATE canvas_poc03_rf01_adapter)
endif()
if(CANVAS_POC03_ENABLE_INK_INTEGRATION)
  target_sources(canvas_poc03_tests PRIVATE integrated_ink_test.cpp)
  target_link_libraries(canvas_poc03_tests PRIVATE
    canvas_poc03_ink_integration)
endif()
gtest_discover_tests(canvas_poc03_tests DISCOVERY_TIMEOUT 60)

add_test(
  NAME canvas_poc03_100k_acceptance
  COMMAND canvas_poc03_benchmark --nodes=100000 --frames=120 --updates=100)
set_tests_properties(canvas_poc03_100k_acceptance PROPERTIES
  TIMEOUT 120
  PASS_REGULAR_EXPRESSION "\"full_incremental_equivalent\": true")

if(CANVAS_POC03_ENABLE_INK_INTEGRATION)
  add_test(
    NAME canvas_poc03_integrated_1k_acceptance
    COMMAND canvas_poc03_integrated_benchmark --nodes=1000)
  set_tests_properties(canvas_poc03_integrated_1k_acceptance PROPERTIES
    TIMEOUT 120
    PASS_REGULAR_EXPRESSION "\"preview_canonical_handoff_frames\": 1")
endif()
