Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Assert-True {
    param(
        [Parameter(Mandatory = $true)]
        [bool]$Condition,

        [Parameter(Mandatory = $true)]
        [string]$Message
    )

    if (-not $Condition) {
        throw $Message
    }
}

function New-FakeWebRuntime {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $assetRoot = Join-Path $Path 'assets'
    New-Item -ItemType Directory -Force -Path $assetRoot | Out-Null
    [IO.File]::WriteAllText((Join-Path $Path 'richtext.html'), '<main>rich text</main>')
    [IO.File]::WriteAllText((Join-Path $Path 'video.html'), '<video></video>')
    [IO.File]::WriteAllText((Join-Path $assetRoot 'canvas.js'), 'export {};')
    [IO.File]::WriteAllText((Join-Path $Path '.canvas-assets.stamp'), '')
}

function New-FakeBuild {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    New-FakeWebRuntime (Join-Path $Path 'web')
    [IO.File]::WriteAllBytes(
        (Join-Path $Path 'canvas_windows.exe'),
        [byte[]](0x4d, 0x5a, 0x01))
}

function Assert-PackageFails {
    param(
        [Parameter(Mandatory = $true)]
        [scriptblock]$Action,

        [Parameter(Mandatory = $true)]
        [string]$Message
    )

    $failure = $null
    try {
        & $Action
    } catch {
        $failure = $_
    }
    if ($null -eq $failure) {
        throw $Message
    }
}

