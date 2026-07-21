#pragma once

#include "platform/windows/dcomp_host.h"

namespace canvas::windows {

class WhiteboardApp {
 public:
  int run(HINSTANCE instance, int commandShow);

  static LRESULT CALLBACK windowProc(HWND window, UINT message,
                                     WPARAM wParam, LPARAM lParam);

 private:
  DCompHost composition_;
};

}  // namespace canvas::windows
