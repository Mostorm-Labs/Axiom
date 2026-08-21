add_library(canvas_runtime_scene_fakes STATIC
    src/fake_render_scene.cpp
    src/fake_spatial_index.cpp)
target_include_directories(canvas_runtime_scene_fakes PUBLIC include)
target_link_libraries(canvas_runtime_scene_fakes PUBLIC canvas_runtime_scene)

if(MSVC)
    target_compile_options(canvas_runtime_scene_fakes PRIVATE /W4 /permissive- /WX /EHsc)
else()
    target_compile_options(canvas_runtime_scene_fakes PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
