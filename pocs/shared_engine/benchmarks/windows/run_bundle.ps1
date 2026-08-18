param(
    [Parameter(Mandatory = $true)][string]$NativeExe,
    [Parameter(Mandatory = $true)][string]$WebUrl,
    [Parameter(Mandatory = $true)][string]$OutputDirectory,
    [int]$DurationSeconds = 60,
    [string]$RuntimeCommit = "5ab8b16bdac8f982a9d221d1f48d3867dda7b43c"
)

$ErrorActionPreference = "Stop"
$expectedDigest = "47826449b895ac4f4a57b4f386379775"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "../../../..")).Path
$output = New-Item -ItemType Directory -Force $OutputDirectory
$nativeExePath = (Resolve-Path $NativeExe).Path
$nativeRgba = Join-Path $output "windows-hardware-actual.rgba"
$webRgba = Join-Path $output "web-hardware-actual.rgba"
$nativeJson = Join-Path $output "windows-hardware-result.json"
$webJson = Join-Path $output "web-hardware-result.json"
$started = [DateTime]::UtcNow

& $nativeExePath --hardware --offscreen --lifecycle=100 "--smoke=$DurationSeconds" "--output=$nativeRgba" |
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
node $webScript --url $WebUrl --chrome $chrome --seconds $DurationSeconds `
    --output $webJson --rgba-output $webRgba
if ($LASTEXITCODE -ne 0) { throw "Web hardware benchmark failed" }
$webResult = Get-Content $webJson -Raw | ConvertFrom-Json

foreach ($record in @($nativeResult, $webResult)) {
    if ($record.digest -ne $expectedDigest) { throw "$($record.platform) digest mismatch" }
    if ($record.lifecycle -ne 100) { throw "$($record.platform) lifecycle mismatch" }
    if ($record.smoke_seconds -ne $DurationSeconds) { throw "$($record.platform) smoke duration mismatch" }
    if ($record.smoke_frames -le 0) { throw "$($record.platform) has no smoke frames" }
    if ($record.max_ms -gt 100) { throw "$($record.platform) frame exceeded 100 ms" }
    foreach ($field in @("p50_ms", "p95_ms", "p99_ms", "max_ms", "peak_memory_bytes")) {
        if ($null -eq $record.$field) { throw "$($record.platform) missing $field" }
    }
}

$nativeMemoryAnalysis = Join-Path $output "windows-memory-analysis.json"
python (Join-Path $repoRoot "pocs/shared_engine/tools/memory_series.py") `
    $nativeJson --output $nativeMemoryAnalysis
if ($LASTEXITCODE -ne 0) { throw "Native working-set series shows sustained growth" }

function Observation($value, [string]$method, [string]$unavailableReason = "") {
    if ($null -eq $value -or "$value" -eq "") {
        return @{ status = "unavailable"; value = $null; observation_method = $method; reason = $unavailableReason }
    }
    return @{ status = "observed"; value = $value; observation_method = $method }
}

$powerPlan = (powercfg /GETACTIVESCHEME 2>&1 | Out-String).Trim()
$videoControllers = @(Get-CimInstance Win32_VideoController -ErrorAction SilentlyContinue)
$currentRefreshRates = @($videoControllers | ForEach-Object { $_.CurrentRefreshRate } | Where-Object { $null -ne $_ -and $_ -gt 0 } | Sort-Object -Unique)
$displayControllers = Get-CimInstance -Namespace root\wmi -ClassName WmiMonitorListedSupportedSourceModes -ErrorAction SilentlyContinue
$supportedRefreshRates = @($displayControllers | ForEach-Object { $_.MonitorSourceModes } | ForEach-Object { $_.VerticalRefreshRate } | Sort-Object -Unique)
$thermalZones = Get-CimInstance -Namespace root\wmi -ClassName MSAcpi_ThermalZoneTemperature -ErrorAction SilentlyContinue
$thermalValues = @($thermalZones | ForEach-Object { [math]::Round(($_.CurrentTemperature / 10) - 273.15, 1) })
$environment = @{
    thermal = Observation $thermalValues "WMI MSAcpi_ThermalZoneTemperature" "firmware exposes no ACPI thermal zone"
    power_mode = Observation $powerPlan "powercfg /GETACTIVESCHEME"
    display_refresh = @{
        current_hz = Observation $currentRefreshRates "CIM Win32_VideoController.CurrentRefreshRate" "display driver exposes no current refresh rate"
        supported_hz = Observation $supportedRefreshRates "WMI WmiMonitorListedSupportedSourceModes" "display driver exposes no source modes"
    }
    target_frame_interval = @{
        status = "unbounded-submit-loop"
        value_ms = $null
        observation_method = "POC-01 native/Web benchmark loops measure draw-submit throughput, not presentation cadence"
    }
    vrr = Observation $null "Windows public user-mode query not implemented by this POC harness" "active VRR state unavailable"
    browser_throttling = @{
        status = $webResult.browser_throttling.status
        flags = $webResult.browser_throttling.flags
        visibility_state = $webResult.browser_throttling.visibility_state
        document_has_focus = $webResult.browser_throttling.document_has_focus
        observation_method = $webResult.browser_throttling.observation_method
    }
}

