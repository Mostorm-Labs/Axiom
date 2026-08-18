# POC-01 Apple/macOS Supplemental Physical Evidence — 2026-08-18

> Result: **iPhone and iPadOS passed; macOS failed.** POC-01 remains
> `Validating`. The macOS failures are retained as acceptance evidence and no
> third physical rerun is permitted for this evidence cycle.

## Scope and identity

This report records clean Release builds and physical execution from source
commit `06b61f3c7506f93d380765cdcaca59637bdf2644`, the squash-merged result of
[PR #12](https://github.com/Mostorm-Labs/canvas/pull/12). The same locked C++20
Runtime, fixtures, Skia commit, thresholds, and memory decision rule were used
for the three Apple targets.

| Input | Identity |
| --- | --- |
| Canvas source under test | `06b61f3c7506f93d380765cdcaca59637bdf2644` |
| Skia source | `b6d106297ff9ef2ff8094033695d045e87775581` |
| Skia SDK set | `debcbb7b9376806c94ffb9af5950ebd8a6de0547833f9b57df96a20531ca7817` |
| Toolchain | Xcode 26.2 (`17C52`), macOS/iPhoneOS SDK 26.2 |
| Deployment target | 17.0 |
| macOS runner SHA-256 | `6a775f51042307b4e9dbd6bf71a9e2d068a4486a2ec75aa2a069318e45cfedca` |
| iPhoneOS runner SHA-256 | `463a5e742434f2692b898ec633eb79c8f53dc36c1b80fe2d242abf42f545156b` |
| Replay fixture SHA-256 | `110a9572d54864ae913e28d91a5e392135e22c1dbedc06a88db6d7b425d25724` |
| Reference RGBA SHA-256 | `1b1e4a77a213515469b094ccb77b43be5c75fa7f1d2382f38583ed8aaab51041` |

[`manifest.json`](manifest.json) is the machine-readable report. Sanitized
results and visual metrics are committed under [`results/`](results/). Raw
RGBA, normalized and raw runner output, rendered expected/actual/diff images,
host observations, and file-level hashes are retained in the external
content-addressed evidence asset.

## Gate results

| Target and run | Transport | Lifecycle / smoke | Maximum frame | Memory decision | Visual | Result |
| --- | --- | --- | ---: | --- | --- | --- |
| iPhone 15 Pro, iOS 26.6 | Local network | 100 / 60 s, 3,600 frames | 6.93175 ms | 0% tail growth; pass | 99.982708% within ±2; pass | **Passed** |
| iPad Air (4th generation), iPadOS 26.6 | Wired | 100 / 60 s, 3,600 frames | 4.98487 ms | 0.065829% tail growth; pass | 99.982708% within ±2; pass | **Passed** |
| macOS, first clean Release attempt | Local | Workload stopped at failed smoke gate | 104.214750 ms | Not evaluated after frame failure | 99.982708% within ±2; pass | **Failed** |
| macOS, only bounded rerun | Local | 100 / 60 s, 3,554 frames | 49.3456 ms | 10.635462% tail growth; fail | 99.982708% within ±2; pass | **Failed** |

Every completed correctness path reports document digest
`47826449b895ac4f4a57b4f386379775`, numeric corpus
`e8f3bc4f06282fc0a2348aa5059d56fa`, and independent replay digest
`bcdb19afb9eccbf68cec4a7f442b1cd2` at revision 1 and sequence 4.

### Apple mobile

The iPhone and iPadOS runs each contain 13 process physical-footprint samples
spanning at least 60 seconds at a nominal five-second interval. The fixed
decision rule compares medians of the first and last quartile windows and
fails when tail growth exceeds 5%.

The iPhone head and tail medians were both 69,485,464 bytes. The iPad head
median was 24,888,896 bytes and the tail median was 24,905,280 bytes, a
16,384-byte increase. Both pass the unchanged rule. Thermal state remained
`nominal`, low-power mode was disabled, and the maximum exposed refresh rates
were 120 Hz and 60 Hz respectively. Active VRR is unavailable through public
Apple APIs and is explicitly recorded as such.

The initial iPad installation was rejected before the workload began because
the device no longer accepted the stale development signing identity. Xcode
automatic development signing refreshed the profile, after which the clean
Release runner was installed and the full iPadOS gate passed. The sanitized
pre-workload failure is retained; signing account, team, certificate, profile,
and device identifiers are excluded.

### macOS blocker and rerun policy

The first clean Release macOS run failed the unchanged 100 ms maximum-frame
ceiling at 104.214750 ms. Its visual readback passed and the failure is retained
instead of being replaced by a better run. One bounded rerun was allowed
without a source, fixture, build-configuration, threshold, or environment
change. That rerun passed the frame ceiling at 49.3456 ms but failed the fixed
memory rule: the first-window median was 45,367,920 bytes and the last-window
median was 50,193,008 bytes, a 4,825,088-byte or 10.635462% increase.

No third macOS rerun is permitted for this evidence cycle. The earlier
historical macOS physical observation of 148.524167 ms also remains a failed
observation; it is not reclassified or superseded by this supplemental run.
The two clean Release failures are sufficient to keep macOS performance and
memory stability open as a POC-01 blocker.

## External evidence and privacy

The immutable prerelease is:

- Release: [POC-01 Apple/macOS supplemental evidence 2026-08-18](https://github.com/Mostorm-Labs/canvas/releases/tag/poc01-apple-macos-supplemental-2026-08-18-55b28935b59bbef2)
- Asset: `poc01-apple-macos-supplemental-2026-08-18.zip`
- Asset size: 57,292 bytes; uncompressed payload size: 7,755,473 bytes
- Asset SHA-256: `55b28935b59bbef2325218133f9e686f6e67d2a09bc2b90f96075fa7c49c86a8`
- Contents: 34 file members; `SHA256SUMS.json` records all 33 other members

The GitHub asset digest, downloaded ZIP digest, byte count, and every retained
member hash were verified. Once referenced by this report, the tag and asset
must not be overwritten or deleted; corrections require a new content-addressed
Release.

The report and asset omit device identifiers, user and host names, absolute
paths, network addresses, proxy settings, signing identities, team IDs,
provisioning profiles, and account details. The private signing log and
embedded profile were not packaged or committed.

## Decision and remaining work

This report closes the missing iPhone and iPadOS post-warm-up memory-series and
performance-environment evidence. It does **not** pass macOS: both the retained
frame failure and the bounded-rerun memory failure remain blocking observations.
The Windows/Web physical bundle must also be regenerated with the revised
`run_bundle.ps1` so that Windows working-set samples and the updated environment
metadata are retained and reviewed.

POC-01 therefore remains `Validating`. Acceptance still requires a technical
resolution and fresh, explicitly authorized validation of the macOS blocker,
revised Windows/Web physical evidence, and final aggregate review/publication.
