[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$ModulePath,
    [Parameter(Mandatory = $true)][string]$ToolPath,
    [Parameter(Mandatory = $true)][string]$VcpkgRoot,
    [Parameter(Mandatory = $true)][string]$WrongRepository
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$savedPkgConfig = [Environment]::GetEnvironmentVariable('PKG_CONFIG', 'Process')
$savedKeepVars = [Environment]::GetEnvironmentVariable('VCPKG_KEEP_ENV_VARS', 'Process')
try {
    Import-Module -Name $ModulePath -Force
    Enable-CimbarpunkPkgconfRetry -Pkgconf $ToolPath

    if ($env:PKG_CONFIG -ne $ToolPath) {
        throw "PKG_CONFIG was not preserved as a raw executable path: $env:PKG_CONFIG"
    }
    if ($env:PKG_CONFIG.StartsWith('"') -or $env:PKG_CONFIG.EndsWith('"')) {
        throw 'PKG_CONFIG contains literal command-line quotes.'
    }
    $version = @(& $env:PKG_CONFIG --version)
    if ($LASTEXITCODE -ne 0 -or $version.Count -eq 0) {
        throw 'The raw PKG_CONFIG path was not directly executable.'
    }

    Assert-CimbarpunkVcpkgCheckout -VcpkgRoot $VcpkgRoot
    $wrongCommitRejected = $false
    try {
        Assert-CimbarpunkVcpkgCheckout -VcpkgRoot $WrongRepository
    }
    catch {
        $wrongCommitRejected = $true
    }
    if (-not $wrongCommitRejected) {
        throw 'A vcpkg checkout at the wrong commit was accepted.'
    }

    $temporaryParent = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\', '/')
    $temporaryRoot = Join-Path $temporaryParent ("cimbarpunk-build-helper-test-" + [guid]::NewGuid().ToString('N'))
    $repositoryRoot = Join-Path $temporaryRoot 'repository'
    $installParent = Join-Path $repositoryRoot 'out\install'
    $stagingDirectory = Join-Path $installParent 'windows-release'
    $outsideDirectory = Join-Path $temporaryRoot 'outside'
    New-Item -ItemType Directory -Path $installParent,$outsideDirectory -Force | Out-Null

    $goodSbomRoot = Join-Path $temporaryRoot 'good-sbom'
    New-Item -ItemType Directory -Path $goodSbomRoot -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $goodSbomRoot 'qtbase-6.8.4.spdx') -Value @'
SPDXVersion: SPDX-2.3
DocumentNamespace: https://qt.io/spdxdocs/qtbase-6.8.4

PackageName: qtbase
PackageVersion: 6.8.4
PackageDownloadLocation: git://code.qt.io/qt/qtbase.git

'@
    Assert-CimbarpunkQtSbomCorpus -SbomRoot $goodSbomRoot

    $badSbomRoot = Join-Path $temporaryRoot 'bad-sbom'
    New-Item -ItemType Directory -Path $badSbomRoot -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $badSbomRoot 'qtbase-6.8.4.spdx') -Value @'
SPDXVersion: SPDX-2.3
DocumentNamespace: https://qt.io/spdxdocs/qtbase-deadbee+feat/desktop-region-decoder+dirty

PackageName: qtbase
PackageVersion: deadbee+feat/desktop-region-decoder+dirty
PackageDownloadLocation: git://example.invalid/outer.git@deadbeef

'@
    $contaminatedSbomRejected = $false
    try {
        Assert-CimbarpunkQtSbomCorpus -SbomRoot $badSbomRoot
    }
    catch {
        $contaminatedSbomRejected = $true
    }
    if (-not $contaminatedSbomRejected) {
        throw 'A Qt SPDX document contaminated by an outer git repository was accepted.'
    }

    $sentinel = Join-Path $outsideDirectory 'sentinel.txt'
    Set-Content -LiteralPath $sentinel -Value 'must survive' -NoNewline
    New-Item -ItemType Junction -Path $stagingDirectory -Target $outsideDirectory | Out-Null

    $junctionRejected = $false
    try {
        Remove-CimbarpunkStagingDirectory -RepositoryRoot $repositoryRoot -StagingDirectory $stagingDirectory
    }
    catch {
        $junctionRejected = $true
    }
    if (-not $junctionRejected -or -not (Test-Path -LiteralPath $sentinel -PathType Leaf)) {
        throw 'A staging junction was traversed or was not rejected.'
    }

    Remove-Item -LiteralPath $stagingDirectory -Force
    New-Item -ItemType Directory -Path $stagingDirectory -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $stagingDirectory 'old.txt') -Value 'replace me' -NoNewline
    Remove-CimbarpunkStagingDirectory -RepositoryRoot $repositoryRoot -StagingDirectory $stagingDirectory
    if (Test-Path -LiteralPath $stagingDirectory) {
        throw 'An ordinary in-repository staging directory was not removed.'
    }
}
finally {
    [Environment]::SetEnvironmentVariable('PKG_CONFIG', $savedPkgConfig, 'Process')
    [Environment]::SetEnvironmentVariable('VCPKG_KEEP_ENV_VARS', $savedKeepVars, 'Process')
    if ($null -ne (Get-Variable temporaryRoot -ErrorAction SilentlyContinue)) {
        $resolvedTemporaryRoot = [IO.Path]::GetFullPath($temporaryRoot)
        $temporaryLeaf = Split-Path -Leaf $resolvedTemporaryRoot
        $isUnderTemporaryParent = $resolvedTemporaryRoot.StartsWith(
            $temporaryParent + [IO.Path]::DirectorySeparatorChar,
            [StringComparison]::OrdinalIgnoreCase
        )
        $hasExpectedLeaf = $temporaryLeaf.StartsWith('cimbarpunk-build-helper-test-')
        if ($isUnderTemporaryParent -and $hasExpectedLeaf) {
            Remove-Item -LiteralPath $resolvedTemporaryRoot -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
}
