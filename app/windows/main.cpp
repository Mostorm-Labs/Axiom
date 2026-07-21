#include "whiteboard_app.h"

#include <objbase.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int commandShow) {
  const HRESULT initResult =
      CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (FAILED(initResult)) {
    return static_cast<int>(initResult);
  }

  int result = 0;
  {
    canvas::windows::WhiteboardApp app;
    result = app.run(instance, commandShow);
  }
  CoUninitialize();
  return result;
}
