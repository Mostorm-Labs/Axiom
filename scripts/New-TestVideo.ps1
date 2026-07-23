param([string]$Output = "tests\fixtures\test-pattern-1080p30.mp4")

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$requested = if ([IO.Path]::IsPathRooted($Output)) {
    [IO.Path]::GetFullPath($Output)
} else {
    [IO.Path]::GetFullPath((Join-Path $repoRoot $Output))
}

if (Test-Path -LiteralPath $requested) {
    $existing = Get-Item -LiteralPath $requested
    if ($existing.PSIsContainer -or $existing.Length -le 0) {
        throw "The existing test-video path is not a non-empty file: $requested"
    }
    Write-Host "Reusing existing test video: $requested"
    exit 0
}

$ffmpeg = Get-Command ffmpeg -CommandType Application -ErrorAction SilentlyContinue
if (-not $ffmpeg) {
    throw "ffmpeg is required to generate the Canvas test video."
}

$parent = Split-Path -Parent $requested
if (-not $parent) { throw "The test-video output needs a parent directory." }
New-Item -ItemType Directory -Force -Path $parent | Out-Null
$temporary = Join-Path $parent (".canvas-test-video-{0}.tmp.mp4" -f [guid]::NewGuid())

try {
    & $ffmpeg.Source -hide_banner -loglevel error -nostdin -y `
        -f lavfi -i "testsrc2=size=1920x1080:rate=30" `
        -t 60 -c:v libx264 -pix_fmt yuv420p $temporary
    if ($LASTEXITCODE -ne 0) {
        throw "ffmpeg failed with exit code $LASTEXITCODE."
    }
    if (-not (Test-Path -LiteralPath $temporary) -or
        (Get-Item -LiteralPath $temporary).Length -le 0) {
        throw "Test video was not generated."
    }
    Move-Item -LiteralPath $temporary -Destination $requested
    Write-Host "Generated test video: $requested"
} finally {
    if (Test-Path -LiteralPath $temporary) {
        Remove-Item -LiteralPath $temporary -Force
    }
}
