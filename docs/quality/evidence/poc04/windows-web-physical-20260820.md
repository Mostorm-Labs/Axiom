# POC-04 Windows/Web physical IME revalidation — 2026-08-20

Status: **Passed for the Windows IMM and Chrome Stable semantic-result gates.**
POC-04 remains `Validating`; this evidence does not mark it `Accepted` and does
not claim React Native Windows (RNW) coverage.

The Win32 recorder and Chrome Stable recorder ran on the same physical Windows
machine. Both completed the controlled Microsoft Pinyin flow `ni hao → 你好`
and passed `tools/poc04/validate_physical_ime.py`.

## Locked identity

| Item | Value |
| --- | --- |
| Runtime/source commit | `cc9a70e320d7c049b6abc0a374514e6ead15f125` |
| Skia commit | `b6d106297ff9ef2ff8094033695d045e87775581` |
| `skia-sdk.lock.json` SHA-256 | `ba8bbbf1a2db281b41bc60719dd63c2383fe100523ec3cd15ddbad727ecc6199` |
| Skia SDK set ID | `72f006b19ac77233cd6187f2cac5476b6eda041043ae0b646f1a3e7f5c1b3674` |
| Windows SDK ID | `2154617d78bb73556e8238901e4ee1a233eded9bd0897ad795124772dc03902c` |
| Web SDK ID | `875f2519b656fbfd42c42596551c3e836d2d29657a021b01ca23c5521f1c9db2` |
| Fixture manifest SHA-256 | `77d4dacd86e174effa51347f65401a4a605e3222ece6de203e026461e338af26` |
| Fixture replay SHA-256 | `110a9572d54864ae913e28d91a5e392135e22c1dbedc06a88db6d7b425d25724` |
| Fixture checker SHA-256 | `10ee6bb34dfe7ba4d866c1bc7cb828a045ba48e97c971e2ca8df05f66df99f59` |
| Golden SHA-256 | `1b1e4a77a213515469b094ccb77b43be5c75fa7f1d2382f38583ed8aaab51041` |

## Test environment

| Item | Observation |
| --- | --- |
| Test date/time | 2026-08-20, Asia/Shanghai; Win32 artifact 12:41, Chrome artifact 13:00 |
| OS | Windows 10 Pro 10.0.19045, x64 |
| CPU | Intel Core i5-10400 |
| Hardware GPU | Intel UHD Graphics 630 |
| GPU driver | `31.0.101.2111` |
| Chrome Stable | `151.0.7922.138` (`Chrome/151.0.0.0` user agent) |
| IME | Microsoft Pinyin |

## Results

| Path | Protocol | Final text | Composition callbacks | Selection | Caret | Runtime digest | Lifecycle |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Windows | Win32 IMM | `你好` | start/update/commit observed | `[2, 0]` | `[32, 0, 1, 20]` | `022a3d56715929e23e94411fb71ffe50` | 100 cycles / 0 failures |
| Chrome Stable | composition/beforeinput | `你好` | start/update/end observed | `[2, 0]` | `[32, 0, 1, 20]` | `5ac9f9c00984080f7f59eef01fbf4993` | n/a |

Both reports set `controlled_flow_passed: true`. The browser report records the
full real composition/beforeinput event stream and explicitly identifies
installed Chrome Stable rather than Headless Chrome or Edge.

## Committed evidence and hashes

- [Windows IMM evidence](windows-ime-20260820.json) — SHA-256
  `dba10152710429292ba082b59e98bb95b6eeee199b43b4fd5791470364b9de7d`
- [Chrome Stable evidence](chrome-ime-20260820.json) — SHA-256
  `89c31b7418bcd0f7e3fc6ad0c8db340979b0a0431d4c73cb04d361708c5c844d`

Validation command:

```powershell
python tools/poc04/validate_physical_ime.py `
  docs/quality/evidence/poc04/windows-ime-20260820.json `
  docs/quality/evidence/poc04/chrome-ime-20260820.json
```

Result:

```text
PASS windows
PASS web
```

## Reproduction

```powershell
git switch codex/poc04-windows-web-revalidation
git pull --ff-only origin codex/poc04-windows-web-revalidation
powershell -ExecutionPolicy Bypass -File tools/poc04/prepare_windows_web_validation.ps1
```

Set `CANVAS_POC04_IME_EVIDENCE_PATH`, launch the Win32 recorder, select
Microsoft Pinyin, enter `ni hao`, commit `你好`, and close the window. Serve the
printed Chrome recorder directory on `127.0.0.1:4173`, repeat the same flow in
installed Chrome Stable, and download `poc04-chrome-ime.json`.

The preparation CTest run was 36/37: the sole failure was the pre-existing
`SkParagraphLayoutTest.FixedFontProducesLineAndSelectionGeometry` exception
(`input is truncated UTF-8`). It is retained here and is not represented as a
passing test.
