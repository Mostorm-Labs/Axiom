# WebView2 vertical-slice evidence pending

Task 12 creates a composition-hosted WebView2 only when
`canvas_windows.exe --self-test-layers` is used. That diagnostic path opts in
to a fixed `data:text/html` page so layer ordering and input gating can be
tested without a network dependency. Normal launcher startup does not create
rich web content yet.

Production `WebView2Surface` instances reject data URLs by default. They allow
remote HTTPS and expose explicit `canvas.local` and `media.canvas.local`
folder options. A local virtual-host URL is rejected unless its exact host has
a configured folder; the mapping is installed with
`ICoreWebView2_3::SetVirtualHostNameToFolderMapping` when the controller is
ready. Wiring document nodes and production resource folders through the
embedded-surface factory is deferred to Task 13.

The authoritative Windows checks remain:

```powershell
./scripts/Restore-WebView2.ps1
cmake --preset windows-x64
cmake --build --preset windows-x64-release --parallel
ctest --preset windows-x64-release -R "canvas_(windows_composition|webview2_surface)_test"
```

No Windows runtime result or layer screenshot was produced on this macOS
host. Real DirectComposition/WebView2 evidence must come from Windows CI and
the target touch display; generated or fabricated evidence is not acceptable.
