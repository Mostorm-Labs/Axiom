if(NOT ANDROID)
  message(FATAL_ERROR "POC-04 Android adapter requires the Android toolchain")
endif()

add_library(canvas_poc04_android SHARED android_jni.cpp)
target_link_libraries(canvas_poc04_android PRIVATE canvas_poc04_core log android)
if(TARGET canvas_poc04_canonical_behavior_fixture)
  target_compile_definitions(canvas_poc04_android PRIVATE
    CANVAS_POC04_HAS_CANONICAL_BEHAVIOR=1)
  target_link_libraries(canvas_poc04_android PRIVATE
    canvas_poc04_canonical_behavior_fixture)
endif()
