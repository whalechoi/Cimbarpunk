[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Archive,
    [Parameter(Mandatory = $true)][string]$SourceDirectory,
    [Parameter(Mandatory = $true)][string]$BuildDirectory,
    [Parameter(Mandatory = $true)][string]$InstallPrefix
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$expectedSha256 = 'F56EA93356ECE3BCA727815233B86D9E1242D28D418074389FADCE683227C87C'

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
if (-not (Test-Path -LiteralPath (Join-Path $SourceDirectory 'configure.bat') -PathType Leaf)) {
    throw "Extract the verified archive first; configure.bat is missing from $SourceDirectory"
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

Push-Location $BuildDirectory
try {
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
    foreach ($configuration in @('Release', 'Debug')) {
        & $cmake --build . --config $configuration --parallel
        if ($LASTEXITCODE -ne 0) { throw "Qt $configuration build failed." }
        & $cmake --install . --config $configuration
        if ($LASTEXITCODE -ne 0) { throw "Qt $configuration install failed." }
    }
}
finally {
    Pop-Location
}

foreach ($required in @(
    (Join-Path $InstallPrefix 'lib\cmake\Qt6Core\Qt6CoreTargets-release.cmake'),
    (Join-Path $InstallPrefix 'lib\cmake\Qt6Core\Qt6CoreTargets-debug.cmake'),
    (Join-Path $InstallPrefix 'plugins\iconengines\qsvgicon.dll'),
    (Join-Path $InstallPrefix 'plugins\iconengines\qsvgicond.dll')
)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Qt SDK verification failed: $required" }
}
