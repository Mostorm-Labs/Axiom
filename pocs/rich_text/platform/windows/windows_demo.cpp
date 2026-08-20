#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include "canvas_poc04/canvas_poc04.h"
#include "windows_ime_adapter.h"

namespace {

constexpr float kLayoutWidth = 720.0F;

canvas_poc04_handle_t g_session = 0;
canvas::poc04::WindowsImeAdapter* g_adapter = nullptr;
std::vector<canvas::poc04::WindowsImeEvidenceEvent> g_events;

std::filesystem::path EvidencePath() {
  const DWORD length = GetEnvironmentVariableW(
      L"CANVAS_POC04_IME_EVIDENCE_PATH", nullptr, 0);
  if (length == 0) return {};
  std::wstring value(length, L'\0');
  if (GetEnvironmentVariableW(L"CANVAS_POC04_IME_EVIDENCE_PATH", value.data(),
                              length) == 0) {
    return {};
  }
  value.resize(length - 1);
  return value;
}

std::string JsonEscape(std::string_view value) {
  std::string result;
  result.reserve(value.size());
  for (const unsigned char character : value) {
    switch (character) {
      case '\\': result += "\\\\"; break;
      case '"': result += "\\\""; break;
      case '\n': result += "\\n"; break;
      case '\r': result += "\\r"; break;
      case '\t': result += "\\t"; break;
      default:
        if (character < 0x20) {
          constexpr char digits[] = "0123456789abcdef";
          result += "\\u00";
          result += digits[character >> 4U];
          result += digits[character & 0x0fU];
        } else {
          result += static_cast<char>(character);
        }
    }
  }
  return result;
}

std::string Digest() {
  size_t required = 0;
  if (canvas_poc04_document_digest(g_session, nullptr, 0, &required) !=
          CANVAS_POC04_STATUS_BUFFER_TOO_SMALL ||
      required == 0) {
    return {};
  }
  std::string result(required, '\0');
  if (canvas_poc04_document_digest(g_session, result.data(), result.size(),
                                   &required) != CANVAS_POC04_STATUS_OK) {
    return {};
  }
  result.resize(required - 1);
  return result;
}

std::string PresentedText() {
  uint64_t length = 0;
  if (canvas_poc04_session_presented_utf16_length(g_session, &length) !=
      CANVAS_POC04_STATUS_OK) {
    return {};
  }
  size_t required = 0;
  if (canvas_poc04_session_presented_text_range_utf8(
          g_session, 0, length, nullptr, 0, &required) !=
          CANVAS_POC04_STATUS_BUFFER_TOO_SMALL ||
      required == 0) {
    return {};
  }
  std::string result(required, '\0');
  if (canvas_poc04_session_presented_text_range_utf8(
          g_session, 0, length, result.data(), result.size(), &required) !=
      CANVAS_POC04_STATUS_OK) {
    return {};
  }
  result.resize(required - 1);
  return result;
}

std::wstring Utf8ToWide(std::string_view value) {
  if (value.empty()) return {};
  const int required = MultiByteToWideChar(
      CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
      nullptr, 0);
  if (required <= 0) return {};
  std::wstring result(static_cast<size_t>(required), L'\0');
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                          static_cast<int>(value.size()), result.data(),
                          required) != required) {
    return {};
  }
  return result;
}

uint32_t ExerciseLifecycle() {
  uint32_t failures = 0;
  for (uint32_t cycle = 0; cycle < 100; ++cycle) {
    canvas_poc04_handle_t session = 0;
    canvas_poc04_create_info_t info{sizeof(info), CANVAS_POC04_ABI_VERSION};
    if (canvas_poc04_session_create(&info, &session) !=
            CANVAS_POC04_STATUS_OK ||
        canvas_poc04_session_focus(session) != CANVAS_POC04_STATUS_OK ||
        canvas_poc04_session_begin_composition(session) !=
            CANVAS_POC04_STATUS_OK ||
        canvas_poc04_session_blur(session) != CANVAS_POC04_STATUS_OK ||
        canvas_poc04_session_destroy(session) != CANVAS_POC04_STATUS_OK) {
      ++failures;
    }
  }
  return failures;
}

void RecordEvidence(const canvas::poc04::WindowsImeEvidenceEvent& event,
                    void*) {
  g_events.push_back(event);
}

