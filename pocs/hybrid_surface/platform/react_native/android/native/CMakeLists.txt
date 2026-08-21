add_library(canvas_poc05_android SHARED
  android_rn_jni.cpp
  ${PROJECT_SOURCE_DIR}/pocs/large_scene/platform/android/android_gles_adapter.cpp)

target_include_directories(canvas_poc05_android PRIVATE
  ${PROJECT_SOURCE_DIR}/pocs/large_scene/platform/android
  ${PROJECT_SOURCE_DIR}/pocs/large_scene/platform/skia)

target_link_libraries(canvas_poc05_android PRIVATE
  canvas_poc05_core
  canvas_poc03_skia
  android
  EGL
  GLESv3
  log)

target_compile_options(canvas_poc05_android PRIVATE
  -Wall -Wextra -Wpedantic -Werror)
target_link_options(canvas_poc05_android PRIVATE
  "-Wl,-z,max-page-size=16384"
  "-Wl,-z,common-page-size=16384")

# The React Native New Architecture loads libappmodules.so to install the
# generated TurboModule and Fabric component providers. The POC builds Canvas
# from the repository root CMake project, so the React Native Gradle plugin
# does not create that application glue target implicitly.
if(DEFINED REACT_ANDROID_DIR AND DEFINED PROJECT_BUILD_DIR)
  add_subdirectory(
    ${CMAKE_CURRENT_LIST_DIR}/rn_appmodules
    ${CMAKE_CURRENT_BINARY_DIR}/rn_appmodules)
  add_dependencies(canvas_poc05_android appmodules)
endif()
