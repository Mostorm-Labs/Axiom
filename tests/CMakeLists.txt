find_package(GTest CONFIG REQUIRED)
include(GoogleTest)

add_test(NAME canvas_webview2_restore_script_contract
         COMMAND ${CMAKE_COMMAND}
                 -DRESTORE_SCRIPT=${CMAKE_SOURCE_DIR}/scripts/Restore-WebView2.ps1
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/scripts/restore_webview2_contract_test.cmake)

add_test(NAME canvas_webview2_initial_load_fail_source_contract
         COMMAND ${CMAKE_COMMAND}
                 -DSURFACE_SOURCE=${CMAKE_SOURCE_DIR}/src/platform/windows/webview2_surface.cpp
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/scripts/webview2_initial_load_fail_contract_test.cmake)

add_test(NAME canvas_webview2_navigation_source_contract
         COMMAND ${CMAKE_COMMAND}
                 -DSURFACE_SOURCE=${CMAKE_SOURCE_DIR}/src/platform/windows/webview2_surface.cpp
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/scripts/webview2_navigation_source_contract_test.cmake)

add_test(NAME canvas_windows_atomic_open_contract
         COMMAND ${CMAKE_COMMAND}
                 -DAPP_SOURCE=${CMAKE_SOURCE_DIR}/app/windows/whiteboard_app.cpp
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/scripts/windows_atomic_open_contract_test.cmake)

add_test(NAME canvas_macos_pointer_seam_source_contract
         COMMAND ${CMAKE_COMMAND}
                 -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/contracts/macos_pointer_seam_source_contract_test.cmake)

add_executable(canvas_version_test unit/version_test.cpp)
target_link_libraries(canvas_version_test PRIVATE canvas::core GTest::gtest_main)
canvas_enable_warnings(canvas_version_test)
gtest_discover_tests(canvas_version_test)

add_executable(canvas_ipc_protocol_test unit/ipc_protocol_test.cpp)
target_link_libraries(canvas_ipc_protocol_test PRIVATE canvas::ipc
                      GTest::gtest_main)
canvas_enable_warnings(canvas_ipc_protocol_test)
gtest_discover_tests(canvas_ipc_protocol_test)

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
gtest_discover_tests(canvas_document_codec_test
                     TEST_PREFIX "canvas_document_codec_test.")

add_executable(canvas_stroke_builder_test unit/stroke_builder_test.cpp)
target_link_libraries(canvas_stroke_builder_test PRIVATE canvas::core GTest::gtest_main)
canvas_enable_warnings(canvas_stroke_builder_test)
gtest_discover_tests(canvas_stroke_builder_test)

add_executable(canvas_input_router_test unit/input_router_test.cpp)
target_link_libraries(canvas_input_router_test PRIVATE canvas::core GTest::gtest_main)
canvas_enable_warnings(canvas_input_router_test)
gtest_discover_tests(canvas_input_router_test)

add_executable(canvas_whiteboard_lifecycle_test
               unit/whiteboard_lifecycle_test.cpp)
