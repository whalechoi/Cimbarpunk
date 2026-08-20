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

    $goodFfmpegRoot = Join-Path $temporaryRoot 'good-ffmpeg'
    $ffmpegTripletRoot = Join-Path $goodFfmpegRoot 'x64-windows'
    foreach ($directory in @(
        (Join-Path $goodFfmpegRoot 'vcpkg'),
        (Join-Path $ffmpegTripletRoot 'include\libavcodec'),
        (Join-Path $ffmpegTripletRoot 'lib'),
        (Join-Path $ffmpegTripletRoot 'bin'),
        (Join-Path $ffmpegTripletRoot 'debug\lib'),
        (Join-Path $ffmpegTripletRoot 'debug\bin'),
        (Join-Path $ffmpegTripletRoot 'share\ffmpeg')
    )) {
        New-Item -ItemType Directory -Path $directory -Force | Out-Null
    }
    $statusParagraphs = @(
        @('Package: ffmpeg', 'Version: 7.1.1', 'Port-Version: 6', 'Architecture: x64-windows', 'Status: install ok installed'),
        @('Package: ffmpeg', 'Feature: avcodec', 'Architecture: x64-windows', 'Status: install ok installed'),
        @('Package: ffmpeg', 'Feature: avformat', 'Architecture: x64-windows', 'Status: install ok installed'),
        @('Package: ffmpeg', 'Feature: swresample', 'Architecture: x64-windows', 'Status: install ok installed'),
        @('Package: ffmpeg', 'Feature: swscale', 'Architecture: x64-windows', 'Status: install ok installed')
    )
    $statusText = ($statusParagraphs | ForEach-Object { $_ -join "`n" }) -join "`n`n"
    Set-Content -LiteralPath (Join-Path $goodFfmpegRoot 'vcpkg\status') -Value $statusText -NoNewline
    Set-Content -LiteralPath (Join-Path $ffmpegTripletRoot 'include\libavcodec\avcodec.h') -Value 'fixture' -NoNewline
    foreach ($library in @('avcodec', 'avformat', 'avutil', 'swresample', 'swscale')) {
        Set-Content -LiteralPath (Join-Path $ffmpegTripletRoot "lib\$library.lib") -Value 'release' -NoNewline
        Set-Content -LiteralPath (Join-Path $ffmpegTripletRoot "debug\lib\$library.lib") -Value 'debug' -NoNewline
    }
    foreach ($dll in @('avcodec-61.dll', 'avformat-61.dll', 'avutil-59.dll', 'swresample-5.dll', 'swscale-8.dll')) {
        Set-Content -LiteralPath (Join-Path $ffmpegTripletRoot "bin\$dll") -Value 'release' -NoNewline
        Set-Content -LiteralPath (Join-Path $ffmpegTripletRoot "debug\bin\$dll") -Value 'debug' -NoNewline
    }
    Set-Content -LiteralPath (Join-Path $ffmpegTripletRoot 'share\ffmpeg\copyright') -Value 'fixture license' -NoNewline
    Assert-CimbarpunkFfmpegInstallation -InstallRoot $goodFfmpegRoot

    $wrongFfmpegRoot = Join-Path $temporaryRoot 'wrong-ffmpeg-version'
    Copy-Item -LiteralPath $goodFfmpegRoot -Destination $wrongFfmpegRoot -Recurse
    $wrongStatus = (Get-Content -LiteralPath (Join-Path $wrongFfmpegRoot 'vcpkg\status') -Raw).Replace('7.1.1', '8.1.2')
    Set-Content -LiteralPath (Join-Path $wrongFfmpegRoot 'vcpkg\status') -Value $wrongStatus -NoNewline
    $wrongFfmpegRejected = $false
    try {
        Assert-CimbarpunkFfmpegInstallation -InstallRoot $wrongFfmpegRoot
    }
    catch {
        $wrongFfmpegRejected = $true
    }
    if (-not $wrongFfmpegRejected) {
        throw 'An FFmpeg installation at the wrong version was accepted.'
    }

    $unexpectedFeatureRoot = Join-Path $temporaryRoot 'unexpected-ffmpeg-feature'
    Copy-Item -LiteralPath $goodFfmpegRoot -Destination $unexpectedFeatureRoot -Recurse
    $unexpectedStatusPath = Join-Path $unexpectedFeatureRoot 'vcpkg\status'
    $unexpectedStatus = (Get-Content -LiteralPath $unexpectedStatusPath -Raw).TrimEnd() + "`n`n" + @'
