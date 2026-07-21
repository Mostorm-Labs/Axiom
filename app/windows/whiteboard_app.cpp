#include "whiteboard_app.h"

#include "platform/windows/win_pointer_adapter.h"

#include <windows.h>

#include <vector>

namespace canvas::windows {

namespace {

constexpr wchar_t kWindowClassName[] = L"MostormCanvasWindow";

int hresultExitCode(HRESULT hr) {
  return FAILED(hr) ? static_cast<int>(hr) : 1;
}

input::PointerPhase pointerPhaseForMessage(UINT message) {
  switch (message) {
    case WM_POINTERDOWN:
      return input::PointerPhase::Down;
    case WM_POINTERUP:
      return input::PointerPhase::Up;
    case WM_POINTERCAPTURECHANGED:
      return input::PointerPhase::Cancel;
    case WM_POINTERUPDATE:
    default:
      return input::PointerPhase::Move;
  }
}

}  // namespace

int WhiteboardApp::run(HINSTANCE instance, int commandShow) {
  if (instance == nullptr) {
    return hresultExitCode(E_INVALIDARG);
  }

  WNDCLASSEXW windowClass{};
  windowClass.cbSize = sizeof(windowClass);
  windowClass.hInstance = instance;
  windowClass.lpfnWndProc = &WhiteboardApp::windowProc;
  windowClass.lpszClassName = kWindowClassName;
  windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  windowClass.style = CS_HREDRAW | CS_VREDRAW;

  if (RegisterClassExW(&windowClass) == 0) {
    return hresultExitCode(HRESULT_FROM_WIN32(GetLastError()));
  }

  const HWND window = CreateWindowExW(
      WS_EX_APPWINDOW, kWindowClassName, L"Mostorm Canvas", WS_POPUP,
      CW_USEDEFAULT, CW_USEDEFAULT, 1280, 720, nullptr, nullptr, instance,
      this);
  if (window == nullptr) {
    const HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
    UnregisterClassW(kWindowClassName, instance);
    return hresultExitCode(hr);
  }

  const HRESULT compositionResult = composition_.initialize(window);
  if (FAILED(compositionResult)) {
    DestroyWindow(window);
    UnregisterClassW(kWindowClassName, instance);
    return hresultExitCode(compositionResult);
  }

  ShowWindow(window, commandShow);
  UpdateWindow(window);

  MSG message{};
  int messageResult = 0;
  while ((messageResult = GetMessageW(&message, nullptr, 0, 0)) > 0) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }

  UnregisterClassW(kWindowClassName, instance);
  return messageResult < 0 ? hresultExitCode(HRESULT_FROM_WIN32(GetLastError()))
                           : static_cast<int>(message.wParam);
}

LRESULT CALLBACK WhiteboardApp::windowProc(HWND window, UINT message,
                                           WPARAM wParam, LPARAM lParam) {
  if (message == WM_NCCREATE) {
    const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
    SetWindowLongPtrW(window, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(create->lpCreateParams));
  }

  if (message == WM_DESTROY) {
    PostQuitMessage(0);
    return 0;
  }

  if (message == WM_POINTERDOWN || message == WM_POINTERUPDATE ||
      message == WM_POINTERUP || message == WM_POINTERCAPTURECHANGED) {
    if (message == WM_POINTERUP || message == WM_POINTERCAPTURECHANGED) {
      ReleaseCapture();
    }
    auto* app = reinterpret_cast<WhiteboardApp*>(GetWindowLongPtrW(
        window, GWLP_USERDATA));
    if (app == nullptr) {
      return DefWindowProcW(window, message, wParam, lParam);
    }

    const UINT32 pointerId = GET_POINTERID_WPARAM(wParam);
    POINTER_INPUT_TYPE pointerType{};
    if (!GetPointerType(pointerId, &pointerType)) {
      return DefWindowProcW(window, message, wParam, lParam);
    }

    const input::PointerPhase phase = pointerPhaseForMessage(message);
    std::vector<input::PointerSample> samples;
    if (pointerType == PT_PEN) {
      samples = WinPointerAdapter::readPenHistory(window, pointerId, phase);
    } else if (pointerType == PT_TOUCH) {
      samples = WinPointerAdapter::readTouchHistory(window, pointerId, phase);
    } else {
      return DefWindowProcW(window, message, wParam, lParam);
    }

    for (const auto& sample : samples) {
      app->onPointerSample(sample);
    }

    if (message == WM_POINTERDOWN) {
      SetCapture(window);
    }
    return 0;
  }

  return DefWindowProcW(window, message, wParam, lParam);
}

void WhiteboardApp::onPointerSample(const input::PointerSample&) {
  // Intentionally empty until Task 11 wires samples into the stroke loop.
}

}  // namespace canvas::windows
