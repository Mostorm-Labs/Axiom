add_library(canvas_poc03_rf01_adapter STATIC src/poc03_scene_source.cpp)
target_include_directories(canvas_poc03_rf01_adapter PUBLIC include)
target_link_libraries(canvas_poc03_rf01_adapter
    PUBLIC canvas_poc03_core canvas_runtime_scene)

if(MSVC)
    target_compile_options(canvas_poc03_rf01_adapter PRIVATE /W4 /permissive- /WX /EHsc)
else()
    target_compile_options(
        canvas_poc03_rf01_adapter PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
