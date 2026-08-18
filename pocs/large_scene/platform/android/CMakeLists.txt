add_library(canvas_poc03_android SHARED
  android_gles_adapter.cpp
  android_jni.cpp)
target_include_directories(canvas_poc03_android PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${POC03_ROOT}/platform/skia)
target_link_libraries(canvas_poc03_android PRIVATE
  canvas_poc03_skia android EGL GLESv3 log)
target_compile_options(canvas_poc03_android PRIVATE -Wall -Wextra -Wpedantic)
target_link_options(canvas_poc03_android PRIVATE
  "-Wl,-z,max-page-size=16384"
  "-Wl,-z,common-page-size=16384")
