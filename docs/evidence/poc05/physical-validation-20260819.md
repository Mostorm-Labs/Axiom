# POC-05 Hybrid Surface Evidence

Date: 2026-08-19

Branch: `codex/poc-05-hybrid-surface`

Base: latest `main` with the POC-03 scene branch stacked above it

## Current result

The branch is rebased to POC-03 `84b3aaf`. The implementation consumes the
normative View/Camera/Surface C ABI rather than POC-03 C++ Scene types. On this
development machine, the updated host suite passed 8/8 in Debug and 8/8 with
sanitizers. The separate POC-03 contract test confirms the reserved, empty
ExternalSurface pass remains outside the Canvas draw list.

The Web Playwright harness passed 3/3 locally for a real iframe and HTML video
element. This run used the available Node 22.14.0 rather than the locked Node
24.18.0, so the formal Web artifact remains Pending until `poc05.yml` runs with
the locked toolchain.

Windows WebView2, Android WebView/VideoView and Apple native overlay adapters
remain Pending. No physical-device gate is claimed by this document.

## Acceptance checklist

- [x] POC-03 reserved pass and Public C ABI boundary tests pass after rebase.
- [x] Host lifecycle/focus/failure contract tests pass after C ABI migration.
- [ ] Web placement ≤ 1 device pixel and update ≤ 2 frames artifact.
- [ ] Web 100 lifecycle memory/active-count artifact.
- [ ] Windows WebView2/video adapter and physical report.
- [ ] Android WebView/VideoView adapter, 16 KiB checks and physical report.
- [ ] Apple native overlay adapter and physical report.
- [ ] Product accepts the controlled-overlay z-order restriction.
