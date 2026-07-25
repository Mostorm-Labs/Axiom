# Canvas

Canvas is the Mostorm cross-platform collaborative whiteboard. The first milestone is a Windows native vertical slice controlled by Electron, with a shared C++ document core, Skia rendering, native pointer input, and WebView2 embedded content.

## Windows build

1. Install Visual Studio 2022 with Desktop development with C++, plus Node.js
   22.12 or newer.
2. Set `VCPKG_ROOT` to a vcpkg checkout.
3. Run `./scripts/Restore-WebView2.ps1`.
4. Run `cmake --preset windows-x64`.
5. Run `cmake --build --preset windows-x64-release --parallel`.
6. Run `ctest --preset windows-x64-release`.

CMake restores the locked npm workspace, builds the rich-text/video assets,
and copies them beside `canvas_windows.exe` under `web/`.

## Windows downloads

Every successful pull-request or manually dispatched Windows workflow uploads
a versioned native portable ZIP to the workflow run's **Artifacts** section in
[GitHub Actions](https://github.com/Mostorm-Labs/canvas/actions). Artifacts are
retained for 30 days. Builds created from a `v*` Git tag are also published,
together with a SHA-256 checksum, on
[GitHub Releases](https://github.com/Mostorm-Labs/canvas/releases). Tags with a
hyphen, such as `v0.1.0-alpha.1`, produce a prerelease.

Extract the complete ZIP before running `canvas_windows.exe`; the adjacent
`web/` directory is required. The current download is an unsigned native
portable build and requires the Microsoft Edge WebView2 Runtime. The Electron
launcher is not included yet.

## Embedded-content diagnostic

From PowerShell at the repository root:

```powershell
./scripts/New-TestVideo.ps1
./out/build/windows-x64/app/windows/Release/canvas_windows.exe `
  --self-test-embedded `
  --video "$PWD/tests/fixtures/test-pattern-1080p30.mp4"
```

The diagnostic hosts Lexical rich text, HTML video, and an HTTPS page as three
DirectComposition children below native annotation ink. Generated MP4
fixtures are intentionally ignored and must not be committed.

## Document persistence

Canvas files use the versioned schema encoded as MessagePack. Saves write and
flush `<path>.tmp` before an atomic Windows replacement; loads reject files
larger than 512 MiB.

```powershell
./out/build/windows-x64/app/windows/Release/canvas_windows.exe `
  --self-test-document --save "$env:TEMP/canvas-roundtrip.canvas"
./out/build/windows-x64/app/windows/Release/canvas_windows.exe `
  --open "$env:TEMP/canvas-roundtrip.canvas"
```
