#ifndef CANVAS_POC04_WINDOWS_IME_ADAPTER_H_
#define CANVAS_POC04_WINDOWS_IME_ADAPTER_H_

#include <windows.h>

#include <optional>

#include "canvas_poc04/canvas_poc04.h"

namespace canvas::poc04 {

class WindowsImeAdapter {
 public:
  explicit WindowsImeAdapter(canvas_poc04_handle_t session) : session_(session) {}
  bool HandleMessage(HWND window, UINT message, WPARAM wparam, LPARAM lparam,
                     LRESULT* result);

 private:
  bool UpdateFromImm(HWND window, LPARAM lparam);
  canvas_poc04_handle_t session_ = 0;
  bool composing_ = false;
  std::optional<wchar_t> pending_high_surrogate_;
};

}  // namespace canvas::poc04

#endif
