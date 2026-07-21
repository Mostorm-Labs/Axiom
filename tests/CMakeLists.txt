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

add_executable(canvas_stroke_builder_test unit/stroke_builder_test.cpp)
target_link_libraries(canvas_stroke_builder_test PRIVATE canvas::core GTest::gtest_main)
canvas_enable_warnings(canvas_stroke_builder_test)
gtest_discover_tests(canvas_stroke_builder_test)

add_executable(canvas_input_router_test unit/input_router_test.cpp)
target_link_libraries(canvas_input_router_test PRIVATE canvas::core GTest::gtest_main)
canvas_enable_warnings(canvas_input_router_test)
gtest_discover_tests(canvas_input_router_test)

add_executable(canvas_embedded_transform_test unit/embedded_transform_test.cpp)
target_link_libraries(canvas_embedded_transform_test PRIVATE canvas::core GTest::gtest_main)
canvas_enable_warnings(canvas_embedded_transform_test)
gtest_discover_tests(canvas_embedded_transform_test)

add_executable(canvas_embedded_surface_manager_test
               unit/embedded_surface_manager_test.cpp)
target_link_libraries(canvas_embedded_surface_manager_test
                      PRIVATE canvas::core GTest::gtest_main)
canvas_enable_warnings(canvas_embedded_surface_manager_test)
gtest_discover_tests(canvas_embedded_surface_manager_test)

add_executable(canvas_skia_renderer_test unit/skia_renderer_test.cpp)
target_link_libraries(canvas_skia_renderer_test PRIVATE canvas::core GTest::gtest_main)
canvas_enable_warnings(canvas_skia_renderer_test)
gtest_discover_tests(canvas_skia_renderer_test)
