add_executable(canvas_poc02_web web_adapter.cpp)
target_include_directories(canvas_poc02_web PRIVATE
  ${POC02_ROOT}/src
  ${POC02_ROOT}/platform/skia
)
target_link_libraries(canvas_poc02_web PRIVATE canvas_poc02_skia)
target_compile_options(canvas_poc02_web PRIVATE -fexceptions)
set_target_properties(canvas_poc02_web PROPERTIES SUFFIX ".js")
target_link_options(canvas_poc02_web PRIVATE
  -fexceptions
  "-sALLOW_MEMORY_GROWTH=1"
  "-sENVIRONMENT=web"
  "-sEXPORT_ES6=1"
  "-sMODULARIZE=1"
  "-sEXPORT_NAME=createCanvasPoc02Module"
  "-sFILESYSTEM=0"
  "-sNO_EXIT_RUNTIME=1"
  "-sUSE_WEBGL2=1"
  "-sMAX_WEBGL_VERSION=2"
  "-sMIN_WEBGL_VERSION=2"
  "-sGL_SUPPORT_AUTOMATIC_ENABLE_EXTENSIONS=0"
  "-sEXPORTED_FUNCTIONS=['_malloc','_free','_canvas_poc02_reset','_canvas_poc02_replay','_canvas_poc02_surface_create','_canvas_poc02_render','_canvas_poc02_readback','_canvas_poc02_compare_golden','_canvas_poc02_last_error','_canvas_poc02_begin','_canvas_poc02_push_batch','_canvas_poc02_end','_canvas_poc02_cancel','_canvas_poc02_visible','_canvas_poc02_document_digest','_canvas_poc02_stroke_digest','_canvas_poc02_preview_digest','_canvas_poc02_numeric_digest','_canvas_poc02_event_count','_canvas_poc02_confirmed_count']"
  "-sEXPORTED_RUNTIME_METHODS=['UTF8ToString','getExceptionMessage','HEAPU8','HEAPU32','HEAPF32']"
)
