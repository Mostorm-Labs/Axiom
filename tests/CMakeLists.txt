find_package(GTest CONFIG REQUIRED)
include(GoogleTest)

add_test(NAME canvas_webview2_restore_script_contract
         COMMAND ${CMAKE_COMMAND}
                 -DRESTORE_SCRIPT=${CMAKE_SOURCE_DIR}/scripts/Restore-WebView2.ps1
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/scripts/restore_webview2_contract_test.cmake)

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

add_executable(canvas_document_codec_test unit/document_codec_test.cpp)
target_link_libraries(canvas_document_codec_test PRIVATE canvas::core
                      GTest::gtest_main)
target_include_directories(canvas_document_codec_test PRIVATE
                           ${CMAKE_SOURCE_DIR}/src)
if(WIN32)
    target_link_libraries(canvas_document_codec_test
                          PRIVATE canvas::windows_platform)
endif()
canvas_enable_warnings(canvas_document_codec_test)
gtest_discover_tests(canvas_document_codec_test)

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

if(WIN32)
    add_executable(canvas_embedded_mouse_session_test
                   unit/embedded_mouse_session_test.cpp)
    target_include_directories(canvas_embedded_mouse_session_test
                               PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(canvas_embedded_mouse_session_test
                          PRIVATE GTest::gtest_main)
    canvas_enable_warnings(canvas_embedded_mouse_session_test)
    gtest_discover_tests(canvas_embedded_mouse_session_test)

    add_executable(canvas_webview2_close_seam_test
                   unit/webview2_close_seam_test.cpp)
    target_include_directories(canvas_webview2_close_seam_test
                               PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(canvas_webview2_close_seam_test
                          PRIVATE GTest::gtest_main)
    canvas_enable_warnings(canvas_webview2_close_seam_test)
    gtest_discover_tests(canvas_webview2_close_seam_test)

    add_executable(canvas_webview2_virtual_host_path_test
                   unit/webview2_virtual_host_path_test.cpp)
    target_include_directories(canvas_webview2_virtual_host_path_test
                               PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(canvas_webview2_virtual_host_path_test
                          PRIVATE GTest::gtest_main)
    canvas_enable_warnings(canvas_webview2_virtual_host_path_test)
    gtest_discover_tests(canvas_webview2_virtual_host_path_test)

    add_executable(canvas_webview2_media_source_test
                   unit/webview2_media_source_test.cpp)
    target_include_directories(canvas_webview2_media_source_test
                               PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(canvas_webview2_media_source_test
                          PRIVATE GTest::gtest_main)
    canvas_enable_warnings(canvas_webview2_media_source_test)
    gtest_discover_tests(canvas_webview2_media_source_test)

    add_executable(canvas_webview2_message_log_test
                   unit/webview2_message_log_test.cpp)
    target_include_directories(canvas_webview2_message_log_test
                               PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(canvas_webview2_message_log_test
                          PRIVATE GTest::gtest_main)
    canvas_enable_warnings(canvas_webview2_message_log_test)
    gtest_discover_tests(canvas_webview2_message_log_test)

    add_executable(canvas_win_pointer_adapter_test
                   unit/win_pointer_adapter_test.cpp)
    target_include_directories(canvas_win_pointer_adapter_test
                               PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(canvas_win_pointer_adapter_test
                          PRIVATE canvas::windows_platform GTest::gtest_main)
    canvas_enable_warnings(canvas_win_pointer_adapter_test)
    gtest_discover_tests(canvas_win_pointer_adapter_test)

    add_executable(canvas_windows_composition_test
                   integration/windows_composition_test.cpp)
    target_link_libraries(canvas_windows_composition_test
                          PRIVATE canvas::windows_platform GTest::gtest_main)
    canvas_enable_warnings(canvas_windows_composition_test)
    gtest_discover_tests(canvas_windows_composition_test
                         TEST_PREFIX "canvas_windows_composition_test.")

    add_executable(canvas_webview2_surface_test
                   integration/webview2_surface_test.cpp)
    target_include_directories(canvas_webview2_surface_test
                               PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(canvas_webview2_surface_test
                          PRIVATE canvas::windows_platform
                                  canvas::webview2_loader
                                  GTest::gtest_main)
    canvas_enable_warnings(canvas_webview2_surface_test)
    gtest_discover_tests(canvas_webview2_surface_test
                         TEST_PREFIX "canvas_webview2_surface_test.")
endif()
