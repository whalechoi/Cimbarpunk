Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$script:ExpectedVcpkgCommit = '9e593bb18ea69cc5095e012465dcd675a822ed0d'

function Assert-CimbarpunkVcpkgCheckout {
    [CmdletBinding()]
    param([Parameter(Mandatory = $true)][string]$VcpkgRoot)

    $git = (Get-Command git.exe -ErrorAction Stop).Source
    $lines = @(& $git -C $VcpkgRoot rev-parse --verify HEAD 2>&1)
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0 -or $lines.Count -ne 1) {
        throw "VcpkgRoot must be a git checkout at the fixed baseline: $VcpkgRoot"
    }
    $actual = ([string]$lines[0]).Trim()
    if ($actual -ne $script:ExpectedVcpkgCommit) {
        throw "vcpkg commit mismatch: expected $script:ExpectedVcpkgCommit, found $actual"
    }
    Write-Host "Verified vcpkg commit: $actual"
}

function Remove-CimbarpunkStagingDirectory {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$RepositoryRoot,
        [Parameter(Mandatory = $true)][string]$StagingDirectory
    )

    $repository = [IO.Path]::GetFullPath($RepositoryRoot).TrimEnd('\', '/')
    $outDirectory = Join-Path $repository 'out'
    $installParent = Join-Path $outDirectory 'install'
    $expectedStaging = [IO.Path]::GetFullPath((Join-Path $installParent 'windows-release'))
    $requestedStaging = [IO.Path]::GetFullPath($StagingDirectory)
    if (-not $requestedStaging.Equals($expectedStaging, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to replace unexpected install directory: $requestedStaging"
    }

    foreach ($path in @($repository, $outDirectory, $installParent, $requestedStaging)) {
        if (-not (Test-Path -LiteralPath $path)) { continue }
        $item = Get-Item -LiteralPath $path -Force
        if (-not $item.PSIsContainer) {
            throw "Staging path component is not a directory: $path"
        }
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Refusing to traverse staging reparse point: $path"
        }
    }

    if (Test-Path -LiteralPath $requestedStaging) {
        Remove-Item -LiteralPath $requestedStaging -Recurse -Force
    }
}

function Assert-CimbarpunkQtSbomCorpus {
    [CmdletBinding()]
    param([Parameter(Mandatory = $true)][string]$SbomRoot)

    if (-not (Test-Path -LiteralPath $SbomRoot -PathType Container)) {
        throw "Qt SPDX directory is missing: $SbomRoot"
    }
    $allDocuments = @(Get-ChildItem -LiteralPath $SbomRoot -Filter '*.spdx' -File)
    $documents = @($allDocuments | Where-Object Name -Like '*-6.8.4.spdx')
    if ($documents.Count -eq 0) {
        throw "Qt SPDX directory contains no 6.8.4 module documents: $SbomRoot"
    }
    if ($documents.Count -ne $allDocuments.Count) {
        throw "Qt SPDX directory contains documents outside the exact 6.8.4 module corpus: $SbomRoot"
    }

    foreach ($document in $documents) {
        $module = $document.BaseName -replace '-6\.8\.4$', ''
        $lines = @(Get-Content -LiteralPath $document.FullName)
        $provenance = @($lines | Where-Object {
            $_ -match '^(DocumentNamespace|PackageVersion|PackageDownloadLocation):'
        }) -join "`n"
        if ($provenance -match '(?i)(dirty|desktop-region-decoder|cimbarpunk)') {
            throw "Qt SPDX provenance contains an outer worktree marker: $($document.FullName)"
        }

        $expectedNamespace = "DocumentNamespace: https://qt.io/spdxdocs/$module-6.8.4"
        if ($lines -notcontains $expectedNamespace) {
            throw "Qt SPDX document namespace is not the official non-git 6.8.4 namespace: $($document.FullName)"
        }

        $packageName = "PackageName: $module"
        $packageIndex = [Array]::IndexOf($lines, $packageName)
        if ($packageIndex -lt 0) {
            throw "Qt SPDX primary package block is missing: $($document.FullName)"
        }
        $primaryVersion = $null
        $primaryDownload = $null
        for ($index = $packageIndex + 1; $index -lt $lines.Count; ++$index) {
            if ([string]::IsNullOrWhiteSpace($lines[$index])) { break }
            if ($lines[$index] -like 'PackageVersion:*') { $primaryVersion = $lines[$index] }
            if ($lines[$index] -like 'PackageDownloadLocation:*') { $primaryDownload = $lines[$index] }
        }
        $versionIsInvalid = $primaryVersion -ne 'PackageVersion: 6.8.4'
        $expectedDownload = "PackageDownloadLocation: git://code.qt.io/qt/$module.git"
        $downloadIsInvalid = $primaryDownload -ne $expectedDownload
        if ($versionIsInvalid -or $downloadIsInvalid) {
            throw "Qt SPDX primary package provenance is not official source release 6.8.4: $($document.FullName)"
        }
    }
    Write-Host "Verified Qt 6.8.4 SPDX provenance for $($documents.Count) module documents."
}

