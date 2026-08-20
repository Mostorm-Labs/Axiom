# POC-04 Windows and Chrome Stable physical revalidation

Status: **ready to execute on the physical Windows machine.** This runbook does
not mark POC-04 `Accepted`; it prepares the two remaining Windows-hosted native
IME evidence records against one exact v2 Runtime source commit.

## Source baseline

Use the `codex/poc04-windows-web-revalidation` branch from
`https://github.com/Mostorm-Labs/Axiom.git`. Record `git rev-parse HEAD` in the
final report. Do not reuse binaries or evidence from the older
`codex/poc04-windows-web-physical` branch because that run consumed the v1 SDK.

## Prepare and run

Open an x64 Developer PowerShell and run:

```powershell
git remote set-url origin https://github.com/Mostorm-Labs/Axiom.git
git fetch origin
git switch codex/poc04-windows-web-revalidation
git pull --ff-only origin codex/poc04-windows-web-revalidation
powershell -ExecutionPolicy Bypass -File tools/poc04/prepare_windows_web_validation.ps1
```

The script performs the canonical Windows build/tests and Web/WASM build, then
prints two interactive commands.

### Win32 IMM

1. Set `CANVAS_POC04_IME_EVIDENCE_PATH` to the printed path.
2. Launch `canvas_poc04_windows_demo.exe`.
3. Select Microsoft Pinyin and type `ni hao`.
4. Commit the candidate `你好`, confirm the window shows
   `Runtime text: 你好`, and close the window.
5. Keep `windows-ime.json`; it must show the three composition observations,
   final text `你好`, selection/caret, digest, and 100 lifecycle cycles with no
   failures.

### Chrome Stable

1. Start the printed `python -m http.server` command.
2. Open the printed local URL in installed Chrome Stable, not Playwright's
   bundled Chromium.
3. Select Microsoft Pinyin, type `ni hao`, and commit `你好` in the recorder.
4. Confirm the status turns green and download `poc04-chrome-ime.json`.
5. The report must contain real `compositionstart/update/end` observations,
   final text `你好`, selection/caret, Runtime digest, and the browser user
   agent.

Validate both JSON files with `tools/poc04/validate_physical_ime.py`. Archive
the canonical `windows-behavior.json`, the two physical JSON files, Windows
version, Microsoft Pinyin version, Chrome version, CPU/GPU/driver identity,
source commit, and SDK IDs in the final evidence report.