void WriteEvidence() {
  const std::filesystem::path path = EvidencePath();
  if (path.empty()) return;
  std::error_code ignored;
  std::filesystem::create_directories(path.parent_path(), ignored);
  std::ofstream stream(path, std::ios::out | std::ios::trunc);
  if (!stream) return;

  canvas_poc04_utf16_range_t selection{};
  canvas_poc04_session_selection_flat_utf16(g_session, &selection);
  canvas_poc04_rect_t caret{};
  canvas_poc04_session_caret_rect_for_offset_utf16(
      g_session, selection.location + selection.length, kLayoutWidth, &caret);
  const std::string text = PresentedText();
  const bool has_start = std::ranges::any_of(g_events, [](const auto& event) {
    return std::string_view(event.name) == "composition_start";
  });
  const bool has_update = std::ranges::any_of(g_events, [](const auto& event) {
    return std::string_view(event.name) == "composition_update";
  });
  const bool has_commit = std::ranges::any_of(g_events, [](const auto& event) {
    return std::string_view(event.name) == "composition_commit";
  });

  stream << "{\n"
         << "  \"schema_version\": 1,\n"
         << "  \"platform\": \"windows\",\n"
         << "  \"protocol\": \"Win32 IMM\",\n"
         << "  \"controlled_flow\": \"ni hao -> 你好\",\n"
         << "  \"final_text\": \"" << JsonEscape(text) << "\",\n"
         << "  \"controlled_flow_passed\": "
         << (text == "你好" ? "true" : "false") << ",\n"
         << "  \"observed_composition_start\": "
         << (has_start ? "true" : "false") << ",\n"
         << "  \"observed_composition_update\": "
         << (has_update ? "true" : "false") << ",\n"
         << "  \"observed_composition_commit\": "
         << (has_commit ? "true" : "false") << ",\n"
         << "  \"selection\": [" << selection.location << ", "
         << selection.length << "],\n"
         << "  \"caret\": [" << caret.x << ", " << caret.y << ", "
         << caret.width << ", " << caret.height << "],\n"
         << "  \"digest\": \"" << Digest() << "\",\n"
         << "  \"lifecycle\": {\"cycles\": 100, \"failures\": "
         << ExerciseLifecycle() << "},\n"
         << "  \"events\": [\n";
  for (size_t index = 0; index < g_events.size(); ++index) {
    const auto& event = g_events[index];
    stream << "    {\"sequence\": " << index << ", \"event\": \""
           << event.name << "\", \"utf16_length\": "
           << event.utf16_length << ", \"composing\": "
           << (event.composing ? "true" : "false") << "}"
           << (index + 1 == g_events.size() ? "\n" : ",\n");
  }
  stream << "  ]\n}\n";
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam,
                            LPARAM lparam) {
  LRESULT result = 0;
  if (g_adapter &&
      g_adapter->HandleMessage(window, message, wparam, lparam, &result)) {
    InvalidateRect(window, nullptr, TRUE);
    return result;
  }
  if (message == WM_DESTROY) {
    WriteEvidence();
    PostQuitMessage(0);
    return 0;
  }
  if (message == WM_PAINT) {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(window, &paint);
    RECT bounds{};
    GetClientRect(window, &bounds);
    bounds.left += 24;
    bounds.right -= 24;
    bounds.top += 24;
    RECT instructions_bounds = bounds;
    instructions_bounds.bottom = instructions_bounds.top + 210;
    const wchar_t* instructions =
        L"Canvas POC-04 Win32 IMM physical validation\n\n"
        L"1. Select Microsoft Pinyin.\n"
        L"2. Type: ni hao\n"
        L"3. Choose the candidate: 你好\n"
        L"4. Close this window to write the JSON evidence.\n\n"
        L"The report contains the controlled final text, selection, caret, "
        L"Runtime digest, and redacted IME event metadata.";
    DrawTextW(dc, instructions, -1, &instructions_bounds,
              DT_LEFT | DT_TOP | DT_WORDBREAK);

    RECT text_bounds = bounds;
    text_bounds.top = instructions_bounds.bottom + 16;
    const std::wstring text = Utf8ToWide(PresentedText());
    const std::wstring presented =
        text.empty() ? L"Runtime text: (empty)" : L"Runtime text: " + text;
    HFONT font = CreateFontW(32, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    const HGDIOBJ previous_font = SelectObject(dc, font);
    DrawTextW(dc, presented.c_str(), -1, &text_bounds,
              DT_LEFT | DT_TOP | DT_WORDBREAK);
    SelectObject(dc, previous_font);
    DeleteObject(font);
    EndPaint(window, &paint);
    return 0;
  }
  return DefWindowProcW(window, message, wparam, lparam);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
  canvas_poc04_create_info_t info{sizeof(info), CANVAS_POC04_ABI_VERSION};
  if (canvas_poc04_session_create(&info, &g_session) !=
      CANVAS_POC04_STATUS_OK) {
    return 1;
  }
  canvas::poc04::WindowsImeAdapter adapter(g_session, RecordEvidence, nullptr);
  g_adapter = &adapter;
  WNDCLASSW window_class{};
  window_class.lpfnWndProc = WindowProc;
  window_class.hInstance = instance;
  window_class.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32513));
  window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  window_class.lpszClassName = L"CanvasPoc04RichText";
  RegisterClassW(&window_class);
  HWND window = CreateWindowW(window_class.lpszClassName,
                              L"Canvas POC-04 Windows IME",
                              WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                              800, 600, nullptr, nullptr, instance, nullptr);
  ShowWindow(window, show);
  SetFocus(window);
  MSG message{};
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  g_adapter = nullptr;
  canvas_poc04_session_destroy(g_session);
  return static_cast<int>(message.wParam);
}
