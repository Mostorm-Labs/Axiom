#include "whiteboard_app.h"

#include <objbase.h>
#include <cwchar>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine,
                    int commandShow) {
  const HRESULT initResult =
      CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (FAILED(initResult)) {
    return static_cast<int>(initResult);
  }

  int result = 0;
  {
    canvas::windows::WhiteboardApp app;
    const bool selfTestLayers =
        commandLine != nullptr &&
        std::wcsstr(commandLine, L"--self-test-layers") != nullptr;
    result = app.run(instance, commandShow, selfTestLayers);
  }
  CoUninitialize();
  return result;
}
