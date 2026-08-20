#ifndef CANVAS_POC04_WINDOWS_IME_ADAPTER_H_
#define CANVAS_POC04_WINDOWS_IME_ADAPTER_H_

#include <windows.h>

#include <cstdint>
#include <optional>

#include "canvas_poc04/canvas_poc04.h"

namespace canvas::poc04 {

struct WindowsImeEvidenceEvent {
  const char* name = "";
  uint32_t utf16_length = 0;
  bool composing = false;
};

using WindowsImeEvidenceCallback = void (*)(
    const WindowsImeEvidenceEvent& event, void* context);

class WindowsImeAdapter {
 public:
  explicit WindowsImeAdapter(
      canvas_poc04_handle_t session,
      WindowsImeEvidenceCallback evidence_callback = nullptr,
      void* evidence_context = nullptr)
      : session_(session),
        evidence_callback_(evidence_callback),
        evidence_context_(evidence_context) {}
  bool HandleMessage(HWND window, UINT message, WPARAM wparam, LPARAM lparam,
                     LRESULT* result);

 private:
  void RecordEvidence(const char* name, uint32_t utf16_length = 0) const;
  bool UpdateFromImm(HWND window, LPARAM lparam);
  canvas_poc04_handle_t session_ = 0;
  WindowsImeEvidenceCallback evidence_callback_ = nullptr;
  void* evidence_context_ = nullptr;
  bool composing_ = false;
  std::optional<wchar_t> pending_high_surrogate_;
};

}  // namespace canvas::poc04

#endif
