# POC-04 Windows/Web physical IME revalidation — 2026-08-20 (SkParagraph fix)

Status: **Passed for the Windows IMM and Chrome Stable semantic-result gates.**
POC-04 remains `Validating`; this bundle does not mark it `Accepted` and does
not claim React Native Windows (RNW) coverage.

This bundle was collected from the same physical Windows machine after
switching to the SkParagraph UTF-8 boundary fix at
`3d13271b27b1a38aef5c5ba3c80099cf4e8d7e7a`. Windows CTest passed 37/37 before
the interactive collection. Both recorders completed Microsoft Pinyin
`ni hao → 你好` and passed `tools/poc04/validate_physical_ime.py`.

## Identity and environment

| Item | Value |
| --- | --- |
| Runtime/source commit | `3d13271b27b1a38aef5c5ba3c80099cf4e8d7e7a` |
| Skia commit | `b6d106297ff9ef2ff8094033695d045e87775581` |
| Skia SDK set ID | `72f006b19ac77233cd6187f2cac5476b6eda041043ae0b646f1a3e7f5c1b3674` |
| Windows SDK ID | `2154617d78bb73556e8238901e4ee1a233eded9bd0897ad795124772dc03902c` |
| Web SDK ID | `875f2519b656fbfd42c42596551c3e836d2d29657a021b01ca23c5521f1c9db2` |
| OS | Windows 10 Pro 10.0.19045, x64 |
| CPU | Intel Core i5-10400 |
| GPU / driver | Intel UHD Graphics 630 / `31.0.101.2111` |
| Chrome Stable | `151.0.7922.138` (`Chrome/151.0.0.0`) |
| IME | Microsoft Pinyin |

## Results

| Path | Final text | Composition observations | Selection / caret | Runtime digest | Lifecycle |
| --- | --- | --- | --- | --- | --- |
| Windows IMM | `你好` | start/update/commit | `[2, 0]` / `[32, 0, 1, 20]` | `5ac9f9c00984080f7f59eef01fbf4993` | 100 / 0 failures |
| Chrome Stable | `你好` | start/update/end | `[2, 0]` / `[32, 0, 1, 20]` | `5ac9f9c00984080f7f59eef01fbf4993` | n/a |

Chrome evidence contains 23 real composition/beforeinput events and identifies
installed Chrome Stable, not Headless Chrome or Edge. The Chrome download was
written at 2026-08-20 16:55:09 Asia/Shanghai.

## Evidence hashes

- Windows IMM raw capture SHA-256:
  `8ab082a90a0b2ad78f507089ec52533a50ab3938f2dc8e17f9e94862df56feb0`
- Chrome Stable raw download SHA-256:
  `1b64bb6b8c5a5fe18c8c7bad760e812fe92f495e46324182e68656616501df5c`
- [Git-archived Windows IMM evidence](windows-ime-20260820-r2.json) SHA-256:
  `49ba7f7aec24dd28ff89b79c1b327c44d49ae43b0ef6366ecf0d3c830958df6c`
- [Git-archived Chrome Stable evidence](chrome-ime-20260820-r2.json) SHA-256:
  `89c31b7418bcd0f7e3fc6ad0c8db340979b0a0431d4c73cb04d361708c5c844d`
- `skia-sdk.lock.json` SHA-256:
  `ba8bbbf1a2db281b41bc60719dd63c2383fe100523ec3cd15ddbad727ecc6199`
- Fixture manifest / replay / checker SHA-256:
  `77d4dacd86e174effa51347f65401a4a605e3222ece6de203e026461e338af26` /
  `110a9572d54864ae913e28d91a5e392135e22c1dbedc06a88db6d7b425d25724` /
  `10ee6bb34dfe7ba4d866c1bc7cb828a045ba48e97c971e2ca8df05f66df99f59`

The committed JSON files are formatting-normalized, semantically equal to the
raw captures, and independently pass the same validator. The raw hashes retain
byte-level identity for external archival.

## Validation and reproduction

```powershell
python tools/poc04/validate_physical_ime.py `
  docs/quality/evidence/poc04/windows-ime-20260820-r2.json `
  docs/quality/evidence/poc04/chrome-ime-20260820-r2.json
```

Expected result:

```text
PASS windows
PASS web
```

The preparation command was run from x64 Visual Studio Developer PowerShell:

```powershell
git switch codex/poc04-windows-web-revalidation
git pull --ff-only origin codex/poc04-windows-web-revalidation
powershell -ExecutionPolicy Bypass -File tools/poc04/prepare_windows_web_validation.ps1
```

The fixed baseline's CTest result was 37/37. The previous 36/37
`input is truncated UTF-8` failure is not present on this commit.
