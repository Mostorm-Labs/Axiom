add_library(canvas_poc01_android SHARED
  android_gles_adapter.cpp
  android_jni.cpp
)
target_include_directories(canvas_poc01_android PRIVATE
  ${POC01_ROOT}/src
  ${POC01_ROOT}/platform/skia
  ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(canvas_poc01_android PRIVATE
  canvas_poc01_skia
  android
  EGL
  GLESv3
  log
)
target_compile_options(canvas_poc01_android PRIVATE -Wall -Wextra -Wpedantic)
