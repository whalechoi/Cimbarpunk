[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Archive,
    [Parameter(Mandatory = $true)][string]$SourceDirectory,
    [Parameter(Mandatory = $true)][string]$BuildDirectory,
    [Parameter(Mandatory = $true)][string]$InstallPrefix
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'WindowsBuildHelpers.psm1') -Force
$expectedSha256 = 'F56EA93356ECE3BCA727815233B86D9E1242D28D418074389FADCE683227C87C'

function Test-SameOrAncestor([string]$CandidateParent, [string]$CandidateChild) {
    $parent = [IO.Path]::GetFullPath($CandidateParent).TrimEnd('\', '/')
    $child = [IO.Path]::GetFullPath($CandidateChild).TrimEnd('\', '/')
    return $child.Equals($parent, [StringComparison]::OrdinalIgnoreCase) -or
        $child.StartsWith($parent + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)
}

foreach ($entry in @{
    Archive = $Archive
    SourceDirectory = $SourceDirectory
    BuildDirectory = $BuildDirectory
    InstallPrefix = $InstallPrefix
}.GetEnumerator()) {
    if (-not [IO.Path]::IsPathFullyQualified($entry.Value)) {
        throw "$($entry.Key) must be an absolute path: $($entry.Value)"
    }
}
if (-not (Test-Path -LiteralPath $Archive -PathType Leaf)) {
    throw "Archive not found: $Archive"
}
if ((Get-FileHash -LiteralPath $Archive -Algorithm SHA256).Hash -ne $expectedSha256) {
    throw 'Qt source archive SHA-256 mismatch.'
}

$Archive = [IO.Path]::GetFullPath($Archive)
$SourceDirectory = [IO.Path]::GetFullPath($SourceDirectory)
$BuildDirectory = [IO.Path]::GetFullPath($BuildDirectory)
$InstallPrefix = [IO.Path]::GetFullPath($InstallPrefix)
foreach ($pair in @(
    @('SourceDirectory', $SourceDirectory, 'BuildDirectory', $BuildDirectory),
    @('SourceDirectory', $SourceDirectory, 'InstallPrefix', $InstallPrefix),
    @('BuildDirectory', $BuildDirectory, 'InstallPrefix', $InstallPrefix)
)) {
    if ((Test-SameOrAncestor $pair[1] $pair[3]) -or (Test-SameOrAncestor $pair[3] $pair[1])) {
        throw "$($pair[0]) and $($pair[2]) must be distinct, non-overlapping paths."
    }
}

$git = (Get-Command git.exe -ErrorAction Stop).Source
$sourceParent = Split-Path -Parent $SourceDirectory
New-Item -ItemType Directory -Path $sourceParent -Force | Out-Null
$savedGitCeiling = [Environment]::GetEnvironmentVariable('GIT_CEILING_DIRECTORIES', 'Process')
[Environment]::SetEnvironmentVariable('GIT_CEILING_DIRECTORIES', $null, 'Process')
try {
    & $git -C $sourceParent rev-parse --is-inside-work-tree *> $null
    if ($LASTEXITCODE -eq 0) {
        throw "SourceDirectory must be outside every enclosing git worktree: $SourceDirectory"
    }
}
finally {
    [Environment]::SetEnvironmentVariable('GIT_CEILING_DIRECTORIES', $savedGitCeiling, 'Process')
}

$sourceMarkerName = '.cimbarpunk-extraction-complete'
$expectedMarker = "cimbarpunk-qt-source-v1`nsha256=$expectedSha256`n"
$sourceMarker = Join-Path $SourceDirectory $sourceMarkerName
if (Test-Path -LiteralPath $SourceDirectory) {
    $sourceItem = Get-Item -LiteralPath $SourceDirectory -Force
    if (-not $sourceItem.PSIsContainer -or ($sourceItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "SourceDirectory must be an ordinary directory: $SourceDirectory"
    }
    $hasSourceMarker = Test-Path -LiteralPath $sourceMarker -PathType Leaf
    $hasExpectedMarker = $hasSourceMarker -and [IO.File]::ReadAllText($sourceMarker) -eq $expectedMarker
    if (-not $hasExpectedMarker) {
        throw "Existing Qt source lacks the trusted archive marker: $SourceDirectory"
    }
}
else {
    $temporaryExtraction = Join-Path $sourceParent ('.cimbarpunk-qt-extract-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $temporaryExtraction | Out-Null
    try {
        $tar = (Get-Command tar.exe -ErrorAction Stop).Source
        & $tar -xf $Archive -C $temporaryExtraction
        if ($LASTEXITCODE -ne 0) { throw 'Qt source archive extraction failed.' }
        $extractedSource = Join-Path $temporaryExtraction 'qt-everywhere-src-6.8.4'
        foreach ($requiredSource in @(
            (Join-Path $extractedSource 'configure.bat'),
            (Join-Path $extractedSource 'qtbase\LICENSES\LGPL-3.0-only.txt'),
            (Join-Path $extractedSource 'qtdeclarative\LICENSES\LGPL-3.0-only.txt'),
            (Join-Path $extractedSource 'qtmultimedia\LICENSES\LGPL-3.0-only.txt'),
            (Join-Path $extractedSource 'qtsvg\LICENSES\LGPL-3.0-only.txt')
        )) {
            if (-not (Test-Path -LiteralPath $requiredSource -PathType Leaf)) {
                throw "Incomplete Qt source archive extraction: $requiredSource"
            }
        }
        [IO.File]::WriteAllText((Join-Path $extractedSource $sourceMarkerName), $expectedMarker)
        [IO.Directory]::Move($extractedSource, $SourceDirectory)
    }
    finally {
        if (Test-Path -LiteralPath $temporaryExtraction) {
            $resolvedTemporary = [IO.Path]::GetFullPath($temporaryExtraction)
            $temporaryLeaf = Split-Path -Leaf $resolvedTemporary
            $isUnderSourceParent = $resolvedTemporary.StartsWith(
                [IO.Path]::GetFullPath($sourceParent).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar,
                [StringComparison]::OrdinalIgnoreCase
            )
            $hasExpectedTemporaryName = $temporaryLeaf.StartsWith('.cimbarpunk-qt-extract-')
            $isExpectedTemporary = $isUnderSourceParent -and $hasExpectedTemporaryName
            if (-not $isExpectedTemporary) {
                throw "Refusing to clean unexpected extraction directory: $resolvedTemporary"
            }
            Remove-Item -LiteralPath $resolvedTemporary -Recurse -Force
        }
    }
}

if (-not (Test-Path -LiteralPath (Join-Path $SourceDirectory 'configure.bat') -PathType Leaf)) {
    throw "Verified Qt source is incomplete: $SourceDirectory"
}
[Environment]::SetEnvironmentVariable('GIT_CEILING_DIRECTORIES', $sourceParent, 'Process')
try {
    & $git -C $SourceDirectory rev-parse --is-inside-work-tree *> $null
    if ($LASTEXITCODE -eq 0) {
        throw "Qt source can still discover an enclosing git repository: $SourceDirectory"
    }
}
finally {
    [Environment]::SetEnvironmentVariable('GIT_CEILING_DIRECTORIES', $savedGitCeiling, 'Process')
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installation = (& $vswhere -latest -products * -version '[17.0,18.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath).Trim()
if ([string]::IsNullOrWhiteSpace($installation)) { throw 'Visual Studio 2022 C++ tools not found.' }
$devCommand = Join-Path $installation 'Common7\Tools\VsDevCmd.bat'
& $env:ComSpec /d /s /c "`"$devCommand`" -arch=x64 -host_arch=x64 >nul && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') }
}

$cmake = (Get-Command cmake.exe -ErrorAction Stop).Source
$ninja = (Get-Command ninja.exe -ErrorAction Stop).Source
Write-Host "Using CMake: $cmake"
Write-Host "Using Ninja: $ninja"
New-Item -ItemType Directory -Path $BuildDirectory,$InstallPrefix -Force | Out-Null

[Environment]::SetEnvironmentVariable('GIT_CEILING_DIRECTORIES', $sourceParent, 'Process')
$locationPushed = $false
$savedBuildPath = $env:PATH
try {
    Push-Location $BuildDirectory
    $locationPushed = $true
    $configureArguments = @(
        '-prefix', $InstallPrefix,
        '-debug-and-release',
        '-opensource', '-confirm-license',
        '-nomake', 'examples', '-nomake', 'tests',
        '-cmake-generator', 'Ninja Multi-Config',
        '-submodules', 'qtbase,qtdeclarative,qtmultimedia,qtsvg,qttools',
        '--',
        '-DBUILD_qtbase=ON',
        '-DBUILD_qtdeclarative=ON',
        '-DBUILD_qtmultimedia=ON',
        '-DBUILD_qtsvg=ON',
        '-DBUILD_qttools=ON'
    )
    & (Join-Path $SourceDirectory 'configure.bat') @configureArguments
    if ($LASTEXITCODE -ne 0) { throw 'Qt configure failed.' }

    # Multi-config Debug host tools live in qtbase/bin/Debug but depend on
    # Qt6Cored.dll in qtbase/bin.  CMake invokes the tools directly, so make
    # the parent runtime directory explicit instead of relying on a developer
    # Qt installation in PATH.
    $qtBuildRuntime = Join-Path $BuildDirectory 'qtbase\bin'
    $env:PATH = $qtBuildRuntime + [IO.Path]::PathSeparator + $savedBuildPath
    foreach ($configuration in @('Release', 'Debug')) {
        & $cmake --build . --config $configuration --parallel
        if ($LASTEXITCODE -ne 0) { throw "Qt $configuration build failed." }
        & $cmake --install . --config $configuration
        if ($LASTEXITCODE -ne 0) { throw "Qt $configuration install failed." }
    }
}
finally {
    if ($locationPushed) { Pop-Location }
    $env:PATH = $savedBuildPath
    [Environment]::SetEnvironmentVariable('GIT_CEILING_DIRECTORIES', $savedGitCeiling, 'Process')
}

foreach ($required in @(
    (Join-Path $InstallPrefix 'lib\cmake\Qt6Core\Qt6CoreTargets-release.cmake'),
    (Join-Path $InstallPrefix 'lib\cmake\Qt6Core\Qt6CoreTargets-debug.cmake'),
    (Join-Path $InstallPrefix 'plugins\iconengines\qsvgicon.dll'),
    (Join-Path $InstallPrefix 'plugins\iconengines\qsvgicond.dll')
)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Qt SDK verification failed: $required" }
}


# Preserve the complete license corpus for every module actually installed,
# as identified by Qt's generated SPDX documents.
$qtLicenseRoot = Join-Path $InstallPrefix 'share\licenses\qt'
$distributionLicenses = Join-Path $SourceDirectory 'LICENSES'
if (-not (Test-Path -LiteralPath $distributionLicenses -PathType Container)) {
    throw "Qt distribution licenses are missing: $distributionLicenses"
}
$distributionDestination = Join-Path $qtLicenseRoot 'qt-distribution'
New-Item -ItemType Directory -Path $distributionDestination -Force | Out-Null
Get-ChildItem -LiteralPath $distributionLicenses -Force |
    Copy-Item -Destination $distributionDestination -Recurse -Force

$sbomFiles = @(Get-ChildItem -LiteralPath (Join-Path $InstallPrefix 'sbom') -Filter '*-6.8.4.spdx' -File)
if ($sbomFiles.Count -eq 0) { throw 'Qt installation did not produce any module SPDX documents.' }
foreach ($sbom in $sbomFiles) {
    $module = $sbom.BaseName -replace '-6\.8\.4$', ''
    $sourceLicenses = Join-Path $SourceDirectory "$module\LICENSES"
    if (-not (Test-Path -LiteralPath $sourceLicenses -PathType Container)) {
        throw "License directory for installed Qt module '$module' is missing: $sourceLicenses"
    }
    $moduleDestination = Join-Path $qtLicenseRoot $module
    New-Item -ItemType Directory -Path $moduleDestination -Force | Out-Null
    Get-ChildItem -LiteralPath $sourceLicenses -Force |
        Copy-Item -Destination $moduleDestination -Recurse -Force
}
Assert-CimbarpunkQtSbomCorpus -SbomRoot (Join-Path $InstallPrefix 'sbom')
