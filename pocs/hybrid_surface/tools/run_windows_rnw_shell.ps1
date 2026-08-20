[CmdletBinding()]
param(
  [switch]$NoLaunch
)

$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "../../..")).Path
$reactNativeRoot = Join-Path $repo "pocs/hybrid_surface/platform/react_native"
$windowsRoot = Join-Path $reactNativeRoot "windows"
$solution = Join-Path $windowsRoot "AxiomPoc05.sln"
$project = Join-Path $windowsRoot "AxiomPoc05/AxiomPoc05.vcxproj"
$output = Join-Path $windowsRoot "x64/Release"
$executable = Join-Path $output "AxiomPoc05.exe"

if (-not (Test-Path -LiteralPath (Join-Path $reactNativeRoot "node_modules/react-native-windows/package.json"))) {
  Push-Location $reactNativeRoot
  try {
    npm ci
    if ($LASTEXITCODE -ne 0) { throw "npm ci failed with exit code $LASTEXITCODE." }
  } finally {
    Pop-Location
  }
}

Push-Location $reactNativeRoot
try {
  node node_modules/react-native/cli.js codegen-windows --check
  if ($LASTEXITCODE -ne 0) { throw "RNW codegen check failed with exit code $LASTEXITCODE." }
  node node_modules/react-native/cli.js autolink-windows --check
  if ($LASTEXITCODE -ne 0) { throw "RNW autolink check failed with exit code $LASTEXITCODE." }
} finally {
  Pop-Location
}

$msbuild = (Get-Command msbuild.exe -ErrorAction SilentlyContinue).Source
if (-not $msbuild) {
  $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio/Installer/vswhere.exe"
  if (Test-Path -LiteralPath $vswhere) {
    $installation = & $vswhere -latest -products * `
      -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
      -property installationPath
    if ($installation) {
      $candidate = Join-Path $installation "MSBuild/Current/Bin/MSBuild.exe"
      if (Test-Path -LiteralPath $candidate) { $msbuild = $candidate }
    }
  }
}
if (-not $msbuild) {
  throw "MSBuild with the Visual Studio C++ workload was not found."
}

& $msbuild $project /m /t:Build `
  /p:Configuration=Release /p:Platform=x64 `
  "/p:SolutionDir=$windowsRoot\" `
  "/p:SolutionPath=$solution" `
  /p:SolutionFileName=AxiomPoc05.sln `
  /p:WindowsTargetPlatformVersion=10.0.26100.0 `
  /p:PlatformToolset=v145
if ($LASTEXITCODE -ne 0) {
  throw "AxiomPoc05 Release build failed with exit code $LASTEXITCODE."
}

foreach ($required in @(
    $executable,
    (Join-Path $output "Microsoft.ReactNative.dll"),
    (Join-Path $output "Microsoft.WindowsAppRuntime.dll"),
    (Join-Path $output "Bundle/index.windows.bundle"))) {
  if (-not (Test-Path -LiteralPath $required)) {
    throw "The self-contained RNW Shell is missing required output: $required"
  }
}

Write-Host "POC-05 RNW New Architecture/Fabric Shell built: $executable"
if (-not $NoLaunch) {
  Start-Process -FilePath $executable -WorkingDirectory $output
}
