include(GoogleTest)

add_executable(canvas_poc01_tests
  c_api_test.cpp
  conformance_test.cpp
  document_test.cpp
  operations_test.cpp
  scene_compiler_test.cpp
  test_fixture.cpp
)
target_include_directories(canvas_poc01_tests PRIVATE
  ${POC01_ROOT}/src
  ${POC01_DEPS_ROOT}/nlohmann-json/single_include
)
target_link_libraries(canvas_poc01_tests PRIVATE
  canvas_poc01_core
  GTest::gtest_main
)
gtest_discover_tests(canvas_poc01_tests DISCOVERY_TIMEOUT 30)

if(TARGET canvas_poc01_skia AND NOT EMSCRIPTEN AND NOT ANDROID AND
   NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
  add_executable(canvas_poc01_skia_tests skia_renderer_test.cpp test_fixture.cpp)
  target_include_directories(canvas_poc01_skia_tests PRIVATE
    ${POC01_ROOT}/src
    ${POC01_ROOT}/platform/skia
  )
  target_compile_definitions(canvas_poc01_skia_tests PRIVATE
    CANVAS_POC01_FIXTURE_DIR="${POC01_ROOT}/fixtures"
    CANVAS_POC01_FONT_PATH="${CANVAS_POC01_SKIA_FONT_PATH}"
  )
  target_link_libraries(canvas_poc01_skia_tests PRIVATE
    canvas_poc01_skia GTest::gtest_main
  )
  gtest_discover_tests(canvas_poc01_skia_tests DISCOVERY_TIMEOUT 30)
endif()

if(TARGET canvas_poc01_cli)
  add_test(
    NAME canvas_poc01_determinism_10
    COMMAND canvas_poc01_cli --lifecycle=10
            --font=${POC01_ROOT}/tests/data/test-font.bin
  )
  set_tests_properties(canvas_poc01_determinism_10 PROPERTIES
    PASS_REGULAR_EXPRESSION "\\\"lifecycle\\\":10"
  )
endif()