function Assert-NoPackageResidue {
    param(
        [Parameter(Mandatory = $true)]
        [string]$OutputPath,

        [Parameter(Mandatory = $true)]
        [string]$Scenario
    )

    if (-not (Test-Path -LiteralPath $OutputPath)) {
        return
    }

    $partialPackages = @(Get-ChildItem -LiteralPath $OutputPath -File -Force |
        Where-Object { $_.Name -clike 'canvas-windows-x64-*.zip*' })
    Assert-True ($partialPackages.Count -eq 0) `
        "$Scenario left a ZIP or checksum behind."

    $stagingDirectories = @(Get-ChildItem -LiteralPath $OutputPath -Force |
        Where-Object { $_.Name -clike '.canvas-package-*' })
    Assert-True ($stagingDirectories.Count -eq 0) `
        "$Scenario left a staging path behind."
}

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$packager = Join-Path $repositoryRoot 'scripts/New-WindowsPortablePackage.ps1'
$temporaryRoot = Join-Path ([IO.Path]::GetTempPath()) `
    ('canvas-package-contract-{0}' -f [guid]::NewGuid().ToString('N'))
$buildRoot = Join-Path $temporaryRoot 'build'
# This deliberately shares the "build" textual prefix without being a child.
$outputRoot = Join-Path $temporaryRoot 'build-output'
$expandedRoot = Join-Path $temporaryRoot 'expanded'
$version = 'v0.0.0-contract'
$archiveName = "canvas-windows-x64-$version.zip"
$junctionPath = $null

try {
    New-FakeBuild $buildRoot

    & $packager -BuildDirectory $buildRoot `
        -OutputDirectory $outputRoot -Version $version

    $archivePath = Join-Path $outputRoot $archiveName
    $checksumPath = "$archivePath.sha256"
    Assert-True (Test-Path -LiteralPath $archivePath -PathType Leaf) `
        'The packager did not create the expected ZIP.'
    Assert-True (Test-Path -LiteralPath $checksumPath -PathType Leaf) `
        'The packager did not create the expected checksum.'

    Expand-Archive -LiteralPath $archivePath -DestinationPath $expandedRoot
    $bundleRoot = Join-Path $expandedRoot 'canvas-windows-x64'
    foreach ($relativePath in @(
            'canvas_windows.exe',
            'README.txt',
            'web/richtext.html',
            'web/video.html',
            'web/assets/canvas.js')) {
        Assert-True `
            (Test-Path -LiteralPath (Join-Path $bundleRoot $relativePath) -PathType Leaf) `
            "Expanded package is missing $relativePath."
    }
    Assert-True `
        (-not (Test-Path -LiteralPath (Join-Path $bundleRoot 'web/.canvas-assets.stamp'))) `
        'The build-only CMake stamp leaked into the portable package.'

    $expectedChecksum = '{0}  {1}' -f `
        (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant(), `
        $archiveName
    $actualChecksum = (Get-Content -LiteralPath $checksumPath -Raw).Trim()
    Assert-True ($actualChecksum -ceq $expectedChecksum) `
        'The generated SHA-256 checksum does not match the ZIP.'

    $missingOutput = Join-Path $temporaryRoot 'missing-output'
    Remove-Item -LiteralPath (Join-Path $buildRoot 'web/video.html') -Force
    Assert-PackageFails {
        & $packager -BuildDirectory $buildRoot `
            -OutputDirectory $missingOutput -Version 'v0.0.0-missing'
    } 'Packaging must fail when a required web entry point is missing.'
    Assert-NoPackageResidue $missingOutput 'Missing-asset failure'

    $unsafeOutput = Join-Path $temporaryRoot 'unsafe-output'
    Assert-PackageFails {
        & $packager -BuildDirectory $buildRoot `
            -OutputDirectory $unsafeOutput -Version '../unsafe'
    } 'Packaging must reject unsafe version strings.'
    Assert-NoPackageResidue $unsafeOutput 'Unsafe-version failure'

    # An optional empty asset passes source preflight, is copied and compressed,
    # then fails the archive's fail-closed empty-runtime-file validation. This
    # exercises cleanup after staging and ZIP creation, not only early exits.
    $postArchiveBuild = Join-Path $temporaryRoot 'post-archive-build'
    New-FakeBuild $postArchiveBuild
    [IO.File]::WriteAllText(
        (Join-Path $postArchiveBuild 'web/assets/empty.css'), '')
    $postArchiveOutput = Join-Path $temporaryRoot 'post-archive-output'
    Assert-PackageFails {
        & $packager -BuildDirectory $postArchiveBuild `
            -OutputDirectory $postArchiveOutput -Version 'v0.0.0-post-archive'
    } 'Archive validation must reject an empty optional runtime asset.'
    Assert-True (Test-Path -LiteralPath $postArchiveOutput -PathType Container) `
        'Post-archive cleanup case did not reach the packaging phase.'
    Assert-NoPackageResidue $postArchiveOutput 'Post-archive validation failure'

    $descendantBuild = Join-Path $temporaryRoot 'descendant-source'
    New-FakeBuild $descendantBuild
    $descendantOutput = Join-Path $descendantBuild 'package-output'
    Assert-PackageFails {
        & $packager -BuildDirectory $descendantBuild `
            -OutputDirectory $descendantOutput -Version 'v0.0.0-descendant'
    } 'Output nested below BuildDirectory must be rejected.'
    Assert-NoPackageResidue $descendantOutput 'Descendant-overlap failure'

    $ancestorOutput = Join-Path $temporaryRoot 'ancestor-output'
    $ancestorBuild = Join-Path $ancestorOutput 'build'
    New-FakeBuild $ancestorBuild
    Assert-PackageFails {
        & $packager -BuildDirectory $ancestorBuild `
            -OutputDirectory $ancestorOutput -Version 'v0.0.0-ancestor'
    } 'Output containing BuildDirectory must be rejected.'
    Assert-NoPackageResidue $ancestorOutput 'Ancestor-overlap failure'

    $caseBuild = Join-Path $temporaryRoot 'case-source'
    New-FakeBuild $caseBuild
    Assert-PackageFails {
        & $packager -BuildDirectory $caseBuild `
            -OutputDirectory ($caseBuild.ToUpperInvariant()) -Version 'v0.0.0-case'
    } 'Case-insensitive source/output equality must be rejected on Windows.'
    Assert-NoPackageResidue $caseBuild 'Case-insensitive overlap failure'

    $readmeOutput = Join-Path $temporaryRoot 'readme-output'
    New-Item -ItemType Directory -Force -Path $readmeOutput | Out-Null
    $customReadme = Join-Path $readmeOutput 'PACKAGE-README.txt'
    [IO.File]::WriteAllText($customReadme, 'portable package')
    $readmeBuild = Join-Path $temporaryRoot 'readme-build'
    New-FakeBuild $readmeBuild
    Assert-PackageFails {
        & $packager -BuildDirectory $readmeBuild `
            -OutputDirectory $readmeOutput -Version 'v0.0.0-readme' `
            -ReadmePath $customReadme
    } 'Output containing ReadmePath must be rejected.'
    Assert-NoPackageResidue $readmeOutput 'README-overlap failure'

    $junctionBuild = Join-Path $temporaryRoot 'junction-build'
    New-Item -ItemType Directory -Force -Path $junctionBuild | Out-Null
    [IO.File]::WriteAllBytes(
        (Join-Path $junctionBuild 'canvas_windows.exe'),
        [byte[]](0x4d, 0x5a, 0x01))
    $junctionTarget = Join-Path $temporaryRoot 'junction-web-target'
    New-FakeWebRuntime $junctionTarget
    $junctionPath = Join-Path $junctionBuild 'web'
    $junctionCreated = $false
    try {
        New-Item -ItemType Junction -Path $junctionPath `
            -Target $junctionTarget -ErrorAction Stop | Out-Null
        $junctionCreated = $true
    } catch {
        Write-Warning "SKIP reparse-point contract: junction creation unavailable: $($_.Exception.Message)"
    }

    if ($junctionCreated) {
        $junctionItem = Get-Item -LiteralPath $junctionPath -Force
        Assert-True `
            (($junctionItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) `
            'The negative-test junction is not marked as a reparse point.'
        $junctionOutput = Join-Path $temporaryRoot 'junction-output'
        Assert-PackageFails {
            & $packager -BuildDirectory $junctionBuild `
                -OutputDirectory $junctionOutput -Version 'v0.0.0-junction'
        } 'Packaging must reject a web source that is a junction.'
        Assert-NoPackageResidue $junctionOutput 'Junction-source failure'
    }

    Write-Host 'Windows portable-package behavioral contract passed.'
} finally {
    if ($null -ne $junctionPath -and (Test-Path -LiteralPath $junctionPath)) {
        Remove-Item -LiteralPath $junctionPath -Force -ErrorAction SilentlyContinue
    }
    Remove-Item -LiteralPath $temporaryRoot -Recurse -Force -ErrorAction SilentlyContinue
}
