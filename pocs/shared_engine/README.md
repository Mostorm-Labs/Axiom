# POC-01 Shared Engine

This directory is intentionally isolated from future product runtime code. It
proves that one single-threaded C++20 document runtime can replay the same
strict operation stream and produce the same semantic and visual result on
Web/WASM/WebGL2, Windows/D3D12, macOS/iOS/iPadOS Metal, and Android/GLES3.

All C ABI, replay schema, handles, scene structures, and fixtures in this
directory are **Experimental**. R1 will replace them using the evidence
collected by this POC; no source or binary compatibility is promised.

## Host-core quick start

```sh
python3 tools/bootstrap_deps.py --core
cmake --preset host-debug
cmake --build --preset host-debug
ctest --preset host-debug
```

The host build exercises Document, Operations, SceneCompiler, XXH3 digest,
the C ABI, a dependency-free software probe, lifecycle, and smoke tests. Only
the pinned Skia raster target owns the visual reference. The native Apple
harness validates Runtime portability without selecting a macOS product shell.

Platform bootstrap, Skia GN arguments, demo usage, acceptance artifacts, and
the manual GPU benchmark process are documented in
[`docs/POC01_RUNBOOK.md`](docs/POC01_RUNBOOK.md).
