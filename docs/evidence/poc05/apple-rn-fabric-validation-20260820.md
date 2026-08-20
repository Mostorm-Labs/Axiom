# POC-05 Apple React Native/Fabric validation — 2026-08-20

Status: **RN/Fabric implementation and Apple physical-device manual gate passed**.

This report is separate from the earlier UIKit runner report. The application
in this report uses React Native **0.84.1** with Fabric codegen and a real
`RCTViewComponentView` named `AxiomHybridSurfaceComponentView`. The component
owns an `AxiomAppleHybridSurfaceHost`, which creates the native Canvas and the
controlled `WKWebView`/video overlays. React only changes low-frequency
visibility, failure, page and lifecycle-generation props.

## Implementation evidence

| Check | Result |
| --- | --- |
| React Native version | 0.84.1, locked in `package.json`/`package-lock.json` |
| Fabric codegen | `AxiomHybridSurface` → `AxiomHybridSurfaceComponentView` |
| Native component base | `RCTViewComponentView` |
| Canvas backend | Skia Ganesh / Metal on `CAMetalLayer` |
| External surfaces | Real `WKWebView` and `AVSampleBufferDisplayLayer` |
| Native frame scheduler | `CADisplayLink`; no per-frame JS props/events |
| JS-stall probe | 100 ms JS block while native placement continues |
| Runtime scene | 100,000-node POC-03 scene through the Apple adapter |
| Stable C ABI exposure | No Skia, Scene, GPU or native handles cross into JS |

The generated provider contains the runtime registration used by the app:

```objc
@{ @"AxiomHybridSurface":
       NSClassFromString(@"AxiomHybridSurfaceComponentView") }
```

The built binary contains `AxiomHybridSurfaceComponentView` and
`AxiomPoc05Probe`; the installed app displayed the RN/Fabric toolbar, the
Skia scene, a real WebView and the native video layer together.

## Build and launch evidence

The following was run from the repository root after installing the locked
Apple Skia SDK and React Native dependencies:

```sh
cd pocs/hybrid_surface/platform/react_native
npm ci
cd ios
pod install

# Simulator arm64 build
xcodebuild \
  -workspace AxiomPoc05.xcworkspace \
  -scheme AxiomPoc05 \
  -configuration Debug \
  -sdk iphonesimulator \
  -destination 'generic/platform=iOS Simulator' \
  -derivedDataPath build/DerivedData-arm64 \
  ARCHS=arm64 ONLY_ACTIVE_ARCH=YES \
  CODE_SIGNING_ALLOWED=NO build
```

The simulator build completed with `** BUILD SUCCEEDED **` and the app was
installed and launched on an iPad simulator. The accessibility tree exposed
the RN/Fabric toolbar controls, including `Hide Web`, `Fail Web`, `Page 2`,
`Recreate` and `Block JS 100ms`. The JS-stall probe reported native placement
frames while JavaScript was blocked.

For a physical device, use an Xcode account or an existing development
profile that includes the device. The profile is intentionally not committed:

```sh
xcodebuild \
  -workspace AxiomPoc05.xcworkspace \
  -scheme AxiomPoc05 \
  -configuration Debug \
  -sdk iphoneos \
  -destination 'id=<xcode-device-udid>' \
  -derivedDataPath build/DerivedData-device-rn \
  CODE_SIGN_STYLE=Automatic \
  DEVELOPMENT_TEAM=<team-id> \
  CODE_SIGN_IDENTITY='Apple Development' \
  PRODUCT_BUNDLE_IDENTIFIER=dev.mostorm.axiom.poc05.rnfabric build

xcrun devicectl device install app \
  --device <coredevice-id> \
  build/DerivedData-device-rn/Build/Products/Debug-iphoneos/AxiomPoc05.app
xcrun devicectl device process launch \
  --device <coredevice-id> \
  --console dev.mostorm.axiom.poc05.rnfabric
```

On 2026-08-20 the build was signed with an already-installed development
wildcard profile, then installed and launched on both the iPad Air 4
(`Piccaso`) and iPhone 15 Pro (`Mosaic`). Both physical-device runs used the
same React Native 0.84.1/Fabric component, native C++ runtime, Metal Canvas,
`WKWebView` and video-overlay implementation.

## Physical RN/Fabric gate

The following manual observations passed on both the iPad Air 4 and iPhone 15
Pro on 2026-08-20:

- single-finger pan outside the WebView;
- centered two-finger pan/zoom, including a one-finger-first release;
- WebView text focus, scrolling and IME input;
- Hide/Show Web, Fail/Recover, Page 1/2 and Recreate;
- `Block JS 100ms`: the native placement label must report at least one frame;
- background/foreground;
- no freeze, black frame, flash, stale placement or input interruption.

The WebView continues to own single-finger interaction inside its bounds;
Canvas single-finger pan was therefore evaluated outside the WebView, while
two-finger Canvas manipulation was evaluated across the composed surface.

The existing UIKit report remains valid for the native adapter, but is not
used as React Native/Fabric conformance evidence. The separate RN/Fabric runs
on both Apple form factors satisfy this conformance gate:

```json
{
  "react_native_fabric_conformance": true,
  "simulator_rn_fabric": true,
  "ipad_rn_fabric_launch": true,
  "ipad_rn_fabric_manual_gate": true,
  "iphone_rn_fabric": true,
  "iphone_rn_fabric_manual_gate": true
}
```
