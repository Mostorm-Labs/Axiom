# Embedded-content Windows evidence pending

The implementation and authoring-host web checks are complete, but Windows
runtime evidence cannot be captured from the macOS authoring host.

Run on the target i5-1235U Windows touch display:

```powershell
./scripts/Restore-WebView2.ps1
./scripts/New-TestVideo.ps1
cmake --preset windows-x64
cmake --build --preset windows-x64-release --parallel
ctest --preset windows-x64-release
./out/build/windows-x64/app/windows/Release/canvas_windows.exe `
  --self-test-embedded `
  --video "$PWD/tests/fixtures/test-pattern-1080p30.mp4"
```

Evidence still required:

- packaged rich-text and video pages both reach `ready`;
- Chinese IME input edits the Lexical surface and emits structured state;
- the approved local MP4 loads and play/pause/seek messages work;
- the ordinary HTTPS page loads in the third surface;
- mouse/touch sessions stay bound to the pressed surface;
- red native annotation ink and blue chrome remain above all WebViews;
- moving/resizing document bounds updates the matching surface only;
- no raw pointer sample enters Electron IPC;
- Windows build, CTest, WebView2 Runtime, and video/IME results are recorded.

Do not replace this file with a fabricated screenshot or claimed hardware
result. Capture real evidence only on Windows hardware.
