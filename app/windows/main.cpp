#include "whiteboard_app.h"

#include <objbase.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int commandShow) {
  const HRESULT initResult =
      CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (FAILED(initResult)) {
    return static_cast<int>(initResult);
  }

  canvas::windows::WhiteboardApp app;
  const int result = app.run(instance, commandShow);
  CoUninitialize();
  return result;
}
