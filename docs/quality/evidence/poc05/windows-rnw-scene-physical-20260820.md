# POC-05 Windows RNW/Fabric 100K scene physical validation — 2026-08-20

> This physical result is included by the later
> [POC-05 consolidated report](../../../evidence/poc05/consolidated-validation-20260820.md),
> which accepts the controlled-overlay risk proof while retaining the private
> Scene bridge as non-conforming product integration work.

Status: **Scoped physical Shell evidence passed; included in the consolidated
POC-05 Accepted architecture risk proof.**

The Windows RNW New Architecture/Fabric Shell was built in Release mode and
run on the physical Windows machine. Its native CanvasSurface uses Skia Ganesh
over D3D12 and the temporary private POC-03 `RuntimeScene` bridge, configured
with the same 100K scene parameters as the Android runner. WebView2 and video
overlays were mounted through `WindowsRnwFabricExternalSurfaceHost`.

| Check | Result |
| --- | --- |
| POC-03 scene nodes | 100,000 |
| Canvas renderer | Skia Ganesh / D3D12 |
| RGBA readback probe | Passed |
| Native render count | 10 |
| Overlay create / placement | 2 / 18 |
| stale frame / backend failures | 0 / 0 |
| Active surfaces | 2 (WebView2 + video) |
| GPU | Intel UHD Graphics 630 |
| Window | 96 DPI, 1164×727 pixels |
| Process | Responding; approximately 172 MB working set during capture |

The raw report is generated beside the Release executable and archived by its
SHA-256 `c0ca41433475ff0b5e5a776bb220f526eeba2d10a97866bd2dbefd1710ef5079`.
The structured copy is [windows-rnw-scene-physical-20260820.json](windows-rnw-scene-physical-20260820.json).

This is a Windows validation bridge, not final Runtime C ABI conformance:
`runtime_c_abi_binary_conformance` remains `false`, and the private POC-03 C++
Scene dependency must be removed before POC-05 can be accepted.
