[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$BuildDirectory,

    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,

    [Parameter(Mandatory = $true)]
    [string]$Version,

    [string]$ReadmePath = (Join-Path $PSScriptRoot '../packaging/windows/README.txt')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-NormalizedFullPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $fullPath = [IO.Path]::GetFullPath($Path)
    $root = [IO.Path]::GetPathRoot($fullPath)
    while ($fullPath.Length -gt $root.Length) {
        $lastCharacter = $fullPath[$fullPath.Length - 1]
        if ($lastCharacter -ne [IO.Path]::DirectorySeparatorChar -and
            $lastCharacter -ne [IO.Path]::AltDirectorySeparatorChar) {
            break
        }
        $fullPath = $fullPath.Substring(0, $fullPath.Length - 1)
    }
    return $fullPath
}

function Test-PathsOverlap {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Left,

        [Parameter(Mandatory = $true)]
        [string]$Right
    )

    $comparison = [StringComparison]::OrdinalIgnoreCase
    if ([string]::Equals($Left, $Right, $comparison)) {
        return $true
    }

    $separator = [string][IO.Path]::DirectorySeparatorChar
    $leftPrefix = if ($Left.EndsWith($separator, [StringComparison]::Ordinal)) {
        $Left
    } else {
        "$Left$separator"
    }
    $rightPrefix = if ($Right.EndsWith($separator, [StringComparison]::Ordinal)) {
        $Right
    } else {
        "$Right$separator"
    }
    return $Right.StartsWith($leftPrefix, $comparison) -or
        $Left.StartsWith($rightPrefix, $comparison)
}

function Assert-PathsDoNotOverlap {
    param(
        [Parameter(Mandatory = $true)]
        [string]$OutputPath,

        [Parameter(Mandatory = $true)]
        [string]$SourcePath,

        [Parameter(Mandatory = $true)]
        [string]$Description
    )

    if (Test-PathsOverlap $OutputPath $SourcePath) {
        throw "Output directory must not contain, equal, or be contained by $Description source: $SourcePath"
    }
}

function Assert-NotReparsePoint {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [string]$Description
    )

    $item = Get-Item -LiteralPath $Path -Force -ErrorAction Stop
    if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "$Description may not be a link or reparse point: $Path"
    }
    return $item
}

function Assert-NonEmptyFile {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [string]$Description
    )

    $item = Assert-NotReparsePoint $Path $Description
    if ($item.PSIsContainer -or $item.Length -le 0) {
        throw "$Description must be a non-empty file: $Path"
    }
    return $item
}

$versionPattern = '\A[A-Za-z0-9][A-Za-z0-9._-]{0,63}\z'
if (-not [regex]::IsMatch(
        $Version,
        $versionPattern,
        [System.Text.RegularExpressions.RegexOptions]::CultureInvariant)) {
    throw "Version must be 1-64 ASCII letters, digits, dots, underscores, or hyphens, and must start with a letter or digit."
}

$buildItem = Assert-NotReparsePoint $BuildDirectory 'Build directory'
if (-not $buildItem.PSIsContainer) {
    throw "Build directory must be a directory: $BuildDirectory"
}
$buildRoot = Get-NormalizedFullPath $buildItem.FullName
$readmeItem = Assert-NonEmptyFile $ReadmePath 'Portable-package README'
$readmeSource = Get-NormalizedFullPath $readmeItem.FullName
$outputRoot = Get-NormalizedFullPath $OutputDirectory
$executableSource = Join-Path $buildRoot 'canvas_windows.exe'
$webSource = Join-Path $buildRoot 'web'

$null = Assert-NonEmptyFile $executableSource 'Canvas executable'
$webItem = Assert-NotReparsePoint $webSource 'Embedded web asset directory'
if (-not $webItem.PSIsContainer) {
    throw "Embedded web asset path must be a directory: $webSource"
}
$webSource = Get-NormalizedFullPath $webItem.FullName
$null = Assert-NonEmptyFile (Join-Path $webSource 'richtext.html') 'Rich-text web entry point'
$null = Assert-NonEmptyFile (Join-Path $webSource 'video.html') 'Video web entry point'