function Assert-CimbarpunkFfmpegInstallation {
    [CmdletBinding()]
    param([Parameter(Mandatory = $true)][string]$InstallRoot)

    if (-not (Test-Path -LiteralPath $InstallRoot -PathType Container)) {
        throw "FFmpeg install root is missing: $InstallRoot"
    }
    $resolvedRoot = (Resolve-Path -LiteralPath $InstallRoot).Path
    $statusPath = Join-Path $resolvedRoot 'vcpkg\status'
    if (-not (Test-Path -LiteralPath $statusPath -PathType Leaf)) {
        throw "FFmpeg vcpkg status file is missing: $statusPath"
    }

    $paragraphs = @((Get-Content -LiteralPath $statusPath -Raw) -split '(?:\r?\n){2,}')
    $coreMatches = @($paragraphs | Where-Object {
        $_ -match '(?m)^Package: ffmpeg\r?$' -and
        $_ -notmatch '(?m)^Feature:' -and
        $_ -match '(?m)^Version: 7\.1\.1\r?$' -and
        $_ -match '(?m)^Port-Version: 6\r?$' -and
        $_ -match '(?m)^Architecture: x64-windows\r?$' -and
        $_ -match '(?m)^Status: install ok installed\r?$'
    })
    if ($coreMatches.Count -ne 1) {
        throw "FFmpeg 7.1.1#6 x64-windows core status is missing or ambiguous: $statusPath"
    }
    $requiredFeatures = @('avcodec', 'avformat', 'swresample', 'swscale')
    foreach ($feature in $requiredFeatures) {
        $featureMatches = @($paragraphs | Where-Object {
            $_ -match '(?m)^Package: ffmpeg\r?$' -and
            $_ -match "(?m)^Feature: $([regex]::Escape($feature))\r?$" -and
            $_ -match '(?m)^Architecture: x64-windows\r?$' -and
            $_ -match '(?m)^Status: install ok installed\r?$'
        })
        if ($featureMatches.Count -ne 1) {
            throw "FFmpeg 7.1.1#6 x64-windows status is missing or ambiguous for feature '$feature': $statusPath"
        }
    }
    $installedFeatures = @($paragraphs | Where-Object {
        $_ -match '(?m)^Package: ffmpeg\r?$' -and
        $_ -match '(?m)^Feature: (?<feature>[^\r\n]+)\r?$' -and
        $_ -match '(?m)^Architecture: x64-windows\r?$' -and
        $_ -match '(?m)^Status: install ok installed\r?$'
    } | ForEach-Object {
        if ($_ -match '(?m)^Feature: (?<feature>[^\r\n]+)\r?$') {
            $matches.feature
        }
    })
    $featureDifference = @(Compare-Object -ReferenceObject $requiredFeatures -DifferenceObject $installedFeatures)
    if ($installedFeatures.Count -ne $requiredFeatures.Count -or $featureDifference.Count -ne 0) {
        throw "FFmpeg installation has unexpected or incomplete features: $($installedFeatures -join ', ')"
    }

    $tripletRoot = Join-Path $resolvedRoot 'x64-windows'
    $requiredFiles = @(
        'include\libavcodec\avcodec.h',
        'share\ffmpeg\copyright'
    )
    foreach ($library in @('avcodec', 'avformat', 'avutil', 'swresample', 'swscale')) {
        $requiredFiles += "lib\$library.lib"
        $requiredFiles += "debug\lib\$library.lib"
    }
    foreach ($dll in @('avcodec-61.dll', 'avformat-61.dll', 'avutil-59.dll', 'swresample-5.dll', 'swscale-8.dll')) {
        $requiredFiles += "bin\$dll"
        $requiredFiles += "debug\bin\$dll"
    }
    foreach ($relativePath in $requiredFiles) {
        $required = Join-Path $tripletRoot $relativePath
        if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
            throw "Required FFmpeg 7.1.1 artifact is missing: $required"
        }
    }

    Write-Host "Verified FFmpeg 7.1.1#6 x64-windows installation: $tripletRoot"
}

