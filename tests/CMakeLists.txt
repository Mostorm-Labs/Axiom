find_package(GTest CONFIG REQUIRED)
include(GoogleTest)

add_executable(canvas_version_test unit/version_test.cpp)
target_link_libraries(canvas_version_test PRIVATE canvas::core GTest::gtest_main)
canvas_enable_warnings(canvas_version_test)
gtest_discover_tests(canvas_version_test)

add_executable(canvas_geometry_test unit/geometry_test.cpp)
target_link_libraries(canvas_geometry_test PRIVATE canvas::core GTest::gtest_main)
canvas_enable_warnings(canvas_geometry_test)
gtest_discover_tests(canvas_geometry_test)

add_executable(canvas_document_test unit/document_test.cpp)
target_link_libraries(canvas_document_test PRIVATE canvas::core GTest::gtest_main)
canvas_enable_warnings(canvas_document_test)
gtest_discover_tests(canvas_document_test)