target_include_directories(canvas_whiteboard_lifecycle_test
                           PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(canvas_whiteboard_lifecycle_test
                      PRIVATE GTest::gtest_main)
canvas_enable_warnings(canvas_whiteboard_lifecycle_test)
gtest_discover_tests(canvas_whiteboard_lifecycle_test)

add_executable(canvas_embedded_load_tracker_test
               unit/embedded_load_tracker_test.cpp)
target_link_libraries(canvas_embedded_load_tracker_test
                      PRIVATE canvas::core GTest::gtest_main)
canvas_enable_warnings(canvas_embedded_load_tracker_test)
gtest_discover_tests(canvas_embedded_load_tracker_test)

add_executable(canvas_embedded_load_batch_test
               unit/embedded_load_batch_test.cpp)
target_link_libraries(canvas_embedded_load_batch_test
                      PRIVATE canvas::core GTest::gtest_main)
canvas_enable_warnings(canvas_embedded_load_batch_test)
gtest_discover_tests(canvas_embedded_load_batch_test)

add_executable(canvas_embedded_load_completion_inbox_test
               unit/embedded_load_completion_inbox_test.cpp)
target_link_libraries(canvas_embedded_load_completion_inbox_test
                      PRIVATE canvas::core GTest::gtest_main)
canvas_enable_warnings(canvas_embedded_load_completion_inbox_test)
gtest_discover_tests(canvas_embedded_load_completion_inbox_test)

add_executable(canvas_embedded_document_open_coordinator_test
               unit/embedded_document_open_coordinator_test.cpp)
target_link_libraries(canvas_embedded_document_open_coordinator_test
                      PRIVATE canvas::core GTest::gtest_main)
canvas_enable_warnings(canvas_embedded_document_open_coordinator_test)
gtest_discover_tests(canvas_embedded_document_open_coordinator_test)

add_executable(canvas_webview2_initial_load_seam_test
               unit/webview2_initial_load_seam_test.cpp)
target_include_directories(canvas_webview2_initial_load_seam_test
                           PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(canvas_webview2_initial_load_seam_test
                      PRIVATE GTest::gtest_main)
canvas_enable_warnings(canvas_webview2_initial_load_seam_test)
gtest_discover_tests(canvas_webview2_initial_load_seam_test)

add_executable(canvas_frame_invalidation_test
               unit/frame_invalidation_test.cpp)
target_include_directories(canvas_frame_invalidation_test
                           PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(canvas_frame_invalidation_test PRIVATE GTest::gtest_main)
canvas_enable_warnings(canvas_frame_invalidation_test)
gtest_discover_tests(canvas_frame_invalidation_test)

if(APPLE)
    add_executable(canvas_macos_pointer_adapter_test
                   unit/macos_pointer_adapter_test.cpp)
    target_include_directories(canvas_macos_pointer_adapter_test
                               PRIVATE ${CMAKE_SOURCE_DIR}/include
                                       ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(canvas_macos_pointer_adapter_test
                          PRIVATE canvas::macos_platform GTest::gtest_main)
    canvas_enable_warnings(canvas_macos_pointer_adapter_test)
    gtest_discover_tests(canvas_macos_pointer_adapter_test)

    add_executable(canvas_macos_mouse_session_test
                   unit/macos_mouse_session_test.cpp)
    target_include_directories(canvas_macos_mouse_session_test
                               PRIVATE ${CMAKE_SOURCE_DIR}/include
                                       ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(canvas_macos_mouse_session_test
                          PRIVATE canvas::macos_platform GTest::gtest_main)
    canvas_enable_warnings(canvas_macos_mouse_session_test)
    gtest_discover_tests(canvas_macos_mouse_session_test)

    add_executable(canvas_macos_whiteboard_input_test
                   unit/macos_whiteboard_input_test.cpp)
    target_include_directories(canvas_macos_whiteboard_input_test
                               PRIVATE ${CMAKE_SOURCE_DIR}/include
                                       ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(canvas_macos_whiteboard_input_test
                          PRIVATE canvas::macos_platform GTest::gtest_main)
    canvas_enable_warnings(canvas_macos_whiteboard_input_test)
    gtest_discover_tests(canvas_macos_whiteboard_input_test)

    add_executable(canvas_macos_wkwebview_navigation_seam_test
                   unit/macos_wkwebview_navigation_seam_test.cpp)
    target_include_directories(canvas_macos_wkwebview_navigation_seam_test
                               PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(canvas_macos_wkwebview_navigation_seam_test
                          PRIVATE GTest::gtest_main)
    canvas_enable_warnings(canvas_macos_wkwebview_navigation_seam_test)
    gtest_discover_tests(canvas_macos_wkwebview_navigation_seam_test)

    add_executable(canvas_macos_skia_frame_plan_test
                   unit/macos_skia_frame_plan_test.cpp)
    target_include_directories(canvas_macos_skia_frame_plan_test
                               PRIVATE ${CMAKE_SOURCE_DIR}/include
                                       ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(canvas_macos_skia_frame_plan_test
                          PRIVATE GTest::gtest_main)
    canvas_enable_warnings(canvas_macos_skia_frame_plan_test)
    gtest_discover_tests(canvas_macos_skia_frame_plan_test)

    add_executable(canvas_macos_appkit_frame_scheduling_test
                   integration/macos_appkit_frame_scheduling_test.mm)
    target_include_directories(canvas_macos_appkit_frame_scheduling_test
                               PRIVATE ${CMAKE_SOURCE_DIR}/include
                                       ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(canvas_macos_appkit_frame_scheduling_test
                          PRIVATE canvas::macos_platform
                                  canvas::core
                                  GTest::gtest_main)
    target_compile_options(canvas_macos_appkit_frame_scheduling_test
                           PRIVATE -fobjc-arc)
    canvas_enable_warnings(canvas_macos_appkit_frame_scheduling_test)
    gtest_discover_tests(
        canvas_macos_appkit_frame_scheduling_test
        PROPERTIES TIMEOUT 15 ENVIRONMENT "MTL_DEBUG_LAYER=1")

    add_executable(canvas_macos_composition_layer_stack_test
                   integration/macos_composition_layer_stack_test.mm)
    target_include_directories(canvas_macos_composition_layer_stack_test
                               PRIVATE ${CMAKE_SOURCE_DIR}/include
                                       ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(canvas_macos_composition_layer_stack_test
                          PRIVATE canvas::macos_platform
                                  canvas::core
                                  GTest::gtest_main)
    target_compile_options(canvas_macos_composition_layer_stack_test
                           PRIVATE -fobjc-arc)
    canvas_enable_warnings(canvas_macos_composition_layer_stack_test)
    gtest_discover_tests(
        canvas_macos_composition_layer_stack_test
        PROPERTIES TIMEOUT 20 ENVIRONMENT "MTL_DEBUG_LAYER=1")

    add_executable(canvas_macos_mouse_input_test
                   integration/macos_mouse_input_test.mm)
    target_include_directories(canvas_macos_mouse_input_test
                               PRIVATE ${CMAKE_SOURCE_DIR}/include
                                       ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(canvas_macos_mouse_input_test
                          PRIVATE canvas::macos_platform
                                  canvas::core
                                  GTest::gtest_main)
    target_compile_options(canvas_macos_mouse_input_test PRIVATE -fobjc-arc)
    canvas_enable_warnings(canvas_macos_mouse_input_test)
    gtest_discover_tests(
        canvas_macos_mouse_input_test
        PROPERTIES TIMEOUT 20 RUN_SERIAL TRUE
                   ENVIRONMENT "MTL_DEBUG_LAYER=1")

    add_executable(canvas_macos_wkwebview_surface_test
                   integration/macos_wkwebview_surface_test.mm)
    target_include_directories(canvas_macos_wkwebview_surface_test
                               PRIVATE ${CMAKE_SOURCE_DIR}/include
                                       ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(canvas_macos_wkwebview_surface_test
                          PRIVATE canvas::macos_platform
                                  canvas::core
                                  GTest::gtest_main)
    target_compile_options(canvas_macos_wkwebview_surface_test
                           PRIVATE -fobjc-arc)
    target_compile_definitions(canvas_macos_wkwebview_surface_test
                               PRIVATE
                               CANVAS_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    canvas_enable_warnings(canvas_macos_wkwebview_surface_test)
    gtest_discover_tests(
        canvas_macos_wkwebview_surface_test
        PROPERTIES TIMEOUT 20 RUN_SERIAL TRUE
                   ENVIRONMENT "MTL_DEBUG_LAYER=1")

    # This source-level contract runs without a GPU. The runtime ownership
    # violation deliberately terminates, so it is asserted structurally here.
    add_test(NAME canvas_macos_metal_host_thread_contract
             COMMAND ${CMAKE_COMMAND}
                     -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/contracts/macos_metal_host_thread_contract_test.cmake)

    add_test(NAME canvas_macos_skia_host_contract
             COMMAND ${CMAKE_COMMAND}
                     -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/contracts/macos_skia_host_contract_test.cmake)

    add_test(NAME canvas_macos_composition_host_contract
             COMMAND ${CMAKE_COMMAND}
                     -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/contracts/macos_composition_host_contract_test.cmake)

    add_test(NAME canvas_macos_wkwebview_surface_contract
             COMMAND ${CMAKE_COMMAND}
                     -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/contracts/macos_wkwebview_surface_contract_test.cmake)

endif()

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
    get_target_property(_canvas_windows_platform_interface_links
                        canvas_windows_platform INTERFACE_LINK_LIBRARIES)
    if(NOT "$<LINK_ONLY:d3dcompiler>" IN_LIST
           _canvas_windows_platform_interface_links)
        message(FATAL_ERROR
            "canvas_windows_platform must propagate d3dcompiler as LINK_ONLY")
    endif()
    unset(_canvas_windows_platform_interface_links)

    add_library(canvas_windows_header_hygiene_compile_test OBJECT
                compile/windows_header_hygiene_compile_test.cpp)
    target_link_libraries(canvas_windows_header_hygiene_compile_test
                          PRIVATE unofficial::skia::skia)
    canvas_enable_warnings(canvas_windows_header_hygiene_compile_test)

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

    add_executable(canvas_webview2_navigation_uri_test
                   unit/webview2_navigation_uri_test.cpp)
    target_include_directories(canvas_webview2_navigation_uri_test
                               PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(canvas_webview2_navigation_uri_test
                          PRIVATE urlmon oleaut32 GTest::gtest_main)
    canvas_enable_warnings(canvas_webview2_navigation_uri_test)
    gtest_discover_tests(canvas_webview2_navigation_uri_test)

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

    add_executable(canvas_webview2_video_restore_test
                   unit/webview2_video_restore_test.cpp)
    target_include_directories(canvas_webview2_video_restore_test
                               PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(canvas_webview2_video_restore_test
                          PRIVATE canvas::windows_platform GTest::gtest_main)
    canvas_enable_warnings(canvas_webview2_video_restore_test)
    gtest_discover_tests(canvas_webview2_video_restore_test
                         TEST_PREFIX "canvas_webview2_video_restore_test.")

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

    add_executable(canvas_named_pipe_server_test
                   integration/named_pipe_server_test.cpp)
    target_include_directories(canvas_named_pipe_server_test
                               PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(canvas_named_pipe_server_test
                          PRIVATE canvas::windows_platform GTest::gtest_main)
    canvas_enable_warnings(canvas_named_pipe_server_test)
    gtest_discover_tests(canvas_named_pipe_server_test
                         TEST_PREFIX "canvas_named_pipe_server_test.")

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
