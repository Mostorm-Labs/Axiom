add_executable(canvas_rf01_scene_atomicity_test scene_atomicity_test.cpp)
target_link_libraries(canvas_rf01_scene_atomicity_test PRIVATE canvas_runtime_scene_fakes)

if(MSVC)
    target_compile_options(canvas_rf01_scene_atomicity_test PRIVATE /W4 /permissive- /WX /EHsc)
else()
    target_compile_options(
        canvas_rf01_scene_atomicity_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_test(NAME canvas_rf01_scene_atomicity COMMAND canvas_rf01_scene_atomicity_test)

add_executable(canvas_rf01_full_rebuild_test scene_full_rebuild_test.cpp)
target_link_libraries(canvas_rf01_full_rebuild_test PRIVATE canvas_runtime_scene)

if(MSVC)
    target_compile_options(canvas_rf01_full_rebuild_test PRIVATE /W4 /permissive- /WX /EHsc)
else()
    target_compile_options(
        canvas_rf01_full_rebuild_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_test(NAME canvas_rf01_full_rebuild COMMAND canvas_rf01_full_rebuild_test)

add_executable(canvas_rf01_scene_binding_test scene_binding_test.cpp)
target_link_libraries(canvas_rf01_scene_binding_test PRIVATE canvas_runtime_scene_fakes)

if(MSVC)
    target_compile_options(canvas_rf01_scene_binding_test PRIVATE /W4 /permissive- /WX /EHsc)
else()
    target_compile_options(
        canvas_rf01_scene_binding_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_test(NAME canvas_rf01_scene_binding COMMAND canvas_rf01_scene_binding_test)

add_executable(canvas_rf01_damage_view_test damage_view_test.cpp)
target_link_libraries(canvas_rf01_damage_view_test PRIVATE canvas_runtime_scene)

if(MSVC)
    target_compile_options(canvas_rf01_damage_view_test PRIVATE /W4 /permissive- /WX /EHsc)
else()
    target_compile_options(
        canvas_rf01_damage_view_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_test(NAME canvas_rf01_damage_view COMMAND canvas_rf01_damage_view_test)

add_library(canvas_rf01_public_abi_c OBJECT public_abi_c_test.c)
target_include_directories(canvas_rf01_public_abi_c PRIVATE ${PROJECT_SOURCE_DIR})
set_target_properties(canvas_rf01_public_abi_c PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)

add_library(canvas_rf01_public_abi_cpp OBJECT public_abi_cpp_test.cpp)
target_include_directories(canvas_rf01_public_abi_cpp PRIVATE ${PROJECT_SOURCE_DIR})
target_compile_features(canvas_rf01_public_abi_cpp PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(canvas_rf01_public_abi_c PRIVATE /W4 /WX)
    target_compile_options(canvas_rf01_public_abi_cpp PRIVATE /W4 /permissive- /WX /EHsc)
else()
    target_compile_options(canvas_rf01_public_abi_c PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_options(
        canvas_rf01_public_abi_cpp PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

foreach(header IN ITEMS
        canvas/foundation/object_id.hpp
        canvas/foundation/result.hpp
        canvas/foundation/revision.hpp
        canvas/foundation/stable_order_key.hpp
        canvas/foundation/world_geometry.hpp
        canvas/scene/damage_tracker.hpp
        canvas/scene/direct_render_scene.hpp
        canvas/scene/render_scene.hpp
        canvas/scene/scene.hpp
        canvas/scene/scene_binding.hpp
        canvas/scene/scene_compiler.hpp
        canvas/scene/scene_frame.hpp
        canvas/scene/scene_query.hpp
        canvas/scene/scene_record_store.hpp
        canvas/scene/scene_types.hpp
        canvas/scene/scene_view.hpp
        canvas/scene/spatial_index.hpp
        canvas/scene/uniform_grid_spatial_index.hpp)
    string(REPLACE "/" "_" target_suffix ${header})
    string(REPLACE "." "_" target_suffix ${target_suffix})
    set(source ${CMAKE_CURRENT_BINARY_DIR}/self_contained_${target_suffix}.cpp)
    file(WRITE ${source} "#include \"${header}\"\n")
    add_library(canvas_rf01_${target_suffix} OBJECT ${source})
    target_link_libraries(canvas_rf01_${target_suffix} PRIVATE canvas_runtime_scene)
    if(MSVC)
        target_compile_options(
            canvas_rf01_${target_suffix} PRIVATE /W4 /permissive- /WX /EHsc)
    else()
        target_compile_options(
            canvas_rf01_${target_suffix} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endforeach()

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(
    NAME canvas_rf01_module_boundaries
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/check_runtime_boundaries.py
            --root ${PROJECT_SOURCE_DIR})
add_test(
    NAME canvas_rf01_public_abi_manifest
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/check_runtime_abi_manifest.py
            --root ${PROJECT_SOURCE_DIR})
