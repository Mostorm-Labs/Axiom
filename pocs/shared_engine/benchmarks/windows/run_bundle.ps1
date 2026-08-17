param(
    [Parameter(Mandatory = $true)][string]$NativeExe,
    [Parameter(Mandatory = $true)][string]$WebUrl,
    [Parameter(Mandatory = $true)][string]$OutputDirectory,
    [int]$DurationSeconds = 60
)

$ErrorActionPreference = "Stop"
$output = New-Item -ItemType Directory -Force $OutputDirectory
$nativeRgba = Join-Path $output "windows-hardware-actual.rgba"
$nativeJson = Join-Path $output "windows-hardware-result.json"

& $NativeExe --hardware --offscreen --lifecycle=100 "--smoke=$DurationSeconds" "--output=$nativeRgba" |
    Tee-Object -FilePath $nativeJson
if ($LASTEXITCODE -ne 0) { throw "Native hardware benchmark failed" }
$nativeResult = Get-Content $nativeJson -Raw | ConvertFrom-Json
if ($nativeResult.warp) { throw "Hardware bundle must not use WARP" }

$chromeCandidates = @(
    "$env:ProgramFiles\Google\Chrome\Application\chrome.exe",
    "${env:ProgramFiles(x86)}\Google\Chrome\Application\chrome.exe"
)
$chrome = $chromeCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $chrome) { throw "Chrome Stable is not installed" }

$webScript = Join-Path $PSScriptRoot "../../platform/web/tools/hardware_benchmark.mjs"
node $webScript --url $WebUrl --chrome $chrome --seconds $DurationSeconds --output (Join-Path $output "web-hardware-result.json")
if ($LASTEXITCODE -ne 0) { throw "Web hardware benchmark failed" }

$device = @{
    generated_at_utc = [DateTime]::UtcNow.ToString("o")
    os = Get-CimInstance Win32_OperatingSystem | Select-Object Caption, Version, BuildNumber
    cpu = Get-CimInstance Win32_Processor | Select-Object Name, Manufacturer, NumberOfCores, NumberOfLogicalProcessors
    gpu = Get-CimInstance Win32_VideoController | Select-Object Name, AdapterCompatibility, PNPDeviceID, DriverVersion, AdapterRAM
    chrome = (Get-Item $chrome).VersionInfo.ProductVersion
    native = $nativeResult
}
$device | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $output "device.json") -Encoding utf8

$hashes = Get-ChildItem $output -File | ForEach-Object {
    $hash = Get-FileHash $_.FullName -Algorithm SHA256
    [PSCustomObject]@{ file = $_.Name; sha256 = $hash.Hash.ToLowerInvariant() }
}
$hashes | ConvertTo-Json | Set-Content (Join-Path $output "artifact-hashes.json") -Encoding utf8
Compress-Archive -Path (Join-Path $output "*") -DestinationPath "$($output.FullName).zip" -Force
Write-Host "Benchmark bundle: $($output.FullName).zip"
