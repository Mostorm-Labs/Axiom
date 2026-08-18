add_library(canvas_poc02_windows_adapter STATIC windows_d3d12_adapter.cpp)
target_include_directories(canvas_poc02_windows_adapter
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${POC02_ROOT}/platform/skia
)
target_compile_definitions(canvas_poc02_windows_adapter PRIVATE
  NOMINMAX WIN32_LEAN_AND_MEAN
)
target_link_libraries(canvas_poc02_windows_adapter PUBLIC
  canvas_poc02_skia d3d12 d3dcompiler dxgi dxguid
)

add_executable(canvas_poc02_windows windows_demo.cpp)
target_include_directories(canvas_poc02_windows PRIVATE
  ${POC02_ROOT}/platform/skia
)
target_compile_definitions(canvas_poc02_windows PRIVATE
  NOMINMAX WIN32_LEAN_AND_MEAN
  CANVAS_POC02_FIXTURE_DIR=L"${POC02_ROOT}/fixtures"
  CANVAS_POC02_GOLDEN_DIR=L"${POC02_ROOT}/goldens"
)
target_link_libraries(canvas_poc02_windows PRIVATE
  canvas_poc02_windows_adapter user32 gdi32 shell32
)
