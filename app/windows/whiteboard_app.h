#pragma once

#include "platform/windows/dcomp_host.h"

#include "canvas/input/pointer_sample.h"

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
};

}  // namespace canvas::windows
