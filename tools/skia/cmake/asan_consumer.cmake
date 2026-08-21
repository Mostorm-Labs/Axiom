# Inject the sanitizer contract required by a source-free Full Skia ASan SDK.
# This file is loaded through CMAKE_PROJECT_INCLUDE only for the producer's
# explicit ASan consumer validation. That hook runs after compiler detection,
# which is required to distinguish clang-cl from Unix-style Clang. Historical
# POC sanitizer presets keep their existing platform behavior.

if(NOT CANVAS_SKIA_SDK_ASAN_CONSUMER)
  message(FATAL_ERROR
    "asan_consumer.cmake requires CANVAS_SKIA_SDK_ASAN_CONSUMER=ON")
endif()

if(CANVAS_SKIA_SDK_ASAN_CONSUMER_CONFIGURED)
  return()
endif()
set(CANVAS_SKIA_SDK_ASAN_CONSUMER_CONFIGURED TRUE CACHE INTERNAL
    "Full Skia SDK ASan consumer flags were injected")

if(MSVC)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "Full Skia SDK ASan validation requires clang-cl")
  endif()
  # Match Skia's Windows GN ASan configuration: clang-cl links the dynamic
  # sanitizer runtime and the SDK supplies its matching DLL. The historical
  # POC-01 static-runtime sanitizer path remains separate and unchanged.
  add_compile_options(/fsanitize=address /Oy-)
  add_link_options(/fsanitize=address /INCREMENTAL:NO)
else()
  add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address)
endif()
