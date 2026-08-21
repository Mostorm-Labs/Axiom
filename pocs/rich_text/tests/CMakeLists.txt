include(GoogleTest)

add_executable(canvas_poc04_tests
  c_api_test.cpp
  font_resources_test.cpp
  text_document_test.cpp
  text_edit_session_test.cpp
  text_layout_test.cpp
  text_operations_test.cpp
)
target_include_directories(canvas_poc04_tests PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../src")
target_link_libraries(canvas_poc04_tests PRIVATE
  canvas_poc04_core GTest::gtest_main)
gtest_discover_tests(canvas_poc04_tests DISCOVERY_TIMEOUT 30)

if(TARGET canvas_poc04_skparagraph)
  add_executable(canvas_poc04_skparagraph_tests skparagraph_layout_test.cpp)
  target_link_libraries(canvas_poc04_skparagraph_tests PRIVATE
    canvas_poc04_skparagraph GTest::gtest_main)
  target_compile_definitions(canvas_poc04_skparagraph_tests PRIVATE
    CANVAS_POC04_FONT_PATH="${CANVAS_POC04_FONT_PATH}"
    CANVAS_POC04_CJK_FONT_PATH="${CANVAS_POC04_CJK_FONT_PATH}")
  if(WIN32 AND CANVAS_POC04_ICU_DATA_PATH)
    add_custom_command(TARGET canvas_poc04_skparagraph_tests POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CANVAS_POC04_ICU_DATA_PATH}"
        "$<TARGET_FILE_DIR:canvas_poc04_skparagraph_tests>/icudtl.dat")
  endif()
  gtest_discover_tests(canvas_poc04_skparagraph_tests DISCOVERY_TIMEOUT 30)
endif()
