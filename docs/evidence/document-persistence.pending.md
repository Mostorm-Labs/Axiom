# Document persistence verification pending

Task 14 implements the Windows file-store and startup commands, but this
macOS host cannot execute the Win32 file APIs or the WebView2 restoration path.

Run on Windows before treating the slice as verified:

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64-release --parallel
ctest --preset windows-x64-release -R canvas_document_codec_test
./out/build/windows-x64/app/windows/Release/canvas_windows.exe `
  --self-test-document --save "$env:TEMP/canvas-roundtrip.canvas"
./out/build/windows-x64/app/windows/Release/canvas_windows.exe `
  --open "$env:TEMP/canvas-roundtrip.canvas"
```

Confirm atomic replacement keeps an existing destination after a forced write
or rename failure, removes only the current `.tmp`, rejects a file larger than
512 MiB, and restores embedded surfaces plus attached annotation ink.
