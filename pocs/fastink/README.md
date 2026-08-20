# POC-06 Arc / FastInk

This directory is the POC-06 integration harness. The reusable module lives in
the top-level [`arc/`](../../arc/) project; this directory is intentionally the
only place that adapts the experimental POC-02 `PreviewSink` to the versioned
Arc protocol.

## Scope

- `adapter/`: a one-way POC-02 → Arc protocol adapter. It does not belong to
  the Arc SDK and must not be installed or included by external consumers.
- `harness/`: deterministic Default-vs-Arc replay and digest equivalence.
- `fixtures/`: versioned input traces used by headless and platform runs.
- `tests/`: lifecycle, failure-isolation, and adapter regression tests.
- `reports/`: checked-in report schema and runbook; device measurements are
  generated under `out/results` and are not silently treated as acceptance.

POC-06 freezes the protocol, presentation target ownership, error domain,
handoff state machine, and platform verification levels. It does not freeze a
public binary compatibility promise or a system DRM/Direct Plane API.

## Build

```sh
cmake -S . -B out/poc06 -G Ninja \
  -DCANVAS_BUILD_POC01=OFF \
  -DCANVAS_BUILD_POC02=ON \
  -DCANVAS_POC02_BUILD_TESTS=OFF \
  -DCANVAS_BUILD_ARC=ON \
  -DCANVAS_BUILD_POC06=ON \
  -DARC_BUILD_PLATFORM_TARGETS=ON
cmake --build out/poc06 --parallel
ctest --test-dir out/poc06 --output-on-failure
```

The headless harness compares the Default Preview event trace with Arc's trace
and requires identical Document and Stroke digests. The Arc target may fall
back or lose Preview geometry, but it must never make the Canonical replay
fail.

## Host integration boundary

`arc::HostAdapter` is the platform-independent composition boundary used by
native/Web hosts. It owns one `PreviewBackend`, one fallback backend, one
`InputSource`, and the Arc `Bridge`; it does not own the Axiom Document,
`StrokeSession`, or Canonical render target. Hosts use it to:

- attach, resize, replace, lose, and detach a generation-bound Preview target;
- start/stop an input source and submit normalized `PointerSampleBatch` data;
- route source loss separately from presentation failure;
- consume Canonical redraw requests after Preview fallback;
- forward Canonical visibility receipts to the handoff state machine.

Platform adapters keep HWND, JNI/Surface, Metal, DOM, and WebGL objects outside
the protocol. The Web adapter uses a non-recycled integer registry handle for
its WebGL2 context because JavaScript object references cannot cross the C ABI.

## Platform implementation matrix

Every target has a separate Arc factory and build target. Tier A/B/Reuse/Utility
changes evidence strength only; it does not remove an implementation:

| Target | Source boundary | Evidence |
| --- | --- | --- |
| Web | `platform/web` + `pointer_adapter.ts` | WebGL2 overlay, raw/coalesced pointer batches |
| Windows | `platform/windows` | WM_POINTER/history host contract and independent target |
| Android | `platform/android` | MotionEvent/history → Native CanvasView/JNI contract |
| macOS | `platform/apple` | NSEvent/tablet → Metal target contract |
| iOS/iPadOS | `platform/apple` | coalesced touch → Metal target contract |
| ChromiumOS | `platform/web/chromiumos_backend.cpp` | Web reuse and optional capability fallback |
| Headless | `platform/headless` | deterministic protocol/null oracle |
| Device | `platform/device` | conditional BSP/direct-plane boundary only |

Native host objects are supplied through the opaque target handle. They never
cross `Arc::Protocol`, and Canonical and Preview do not share presentable
backbuffer ownership.