function Assert-CimbarpunkQtFfmpegBackend {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$QtRoot,
        [Parameter(Mandatory = $true)][string]$ConfigSummary
    )

    if (-not (Test-Path -LiteralPath $QtRoot -PathType Container)) {
        throw "Qt install root is missing: $QtRoot"
    }
    if (-not (Test-Path -LiteralPath $ConfigSummary -PathType Leaf)) {
        throw "Qt configure summary is missing: $ConfigSummary"
    }
    $summaryText = Get-Content -LiteralPath $ConfigSummary -Raw
    if ($summaryText -notmatch '(?m)^\s*FFmpeg\s+\.+\s+yes\s*$') {
        throw "Qt configure summary does not enable the FFmpeg multimedia backend: $ConfigSummary"
    }

    foreach ($relativePath in @(
        'plugins\multimedia\ffmpegmediaplugin.dll',
        'plugins\multimedia\ffmpegmediaplugind.dll',
        'bin\avcodec-61.dll',
        'bin\avformat-61.dll',
        'bin\avutil-59.dll',
        'bin\swresample-5.dll',
        'bin\swscale-8.dll'
    )) {
        $required = Join-Path $QtRoot $relativePath
        if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
            throw "Qt FFmpeg backend artifact is missing: $required"
        }
    }
    Write-Host "Verified Qt FFmpeg multimedia backend: $QtRoot"
}

function Assert-CimbarpunkStagedFfmpegBackend {
    [CmdletBinding()]
    param([Parameter(Mandatory = $true)][string]$StagingRoot)

    if (-not (Test-Path -LiteralPath $StagingRoot -PathType Container)) {
        throw "Staging root is missing: $StagingRoot"
    }
    $resolvedRoot = (Resolve-Path -LiteralPath $StagingRoot).Path
    foreach ($relativePath in @(
        'plugins\multimedia\ffmpegmediaplugin.dll',
        'avcodec-61.dll',
        'avformat-61.dll',
        'avutil-59.dll',
        'swresample-5.dll',
        'swscale-8.dll'
    )) {
        $required = Join-Path $resolvedRoot $relativePath
        if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
            throw "Staged FFmpeg screen-capture backend artifact is missing: $required"
        }
    }
    Write-Host "Verified staged FFmpeg screen-capture backend: $resolvedRoot"
}

function Enable-CimbarpunkPkgconfRetry {
    [CmdletBinding()]
    param([Parameter(Mandatory = $true)][string]$Pkgconf)

    $version = @(& $Pkgconf --version 2>&1)
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0 -or $version.Count -eq 0) {
        throw "Discovered pkg-config tool is not runnable: $Pkgconf"
    }

    # Environment variables are executable paths, not command-line fragments.
    # In particular, literal quotes make CreateProcess look for a quoted name.
    $env:PKG_CONFIG = $Pkgconf
    $env:VCPKG_KEEP_ENV_VARS = 'PKG_CONFIG'
    Write-Host "Using pkg-config retry tool: $Pkgconf ($($version[0]))"
}

Export-ModuleMember -Function Enable-CimbarpunkPkgconfRetry, Assert-CimbarpunkVcpkgCheckout, Remove-CimbarpunkStagingDirectory, Assert-CimbarpunkQtSbomCorpus, Assert-CimbarpunkFfmpegInstallation, Assert-CimbarpunkQtFfmpegBackend, Assert-CimbarpunkStagedFfmpegBackend
