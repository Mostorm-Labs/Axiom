[CmdletBinding()]
param(
  [string]$BuildDir = "build-poc05-win",
  [string]$Output = "poc05-windows-rnw-webview2.json",
  [string]$WebView2SdkRoot = "",
  [string]$SkiaSdkRoot = ""
)

$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "../../..")).Path
if (-not $WebView2SdkRoot) {
  $WebView2SdkRoot = Join-Path $env:USERPROFILE ".nuget/packages/microsoft.web.webview2/1.0.2592.51/build/native"
}
if (-not $SkiaSdkRoot) {
  $SkiaSdkRoot = Join-Path $repo ".deps/skia-sdk/windows-x64-d3d12"
}
$skiaConfig = Join-Path $SkiaSdkRoot "lib/cmake/CanvasSkia/CanvasSkiaConfig.cmake"
if (-not (Test-Path -LiteralPath $skiaConfig)) {
  if ([System.IO.Path]::GetFullPath($SkiaSdkRoot) -ne
      [System.IO.Path]::GetFullPath((Join-Path $repo ".deps/skia-sdk/windows-x64-d3d12"))) {
    throw "Pinned Skia SDK not found at the explicit -SkiaSdkRoot path."
  }
  python (Join-Path $repo "tools/skia/fetch.py") --target windows-x64-d3d12
  if ($LASTEXITCODE -ne 0) { throw "Pinned Windows Skia SDK fetch/verification failed." }
}
$sdkHeader = Join-Path $WebView2SdkRoot "include/WebView2.h"
$sdkLoader = Join-Path $WebView2SdkRoot "x64/WebView2LoaderStatic.lib"
if (-not (Test-Path -LiteralPath $sdkHeader) -or
    -not (Test-Path -LiteralPath $sdkLoader)) {
  throw "Pinned WebView2 SDK not found. Pass -WebView2SdkRoot or restore Microsoft.Web.WebView2 1.0.2592.51."
}
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
  $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio/Installer/vswhere.exe"
  $installation = if (Test-Path -LiteralPath $vswhere) {
    & $vswhere -latest -products * `
      -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
      -property installationPath
  }
  if ($installation) {
    throw "MSVC environment is not loaded. Open an x64 Native Tools/Developer PowerShell for $installation and rerun this script."
  }
  throw "MSVC x64 build tools are unavailable. Install the Visual Studio C++ workload."
}

$buildPath = Join-Path $repo $BuildDir
$webView2CMakePath = $WebView2SdkRoot.Replace('\', '/')
$skiaCMakePath = $SkiaSdkRoot.Replace('\', '/')
cmake -S $repo -B $buildPath -G Ninja `
  -DCANVAS_BUILD_POC01=OFF -DCANVAS_BUILD_POC03=ON `
  -DCANVAS_POC03_BUILD_TESTS=OFF -DCANVAS_BUILD_POC05=ON `
  -DCANVAS_POC05_BUILD_TESTS=OFF -DCANVAS_POC05_ENABLE_SKIA=ON `
  "-DCANVAS_POC05_WEBVIEW2_SDK_ROOT:PATH=$webView2CMakePath" `
  "-DCANVAS_POC05_SKIA_SDK_ROOT:PATH=$skiaCMakePath"
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed with exit code $LASTEXITCODE." }
cmake --build $buildPath --target canvas_poc05_windows_runner --parallel
if ($LASTEXITCODE -ne 0) { throw "CMake build failed with exit code $LASTEXITCODE." }

$runner = Join-Path $repo "$BuildDir/pocs/hybrid_surface/platform/windows/canvas_poc05_windows_runner.exe"
$oldOutput = Join-Path $repo "poc05-windows-rnw-webview2.json"
if (Test-Path -LiteralPath $oldOutput) { Remove-Item -LiteralPath $oldOutput -Force }
$process = Start-Process -FilePath $runner -WorkingDirectory $repo -Wait -PassThru
if ($process.ExitCode -ne 0) { throw "POC-05 Windows native runner failed with exit code $($process.ExitCode)." }
if (-not (Test-Path -LiteralPath $oldOutput)) { throw "Runner did not produce $oldOutput." }

$result = Get-Content -LiteralPath $oldOutput -Raw | ConvertFrom-Json
if ($result.status -ne "physical-runner-ready" -or
    $result.canvas_renderer -ne "skia-ganesh-d3d12" -or
    $result.skia_visible -ne $true -or
    $result.registry.backend_failure_count -ne 0 -or
    $result.registry.active_surface_count -ne 2) {
  throw "POC-05 Windows native evidence failed registry/status checks."
}
$destination = [System.IO.Path]::GetFullPath((Join-Path $repo $Output))
if ($destination -ne $oldOutput) {
  Copy-Item -LiteralPath $oldOutput -Destination $destination -Force
}
Write-Host "POC-05 Windows RNW/Fabric peer validation passed: $destination"