Package: ffmpeg
Feature: gpl
Architecture: x64-windows
Status: install ok installed
'@
    Set-Content -LiteralPath $unexpectedStatusPath -Value $unexpectedStatus -NoNewline
    $unexpectedFeatureRejected = $false
    try {
        Assert-CimbarpunkFfmpegInstallation -InstallRoot $unexpectedFeatureRoot
    }
    catch {
        $unexpectedFeatureRejected = $true
    }
    if (-not $unexpectedFeatureRejected) {
        throw 'An FFmpeg installation with an unexpected feature was accepted.'
    }

    $incompleteFfmpegRoot = Join-Path $temporaryRoot 'incomplete-ffmpeg'
    Copy-Item -LiteralPath $goodFfmpegRoot -Destination $incompleteFfmpegRoot -Recurse
    Remove-Item -LiteralPath (Join-Path $incompleteFfmpegRoot 'x64-windows\bin\avcodec-61.dll') -Force
    $incompleteFfmpegRejected = $false
    try {
        Assert-CimbarpunkFfmpegInstallation -InstallRoot $incompleteFfmpegRoot
    }
    catch {
        $incompleteFfmpegRejected = $true
    }
    if (-not $incompleteFfmpegRejected) {
        throw 'An FFmpeg installation missing a required runtime DLL was accepted.'
    }

    $goodQtFfmpegRoot = Join-Path $temporaryRoot 'good-qt-ffmpeg'
    $qtMultimediaPlugins = Join-Path $goodQtFfmpegRoot 'plugins\multimedia'
    $qtBin = Join-Path $goodQtFfmpegRoot 'bin'
    New-Item -ItemType Directory -Path $qtMultimediaPlugins,$qtBin -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $qtMultimediaPlugins 'ffmpegmediaplugin.dll') -Value 'release' -NoNewline
    Set-Content -LiteralPath (Join-Path $qtMultimediaPlugins 'ffmpegmediaplugind.dll') -Value 'debug' -NoNewline
    foreach ($dll in @('avcodec-61.dll', 'avformat-61.dll', 'avutil-59.dll', 'swresample-5.dll', 'swscale-8.dll')) {
        Set-Content -LiteralPath (Join-Path $qtBin $dll) -Value 'runtime' -NoNewline
    }
    $goodSummary = Join-Path $temporaryRoot 'good-config.summary'
    Set-Content -LiteralPath $goodSummary -Value @'
Qt Multimedia:
  Plugin:
    FFmpeg ............................... yes
    Windows Media Foundation ............. yes
'@
    Assert-CimbarpunkQtFfmpegBackend -QtRoot $goodQtFfmpegRoot -ConfigSummary $goodSummary

    $badSummary = Join-Path $temporaryRoot 'bad-config.summary'
    Set-Content -LiteralPath $badSummary -Value @'
Qt Multimedia:
  Plugin:
    FFmpeg ............................... no
    Windows Media Foundation ............. yes
'@
    $disabledQtFfmpegRejected = $false
    try {
        Assert-CimbarpunkQtFfmpegBackend -QtRoot $goodQtFfmpegRoot -ConfigSummary $badSummary
    }
    catch {
        $disabledQtFfmpegRejected = $true
    }
    if (-not $disabledQtFfmpegRejected) {
        throw 'A Qt build with FFmpeg disabled was accepted.'
    }

    $incompleteQtFfmpegRoot = Join-Path $temporaryRoot 'incomplete-qt-ffmpeg'
    Copy-Item -LiteralPath $goodQtFfmpegRoot -Destination $incompleteQtFfmpegRoot -Recurse
    Remove-Item -LiteralPath (Join-Path $incompleteQtFfmpegRoot 'plugins\multimedia\ffmpegmediaplugind.dll') -Force
    $incompleteQtFfmpegRejected = $false
    try {
        Assert-CimbarpunkQtFfmpegBackend -QtRoot $incompleteQtFfmpegRoot -ConfigSummary $goodSummary
    }
    catch {
        $incompleteQtFfmpegRejected = $true
    }
    if (-not $incompleteQtFfmpegRejected) {
        throw 'A Qt SDK missing the Debug FFmpeg plugin was accepted.'
    }

    $goodStagedFfmpegRoot = Join-Path $temporaryRoot 'good-staged-ffmpeg'
    $stagedMultimediaPlugins = Join-Path $goodStagedFfmpegRoot 'plugins\multimedia'
    New-Item -ItemType Directory -Path $stagedMultimediaPlugins -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $stagedMultimediaPlugins 'ffmpegmediaplugin.dll') -Value 'release' -NoNewline
    foreach ($dll in @('avcodec-61.dll', 'avformat-61.dll', 'avutil-59.dll', 'swresample-5.dll', 'swscale-8.dll')) {
        Set-Content -LiteralPath (Join-Path $goodStagedFfmpegRoot $dll) -Value 'runtime' -NoNewline
    }
    Assert-CimbarpunkStagedFfmpegBackend -StagingRoot $goodStagedFfmpegRoot

    $incompleteStagedFfmpegRoot = Join-Path $temporaryRoot 'incomplete-staged-ffmpeg'
    Copy-Item -LiteralPath $goodStagedFfmpegRoot -Destination $incompleteStagedFfmpegRoot -Recurse
    Remove-Item -LiteralPath (Join-Path $incompleteStagedFfmpegRoot 'swscale-8.dll') -Force
    $incompleteStagedFfmpegRejected = $false
    try {
        Assert-CimbarpunkStagedFfmpegBackend -StagingRoot $incompleteStagedFfmpegRoot
    }
    catch {
        $incompleteStagedFfmpegRejected = $true
    }
    if (-not $incompleteStagedFfmpegRejected) {
        throw 'A staged package missing an FFmpeg runtime DLL was accepted.'
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
