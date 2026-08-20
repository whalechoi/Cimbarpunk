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

Export-ModuleMember -Function Enable-CimbarpunkPkgconfRetry, Assert-CimbarpunkVcpkgCheckout, Remove-CimbarpunkStagingDirectory, Assert-CimbarpunkQtSbomCorpus
