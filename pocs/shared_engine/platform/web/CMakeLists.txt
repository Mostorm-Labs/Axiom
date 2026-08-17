add_executable(canvas_poc01_web web_adapter.cpp)
target_include_directories(canvas_poc01_web PRIVATE
  ${POC01_ROOT}/src
  ${POC01_ROOT}/platform/skia
)
target_link_libraries(canvas_poc01_web PRIVATE canvas_poc01_skia)
set_target_properties(canvas_poc01_web PROPERTIES SUFFIX ".js")
target_link_options(canvas_poc01_web PRIVATE
  "--no-entry"
  "-sALLOW_MEMORY_GROWTH=1"
  "-sENVIRONMENT=web"
  "-sFILESYSTEM=0"
  "-sMODULARIZE=1"
  "-sEXPORT_NAME=createCanvasPocModule"
  "-sEXPORT_ES6=1"
  "-sNO_EXIT_RUNTIME=1"
  "-sMIN_WEBGL_VERSION=2"
  "-sMAX_WEBGL_VERSION=2"
  "-sGL_SUPPORT_AUTOMATIC_ENABLE_EXTENSIONS=0"
  "-sEXPORTED_FUNCTIONS=['_malloc','_free','_canvas_poc_web_reset','_canvas_poc_web_load_assets','_canvas_poc_web_replay','_canvas_poc_web_digest','_canvas_poc_web_surface_create','_canvas_poc_web_render','_canvas_poc_web_readback','_canvas_poc_web_compare_golden','_canvas_poc_web_backend','_canvas_poc_last_error']"
  "-sEXPORTED_RUNTIME_METHODS=['UTF8ToString','HEAPU8','HEAPU32','HEAPF64']"
)
