add_library(canvas_poc01_windows_adapter STATIC windows_d3d12_adapter.cpp)
target_include_directories(canvas_poc01_windows_adapter
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${POC01_ROOT}/src ${POC01_ROOT}/platform/skia
)
target_compile_definitions(canvas_poc01_windows_adapter PRIVATE
  NOMINMAX WIN32_LEAN_AND_MEAN
)
target_link_libraries(canvas_poc01_windows_adapter PUBLIC
  canvas_poc01_skia d3d12 d3dcompiler dxgi dxguid
)

add_executable(canvas_poc01_windows windows_demo.cpp)
target_include_directories(canvas_poc01_windows PRIVATE
  ${POC01_ROOT}/src ${POC01_ROOT}/platform/skia
)
target_compile_definitions(canvas_poc01_windows PRIVATE
  NOMINMAX WIN32_LEAN_AND_MEAN
  CANVAS_POC01_FIXTURE_DIR=L"${POC01_ROOT}/fixtures"
  CANVAS_POC01_FONT_PATH=L"${CANVAS_POC01_SKIA_FONT_PATH}"
)
target_link_libraries(canvas_poc01_windows PRIVATE
  canvas_poc01_windows_adapter user32 gdi32 shell32 psapi
)
