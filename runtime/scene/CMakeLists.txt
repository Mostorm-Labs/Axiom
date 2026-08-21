add_library(canvas_runtime_scene STATIC
    src/damage_tracker.cpp
    src/direct_render_scene.cpp
    src/scene.cpp
    src/scene_binding.cpp
    src/scene_record_store.cpp
    src/scene_view.cpp
    src/shadow_render_scene.cpp
    src/uniform_grid_spatial_index.cpp)
target_include_directories(canvas_runtime_scene PUBLIC include)
target_link_libraries(canvas_runtime_scene PUBLIC canvas_runtime_foundation)
target_compile_features(canvas_runtime_scene PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(canvas_runtime_scene PRIVATE /W4 /permissive- /WX /EHsc)
else()
    target_compile_options(canvas_runtime_scene PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(CANVAS_RF01_BUILD_TESTS AND BUILD_TESTING)
    add_subdirectory(testing)
    add_subdirectory(tests)
endif()