$skiaLockPath = Join-Path $repoRoot "skia-sdk.lock.json"
$skiaLock = Get-Content $skiaLockPath -Raw | ConvertFrom-Json
$device = @{
    schema_version = 1
    started_at_utc = $started.ToString("o")
    completed_at_utc = [DateTime]::UtcNow.ToString("o")
    runtime_commit = $RuntimeCommit
    harness_commit = (git -C $repoRoot rev-parse HEAD).Trim()
    skia_commit = $skiaLock.skia_commit
    os = Get-CimInstance Win32_OperatingSystem | Select-Object Caption, Version, BuildNumber
    cpu = Get-CimInstance Win32_Processor | Select-Object Name, Manufacturer, NumberOfCores, NumberOfLogicalProcessors
    gpu = $videoControllers | Select-Object Name, AdapterCompatibility, DriverVersion, AdapterRAM, CurrentRefreshRate
    chrome = (Get-Item $chrome).VersionInfo.ProductVersion
    native = $nativeResult
    web = $webResult
    performance_environment = $environment
    reproduction = @{
        native = "canvas_poc01_windows.exe --hardware --offscreen --lifecycle=100 --smoke=$DurationSeconds --output=windows-hardware-actual.rgba"
        web = "node hardware_benchmark.mjs --url <local-url> --chrome <chrome-stable> --seconds $DurationSeconds --output=web-hardware-result.json --rgba-output=web-hardware-actual.rgba"
    }
}
$device | ConvertTo-Json -Depth 12 | Set-Content (Join-Path $output "device.json") -Encoding utf8

python (Join-Path $repoRoot "pocs/shared_engine/tools/visual_compare.py") `
    --expected (Join-Path $repoRoot "pocs/shared_engine/goldens/reference.rgba") `
    --actual $nativeRgba `
    --artifacts (Join-Path $output "windows-visual") `
    --backend ganesh-d3d12-hardware `
    --skia-commit $skiaLock.skia_commit
if ($LASTEXITCODE -ne 0) { throw "Native visual gate failed" }

python (Join-Path $repoRoot "pocs/shared_engine/tools/visual_compare.py") `
    --expected (Join-Path $repoRoot "pocs/shared_engine/goldens/reference.rgba") `
    --actual $webRgba `
    --artifacts (Join-Path $output "web-visual") `
    --backend ganesh-webgl2-hardware `
    --skia-commit $skiaLock.skia_commit
if ($LASTEXITCODE -ne 0) { throw "Web visual gate failed" }

$externalHashTargets = @(
    $nativeExePath,
    (Join-Path $repoRoot "out/web-release/pocs/shared_engine/platform/web/canvas_poc01_web.js"),
    (Join-Path $repoRoot "out/web-release/pocs/shared_engine/platform/web/canvas_poc01_web.wasm"),
    $skiaLockPath,
    (Join-Path $repoRoot "pocs/shared_engine/fixtures/manifest.json"),
    (Join-Path $repoRoot "pocs/shared_engine/fixtures/scene.ndjson"),
    (Join-Path $repoRoot "pocs/shared_engine/fixtures/checker.png"),
    (Join-Path $repoRoot "pocs/shared_engine/goldens/reference.rgba")
)
$bundleHashTargets = Get-ChildItem $output -File -Recurse | Where-Object {
    $_.Name -ne "artifact-hashes.json"
} | Select-Object -ExpandProperty FullName
$hashes = @($externalHashTargets) + @($bundleHashTargets) | ForEach-Object {
    $resolved = (Resolve-Path $_).Path
    $hash = Get-FileHash $resolved -Algorithm SHA256
    [PSCustomObject]@{
        file = [System.IO.Path]::GetRelativePath($repoRoot, $resolved).Replace("\", "/")
        bytes = (Get-Item $resolved).Length
        sha256 = $hash.Hash.ToLowerInvariant()
    }
}
$hashes | ConvertTo-Json | Set-Content (Join-Path $output "artifact-hashes.json") -Encoding utf8

$zip = "$($output.FullName).zip"
Compress-Archive -Path (Join-Path $output "*") -DestinationPath $zip -Force
$zipHash = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLowerInvariant()
Write-Host "Benchmark bundle: $zip"
Write-Host "Benchmark bundle SHA-256: $zipHash"
