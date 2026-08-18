#include <windows.h>

#include "canvas_poc04/canvas_poc04.h"
#include "windows_ime_adapter.h"

namespace {
canvas_poc04_handle_t g_session = 0;
canvas::poc04::WindowsImeAdapter* g_adapter = nullptr;

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam,
                            LPARAM lparam) {
  LRESULT result = 0;
  if (g_adapter && g_adapter->HandleMessage(window, message, wparam, lparam, &result)) {
    InvalidateRect(window, nullptr, TRUE);
    return result;
  }
  if (message == WM_DESTROY) {
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProcW(window, message, wparam, lparam);
}
}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
  canvas_poc04_create_info_t info{sizeof(info), CANVAS_POC04_ABI_VERSION};
  if (canvas_poc04_session_create(&info, &g_session) != CANVAS_POC04_STATUS_OK) return 1;
  canvas::poc04::WindowsImeAdapter adapter(g_session);
  g_adapter = &adapter;
  WNDCLASSW window_class{};
  window_class.lpfnWndProc = WindowProc;
  window_class.hInstance = instance;
  window_class.lpszClassName = L"CanvasPoc04RichText";
  RegisterClassW(&window_class);
  HWND window = CreateWindowW(window_class.lpszClassName, L"Canvas POC-04 IME",
                              WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                              800, 600, nullptr, nullptr, instance, nullptr);
  ShowWindow(window, show);
  MSG message{};
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  g_adapter = nullptr;
  canvas_poc04_session_destroy(g_session);
  return static_cast<int>(message.wParam);
}
