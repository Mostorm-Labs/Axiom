# Canvas

Canvas is the Mostorm cross-platform collaborative whiteboard. The first milestone is a Windows native vertical slice controlled by Electron, with a shared C++ document core, Skia rendering, native pointer input, and WebView2 embedded content.

## Windows build

1. Install Visual Studio 2022 with Desktop development with C++.
2. Set `VCPKG_ROOT` to a vcpkg checkout.
3. Run `cmake --preset windows-x64`.
4. Run `cmake --build --preset windows-x64-release --parallel`.
5. Run `ctest --preset windows-x64-release`.
