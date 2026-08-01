# Task 22 macOS CI — pending first hosted run

This increment adds `.github/workflows/macos-build.yml` for the arm64 macOS
runner (`macos-14`). It validates the shared web assets, restores/builds the
arm64-osx vcpkg manifest, configures and builds the macOS preset, runs all
non-GUI tests and source contracts, and then runs the AppKit/Metal/WKWebView
tests as a separate required gate.

The GUI/Metal test step is intentionally not skippable. If the hosted runner
cannot provide WindowServer or Metal, the job must fail and the limitation is
recorded as an external CI blocker; a skipped GUI test is not evidence of a
green macOS build.

## Local checks before the first hosted run

From the macOS worktree:

```sh
node --test tests/contracts/macos_build_workflow.test.mjs
git diff --check
```

The first hosted run still needs to establish whether `macos-14` can restore
the vcpkg Skia/Metal dependency and execute the AppKit GUI tests. No release
artifact, hardware latency, input adapter, IME, or Electron evidence is
claimed by this workflow.
