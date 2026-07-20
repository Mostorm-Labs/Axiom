# Canvas Development Rules

- Configure Windows builds with `cmake --preset windows-x64`.
- Build with `cmake --build --preset windows-x64-release --parallel`.
- Run native tests with `ctest --preset windows-x64-release`.
- Keep platform-neutral headers free of Win32, WebView2, D3D, DXGI, and COM types.
- Do not put network, disk, WebView, Electron IPC, or media work on the pen-preview path.
- Add a failing test before changing core behavior.
- Keep embedded content below `AnnotationLayer` and `InteractionChromeLayer`.
- Raw pointer samples never cross Electron IPC.
