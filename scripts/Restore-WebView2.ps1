param(
    [string]$Version = '1.0.4078.44'
)

$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$output = Join-Path $repoRoot 'third_party/nuget'
$packageRoot = Join-Path $output 'Microsoft.Web.WebView2'
$header = Join-Path $packageRoot 'build/native/include/WebView2.h'
$staticLoader = Join-Path $packageRoot 'build/native/x64/WebView2LoaderStatic.lib'

if ((Test-Path -LiteralPath $header -PathType Leaf) -and
    (Test-Path -LiteralPath $staticLoader -PathType Leaf)) {
    exit 0
}

# NuGet treats an existing -ExcludeVersion package directory as installed, so
# remove a partial package before restoring a missing artifact.
if (Test-Path -LiteralPath $packageRoot -PathType Container) {
    Remove-Item -LiteralPath $packageRoot -Recurse -Force
}

New-Item -ItemType Directory -Force -Path $output | Out-Null
& nuget install Microsoft.Web.WebView2 `
    -Version $Version `
    -OutputDirectory $output `
    -ExcludeVersion `
    -NonInteractive
if ($LASTEXITCODE -ne 0) {
    throw "NuGet failed to restore Microsoft.Web.WebView2 $Version (exit $LASTEXITCODE)"
}

if (-not (Test-Path -LiteralPath $header -PathType Leaf)) {
    throw "WebView2 restore completed without the expected header: $header"
}
if (-not (Test-Path -LiteralPath $staticLoader -PathType Leaf)) {
    throw "WebView2 restore completed without the expected static loader: $staticLoader"
}
