param(
    [string]$Version = '1.0.4078.44'
)

$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$output = Join-Path $repoRoot 'third_party/nuget'
$packageRoot = Join-Path $output 'Microsoft.Web.WebView2'
$header = Join-Path $packageRoot 'build/native/include/WebView2.h'

if (Test-Path -LiteralPath $header) {
    exit 0
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

if (-not (Test-Path -LiteralPath $header)) {
    throw "WebView2 restore completed without the expected header: $header"
}
