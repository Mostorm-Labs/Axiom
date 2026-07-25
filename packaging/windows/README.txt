Canvas for Windows - native portable package
============================================

This archive contains the native Canvas whiteboard executable. It is a
portable package, not an installer.

Running Canvas
--------------

1. Extract the entire ZIP to a writable directory.
2. Keep canvas_windows.exe and the web\ directory together. The rich-text,
   video, and embedded-page features require those web assets at runtime.
3. Install or update the Microsoft Edge WebView2 Runtime if Windows does not
   already provide it:
   https://developer.microsoft.com/microsoft-edge/webview2/
4. Run canvas_windows.exe.

The Electron launcher is not included in this package. Start and control the
native executable directly until launcher packaging is added. This early
package is also unsigned, so Windows SmartScreen may show a warning.
