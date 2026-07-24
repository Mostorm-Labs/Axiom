# Electron/native control Windows evidence pending

Task 15's platform-neutral protocol and Electron TypeScript checks pass on the
macOS authoring host. This file is a Windows verification procedure, not a
record of results. Do not mark any item complete without retaining the real
Windows logs or hardware evidence.

## Build and native tests

Run from a Visual Studio developer PowerShell with `VCPKG_ROOT` configured:

```powershell
./scripts/Restore-WebView2.ps1
cmake --preset windows-x64
cmake --build --preset windows-x64-release --parallel
cmake --build --preset windows-x64-release `
  --target canvas_ipc_protocol_test canvas_named_pipe_server_test
ctest --preset windows-x64-release -N
ctest --preset windows-x64-release `
  -R "^IpcProtocolTest\." --no-tests=error
ctest --preset windows-x64-release `
  -R "^canvas_named_pipe_server_test\." --no-tests=error
ctest --preset windows-x64-release --no-tests=error
```

Confirm both targeted filters discover at least one test. Preserve the MSVC
configure/build output and full CTest log. The named-pipe test must demonstrate
that reconnecting creates a new connection generation and that a stale response
cannot be delivered to the new client.

## Electron-to-native process and command smoke test

Prepare the Electron harness without replacing its lockfile:

```powershell
Push-Location tools/electron-host
npm ci
npm run build
node --check dist/main.js
node --check dist/preload.js
npm audit
$env:CANVAS_EXE = (Resolve-Path `
  "..\..\out\build\windows-x64\app\windows\Release\canvas_windows.exe").Path
npm start
Pop-Location
```

Record the native executable commit SHA, Windows build, WebView2 Runtime,
Electron and Node versions, and the generated pipe name without recording the
session token. Verify with Process Explorer that Electron starts exactly one
native Canvas process. Controls must remain disabled until an authenticated
`ready` arrives.

Exercise Draw, Select, Interact, Add Web, Add Video, Add Rich Text, Save, and
Open. Confirm each accepted command changes only the native window, embedded
objects render in the matching WebView2 surfaces, and save/open preserves the
Document and restores those surfaces. Capture response/document-state envelopes
with a test-only debugger or trace, redacting the token. Invalid payloads and
unauthenticated clients must receive no authority to mutate native state.

### Asynchronous embedded completion is a follow-up

Observe the native event stream while creating and opening embedded content.
The current implementation admits WebView controller creation and navigation
asynchronously, and it does not yet emit `embedded-state` Ready/Failed
completion events or roll back a node after an asynchronous WebView failure.
An accepted response therefore verifies only synchronous HRESULT/render
admission. Record the missing completion event as a pending enhancement, not as
a passing runtime result. A future implementation must correlate completion to
the node/request and add explicit asynchronous failure policy before this item
can be verified.

## Disconnect and reconnect

On a disposable test session, use Process Explorer or Sysinternals Handle to
close only Electron's handle for the generated `mostorm-canvas-*` named pipe;
do not terminate the native Canvas process. Record the process IDs and handle
chosen before closing it.

Verify that the Electron controls become disabled, the old connection is no
longer current, and the bounded retry loop establishes a new pipe connection.
The new connection must send a fresh authenticated hello and receive a fresh
`ready` before controls are re-enabled. Queue a response on the old generation
during this test and confirm it is not observed by the replacement client.
Repeat until the retry budget is exhausted and verify that the host reports a
native failure without an unbounded reconnect loop.

## Graceful shutdown

Close the Electron window while Canvas is responsive. Capture process-exit and
named-pipe events with Process Monitor or WPR. Verify that Electron sends one
`shutdown` command, Canvas closes its window and exits normally within three
seconds, and the fallback child termination is not used. Repeat while socket
backpressure is active to confirm the reserved shutdown control budget still
allows the graceful request.

Also close Canvas first. Electron must disable controls, close the stale socket,
report that the native process exited, and avoid leaving either process or the
named-pipe instance behind.

## Prove raw pointer samples stay out of IPC

Filter Process Monitor or an ETW/WPR trace to the Electron and Canvas process
IDs and the generated named-pipe path. After `ready`, clear the trace and draw
continuously with mouse, pen, and touch for at least 30 seconds without pressing
an Electron control. There must be no pipe writes proportional to pointer
movement and no `pointer-sample`, `stroke-point`, or `video-frame` envelope.

As a positive control, click Select once and confirm that exactly the expected
low-frequency command/response traffic appears in the same trace. Retain the
trace together with the passing `RejectsRawPointerMessages` protocol test; code
inspection or a test result alone is not runtime evidence.

## Target hardware checks still required

On the i5-1235U Windows touch display, also verify Chinese IME in rich text,
touch routing and capture, approved 1080p30 video playback/control, WebView2
layer ordering, and reconnect behavior while those surfaces are active.

Measure true touch-to-visible-ink latency with a 240 fps or faster camera: use
a high-contrast stroke, record physical contact and display pixels together,
repeat at least 30 strokes across the center and edges, and report p50/p95/p99.
The p95 gate is below 50 ms. API timestamps cannot replace this measurement
because they exclude panel scan-out and display latency.

All sections above remain pending on this macOS host. Windows CI may supply the
build and automated-test results; E2E, touch/IME/video, and latency claims still
require the stated Windows runtime or target-hardware evidence.
