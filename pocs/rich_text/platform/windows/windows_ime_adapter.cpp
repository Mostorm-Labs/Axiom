#include "windows_ime_adapter.h"

#include <imm.h>

#include <stdexcept>
#include <string>
#include <vector>

namespace canvas::poc04 {
namespace {

std::string Utf16ToUtf8(std::wstring_view value) {
  if (value.empty()) return {};
  const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                       value.data(), value.size(), nullptr, 0,
                                       nullptr, nullptr);
  if (size <= 0) throw std::runtime_error("invalid IME UTF-16 result");
  std::string result(size, '\0');
  WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), value.size(),
                      result.data(), size, nullptr, nullptr);
  return result;
}

std::wstring CompositionString(HIMC context, DWORD kind) {
  const LONG bytes = ImmGetCompositionStringW(context, kind, nullptr, 0);
  if (bytes <= 0) return {};
  std::wstring result(static_cast<size_t>(bytes) / sizeof(wchar_t), L'\0');
  ImmGetCompositionStringW(context, kind, result.data(), bytes);
  return result;
}

}  // namespace

void WindowsImeAdapter::RecordEvidence(const char* name,
                                       uint32_t utf16_length) const {
  if (evidence_callback_ == nullptr) return;
  evidence_callback_({name, utf16_length, composing_}, evidence_context_);
}

bool WindowsImeAdapter::UpdateFromImm(HWND window, LPARAM lparam) {
  HIMC context = ImmGetContext(window);
  if (!context) return false;
  bool handled = false;
  if (lparam & GCS_COMPSTR) {
    const std::wstring composition = CompositionString(context, GCS_COMPSTR);
    const std::string preview = Utf16ToUtf8(composition);
    LONG cursor = ImmGetCompositionStringW(context, GCS_CURSORPOS, nullptr, 0);
    if (cursor < 0) cursor = 0;
    handled = canvas_poc04_session_update_composition_utf8_with_selection(
                  session_, preview.data(), preview.size(),
                  static_cast<uint32_t>(cursor),
                  static_cast<uint32_t>(cursor)) == CANVAS_POC04_STATUS_OK;
    RecordEvidence("composition_update",
                   static_cast<uint32_t>(composition.size()));
  }
  if (lparam & GCS_RESULTSTR) {
    const std::wstring result = CompositionString(context, GCS_RESULTSTR);
    const std::string committed = Utf16ToUtf8(result);
    handled = canvas_poc04_session_update_composition_utf8(
                  session_, committed.data(), committed.size()) == CANVAS_POC04_STATUS_OK &&
              canvas_poc04_session_commit_composition(session_) == CANVAS_POC04_STATUS_OK;
    composing_ = false;
    RecordEvidence("composition_commit", static_cast<uint32_t>(result.size()));
  }
  ImmReleaseContext(window, context);
  return handled;
}

bool WindowsImeAdapter::HandleMessage(HWND window, UINT message, WPARAM wparam,
                                      LPARAM lparam, LRESULT* result) {
  if (result == nullptr) return false;
  switch (message) {
    case WM_SETFOCUS: {
      const bool handled =
          canvas_poc04_session_focus(session_) == CANVAS_POC04_STATUS_OK;
      RecordEvidence("focus");
      return handled;
    }
    case WM_KILLFOCUS:
      composing_ = false;
      pending_high_surrogate_.reset();
      RecordEvidence("blur");
      return canvas_poc04_session_blur(session_) == CANVAS_POC04_STATUS_OK;
    case WM_IME_STARTCOMPOSITION:
      composing_ = canvas_poc04_session_begin_composition(session_) == CANVAS_POC04_STATUS_OK;
      RecordEvidence("composition_start");
      *result = 0;
      return composing_;
    case WM_IME_COMPOSITION:
      *result = 0;
      return UpdateFromImm(window, lparam);
    case WM_IME_ENDCOMPOSITION:
      if (composing_) canvas_poc04_session_cancel_composition(session_);
      composing_ = false;
      RecordEvidence("composition_end");
      *result = 0;
      return true;
    case WM_CHAR: {
      if (composing_) return false;
      *result = 0;
      const wchar_t unit = static_cast<wchar_t>(wparam);
      if (unit == L'\b') {
        pending_high_surrogate_.reset();
        RecordEvidence("delete_backward", 1);
        return canvas_poc04_session_delete_surrounding_utf16(session_, 1, 0) ==
               CANVAS_POC04_STATUS_OK;
      }
      if (unit == 0x7f) {
        pending_high_surrogate_.reset();
        RecordEvidence("delete_forward", 1);
        return canvas_poc04_session_delete_surrounding_utf16(session_, 0, 1) ==
               CANVAS_POC04_STATUS_OK;
      }
      if (unit >= 0xd800 && unit <= 0xdbff) {
        if (pending_high_surrogate_) return false;
        pending_high_surrogate_ = unit;
        return true;
      }
      std::wstring units;
      if (unit >= 0xdc00 && unit <= 0xdfff) {
        if (!pending_high_surrogate_) return false;
        units.push_back(*pending_high_surrogate_);
        units.push_back(unit);
        pending_high_surrogate_.reset();
      } else {
        if (pending_high_surrogate_) {
          pending_high_surrogate_.reset();
          return false;
        }
        units.push_back(unit);
      }
      const std::string value = Utf16ToUtf8(units);
      RecordEvidence("character", static_cast<uint32_t>(units.size()));
      return canvas_poc04_session_insert_utf8(session_, value.data(), value.size()) ==
             CANVAS_POC04_STATUS_OK;
    }
    default:
      return false;
  }
}

}  // namespace canvas::poc04
