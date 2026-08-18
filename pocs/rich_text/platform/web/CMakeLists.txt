if(NOT EMSCRIPTEN)
  message(FATAL_ERROR "POC-04 Web adapter requires Emscripten")
endif()

add_executable(canvas_poc04_web web_adapter.cpp)
target_link_libraries(canvas_poc04_web PRIVATE canvas_poc04_core)
set(_poc04_web_exports
  "['_malloc','_free','_canvas_poc04_web_create','_canvas_poc04_web_destroy','_canvas_poc04_web_focus','_canvas_poc04_web_blur','_canvas_poc04_web_begin_composition','_canvas_poc04_web_update_composition','_canvas_poc04_web_update_composition_with_selection','_canvas_poc04_web_commit_composition','_canvas_poc04_web_cancel_composition','_canvas_poc04_web_insert','_canvas_poc04_web_delete_surrounding','_canvas_poc04_web_undo','_canvas_poc04_web_redo','_canvas_poc04_web_digest']")
if(TARGET canvas_poc04_canonical_behavior_fixture)
  target_link_libraries(canvas_poc04_web PRIVATE
    canvas_poc04_canonical_behavior_fixture)
  target_compile_definitions(canvas_poc04_web PRIVATE
    CANVAS_POC04_FONT_PATH="${CANVAS_POC04_FONT_PATH}"
    CANVAS_POC04_CJK_FONT_PATH="${CANVAS_POC04_CJK_FONT_PATH}")
  target_link_options(canvas_poc04_web PRIVATE
    "SHELL:--preload-file ${CANVAS_POC04_FONT_PATH}@/fonts/Roboto-Regular.ttf"
    "SHELL:--preload-file ${CANVAS_POC04_CJK_FONT_PATH}@/fonts/NotoSansCJK-VF-subset.otf.ttc")
  set(_poc04_web_exports
    "['_malloc','_free','_canvas_poc04_web_create','_canvas_poc04_web_destroy','_canvas_poc04_web_focus','_canvas_poc04_web_blur','_canvas_poc04_web_begin_composition','_canvas_poc04_web_update_composition','_canvas_poc04_web_update_composition_with_selection','_canvas_poc04_web_commit_composition','_canvas_poc04_web_cancel_composition','_canvas_poc04_web_insert','_canvas_poc04_web_delete_surrounding','_canvas_poc04_web_undo','_canvas_poc04_web_redo','_canvas_poc04_web_digest','_canvas_poc04_web_canonical_behavior_report']")
endif()
target_link_options(canvas_poc04_web PRIVATE
  "SHELL:-s MODULARIZE=1"
  "SHELL:-s EXPORT_ES6=1"
  "SHELL:-s EXPORT_NAME=CanvasPoc04"
  "SHELL:-s ENVIRONMENT=web"
  "SHELL:-s ALLOW_MEMORY_GROWTH=1"
  "SHELL:-s EXPORTED_RUNTIME_METHODS=['UTF8ToString','stringToUTF8','lengthBytesUTF8']"
  "SHELL:-s EXPORTED_FUNCTIONS=${_poc04_web_exports}"
  "SHELL:-s USE_PTHREADS=0"
  --no-entry)
set_target_properties(canvas_poc04_web PROPERTIES SUFFIX ".js")
