# Task 22 macOS CI — hosted arm64 run green; runtime evidence pending

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

## First hosted attempt

Run [30703942343](https://github.com/Mostorm-Labs/canvas/actions/runs/30703942343)
reached the vcpkg install step but failed because the runner's preinstalled
`/usr/local/share/vcpkg` checkout did not contain the manifest's
`builtin-baseline` (`a51bb4d1434e6d0927ff79db8033bed8522b85df`). The workflow now
reads and validates that baseline, fetches the exact commit into a usable
checkout, falls back to a baseline clone when the preinstalled checkout cannot
be updated, and fails closed if the object still cannot be materialized. A new
hosted run is required before claiming any macOS CI result.

## Second hosted attempt

Run [30704136648](https://github.com/Mostorm-Labs/canvas/actions/runs/30704136648)
showed that fetching the missing object alone was insufficient: the
preinstalled vcpkg executable still used an older `HEAD` and therefore an older
port database. The workflow now reuses a preinstalled checkout only when its
clean `HEAD` is exactly the locked baseline and the vcpkg executable exists;
otherwise it always uses an independent detached clone at that exact commit.
A third hosted run is required to validate this stricter checkout selection.

## Third hosted run — green

Run [30704424435](https://github.com/Mostorm-Labs/canvas/actions/runs/30704424435)
completed successfully for macOS commit `ac2dfce6b8c9b765d7348d62817c4c913fea127a`.
The build job is [91380965114](https://github.com/Mostorm-Labs/canvas/actions/runs/30704424435/job/91380965114)
on `macos-14` arm64. It passed, in order:

- arm64 runner verification, Node setup, workflow contract, and shared web-asset tests;
- exact locked-baseline vcpkg resolution, dependency install, and binary-cache steps;
- Configure and Release Build;
- non-GUI CTest/source-contract gate;
- required GUI/Metal/WKWebView discovery gate and AppKit/Metal/WKWebView tests;
- whitespace validation.

The paired Windows check for PR #2 was also green in run
[30704424433](https://github.com/Mostorm-Labs/canvas/actions/runs/30704424433)
(build job [91380964934](https://github.com/Mostorm-Labs/canvas/actions/runs/30704424433/job/91380964934));
the PR-event Release job was skipped by design. This CI result does not provide a
macOS release artifact and does not establish real-device touch/pen/IME behavior,
Electron GUI integration, or a latency target. Those remain pending.
