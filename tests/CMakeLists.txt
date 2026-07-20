include(GoogleTest)

add_executable(canvas_version_test unit/version_test.cpp)
target_link_libraries(canvas_version_test PRIVATE canvas::core GTest::gtest_main)
canvas_enable_warnings(canvas_version_test)
gtest_discover_tests(canvas_version_test)
