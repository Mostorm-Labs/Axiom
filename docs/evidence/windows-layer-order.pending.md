# Windows layer-order evidence pending

Run the following on a real Windows touch display with a hardware D3D12 adapter:

```powershell
out\build\windows-x64\app\windows\Release\canvas_windows.exe --self-test-layers
```

Expected back-to-front result:

1. opaque green base;
2. centered filled gray embedded placeholder;
3. two transparent red diagonals crossing the placeholder;
4. four blue chrome handles covering the red diagonal endpoints.

No PNG is checked in yet because this development host is macOS and cannot run
DirectComposition/D3D12. A screenshot must be captured from the real Windows
touch hardware after the Windows build and integration tests pass; generated or
fabricated evidence is explicitly not acceptable.
