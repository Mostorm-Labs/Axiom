#pragma once

#include "platform/windows/dcomp_host.h"

#include "canvas/input/pointer_sample.h"
#include "canvas/document/document.h"
#include "canvas/input/input_router.h"
#include "canvas/stroke/stroke_builder.h"
#include "platform/windows/skia_d3d12_context.h"
#include "platform/windows/skia_swap_chain_layer.h"

#include <cstdint>
#include <optional>
#include <string>

namespace canvas::windows {

class WhiteboardApp {
 public:
  int run(HINSTANCE instance, int commandShow);

  static LRESULT CALLBACK windowProc(HWND window, UINT message,
                                     WPARAM wParam, LPARAM lParam);

 private:
  // Task 10 seam: samples stay native and are consumed by the future stroke
  // pipeline. No Electron IPC or rendering work belongs on this path.
  void onPointerSample(const input::PointerSample& sample);

  DCompHost composition_;
  SkiaD3D12Context gpu_;
  SkiaSwapChainLayer baseLayer_;
  SkiaSwapChainLayer annotationLayer_;
  input::InputRouter inputRouter_;
  std::optional<stroke::StrokeBuilder> activeStroke_;
  std::optional<std::uint64_t> activePointerId_;
  document::StrokeNode activePreview_;
  document::NodeId activeStrokeId_;
  document::Document document_;
  std::uint64_t strokeSerial_ = 0;
};

}  // namespace canvas::windows