$reparsePoints = @(Get-ChildItem -LiteralPath $webSource -Recurse -Force |
    Where-Object { ($_.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 })
if ($reparsePoints.Count -ne 0) {
    throw "Embedded web assets may not contain links or reparse points: $($reparsePoints[0].FullName)"
}

$javascriptAssets = @(Get-ChildItem -LiteralPath $webSource -File -Recurse -Force |
    Where-Object { $_.Extension -ceq '.js' })
if ($javascriptAssets.Count -eq 0) {
    throw "Embedded web assets must contain at least one JavaScript bundle: $webSource"
}
foreach ($asset in $javascriptAssets) {
    $null = Assert-NonEmptyFile $asset.FullName 'Embedded JavaScript asset'
}

Assert-PathsDoNotOverlap $outputRoot $buildRoot 'build directory'
Assert-PathsDoNotOverlap $outputRoot $webSource 'embedded web directory'
Assert-PathsDoNotOverlap $outputRoot $readmeSource 'package README'

if (Test-Path -LiteralPath $outputRoot) {
    $outputItem = Assert-NotReparsePoint $outputRoot 'Output directory'
    if (-not $outputItem.PSIsContainer) {
        throw "Output path must be a directory: $outputRoot"
    }
}

$bundleName = 'canvas-windows-x64'
$archiveName = "$bundleName-$Version.zip"
$checksumName = "$archiveName.sha256"
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null
$archivePath = Join-Path $outputRoot $archiveName
$checksumPath = Join-Path $outputRoot $checksumName
$stagingRoot = Join-Path $outputRoot ('.canvas-package-{0}' -f [guid]::NewGuid().ToString('N'))
$bundleRoot = Join-Path $stagingRoot $bundleName
$bundleWebRoot = Join-Path $bundleRoot 'web'
$succeeded = $false

Remove-Item -LiteralPath $archivePath -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $checksumPath -Force -ErrorAction SilentlyContinue

try {
    New-Item -ItemType Directory -Force -Path $bundleWebRoot | Out-Null
    Copy-Item -LiteralPath $executableSource `
        -Destination (Join-Path $bundleRoot 'canvas_windows.exe')
    Copy-Item -LiteralPath $readmeSource `
        -Destination (Join-Path $bundleRoot 'README.txt')
    Get-ChildItem -LiteralPath $webSource -Directory -Recurse -Force |
        ForEach-Object {
            $relativePath = [IO.Path]::GetRelativePath($webSource, $_.FullName)
            New-Item -ItemType Directory -Force `
                -Path (Join-Path $bundleWebRoot $relativePath) | Out-Null
        }
    Get-ChildItem -LiteralPath $webSource -File -Recurse -Force |
        ForEach-Object {
            $relativePath = [IO.Path]::GetRelativePath($webSource, $_.FullName)
            Copy-Item -LiteralPath $_.FullName `
                -Destination (Join-Path $bundleWebRoot $relativePath) -Force
    }

    # The CMake synchronization stamp is a build detail, not a runtime asset.
    Remove-Item -LiteralPath (Join-Path $bundleWebRoot '.canvas-assets.stamp') `
        -Force -ErrorAction SilentlyContinue

    $null = Assert-NonEmptyFile `
        (Join-Path $bundleRoot 'canvas_windows.exe') 'Staged Canvas executable'
    $null = Assert-NonEmptyFile `
        (Join-Path $bundleRoot 'README.txt') 'Staged package README'
    $null = Assert-NonEmptyFile `
        (Join-Path $bundleWebRoot 'richtext.html') 'Staged rich-text entry point'
    $null = Assert-NonEmptyFile `
        (Join-Path $bundleWebRoot 'video.html') 'Staged video entry point'
    $null = Assert-NotReparsePoint $bundleRoot 'Staged bundle directory'
    foreach ($stagedItem in Get-ChildItem -LiteralPath $bundleRoot -Recurse -Force) {
        $null = Assert-NotReparsePoint $stagedItem.FullName 'Staged package entry'
    }

    Compress-Archive -LiteralPath $bundleRoot `
        -DestinationPath $archivePath -CompressionLevel Optimal
    $null = Assert-NonEmptyFile $archivePath 'Portable ZIP archive'

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = [IO.Compression.ZipFile]::OpenRead($archivePath)
    try {
        $entries = @($archive.Entries)
        $entryNames = [System.Collections.Generic.HashSet[string]]::new(
            [System.StringComparer]::OrdinalIgnoreCase)
        foreach ($entry in $entries) {
            $entryName = $entry.FullName.Replace('\', '/')
            if (-not $entryName.StartsWith("$bundleName/", [StringComparison]::Ordinal) -or
                $entryName.Split('/') -contains '..') {
                throw "Portable ZIP contains an entry outside its bundle root: $entryName"
            }
            if (-not $entryNames.Add($entryName)) {
                throw "Portable ZIP contains a duplicate entry: $entryName"
            }
            if (-not [string]::IsNullOrEmpty($entry.Name) -and $entry.Length -le 0) {
                throw "Portable ZIP contains an empty runtime file: $entryName"
            }
        }
        foreach ($requiredEntry in @(
                "$bundleName/canvas_windows.exe",
                "$bundleName/README.txt",
                "$bundleName/web/richtext.html",
                "$bundleName/web/video.html")) {
            if (-not $entryNames.Contains($requiredEntry)) {
                throw "Portable ZIP is missing required entry: $requiredEntry"
            }
        }
        if (-not ($entries | Where-Object {
                    $_.FullName.Replace('\', '/') -clike "$bundleName/web/*.js" -and
                    $_.Length -gt 0
                })) {
            throw 'Portable ZIP does not contain a JavaScript web asset.'
        }
    } finally {
        $archive.Dispose()
    }

    $hash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
    $checksumLine = "$hash  $archiveName`n"
    [IO.File]::WriteAllText(
        $checksumPath,
        $checksumLine,
        [Text.UTF8Encoding]::new($false))
    $null = Assert-NonEmptyFile $checksumPath 'SHA-256 checksum'
    $succeeded = $true

    Write-Host "Created portable archive: $archivePath"
    Write-Host "Created SHA-256 checksum: $checksumPath"
} finally {
    Remove-Item -LiteralPath $stagingRoot -Recurse -Force -ErrorAction SilentlyContinue
    if (-not $succeeded) {
        Remove-Item -LiteralPath $archivePath -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $checksumPath -Force -ErrorAction SilentlyContinue
    }
}
