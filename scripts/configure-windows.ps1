[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$QtRoot,

    [Parameter(Mandatory = $true)]
    [string]$VcpkgRoot,

    [ValidateSet('windows-debug', 'windows-release')]
    [string]$Preset = 'windows-release'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Resolve-AbsoluteDirectory([string]$Name, [string]$Value) {
    if (-not [IO.Path]::IsPathFullyQualified($Value)) {
        throw "$Name must be an absolute path: $Value"
    }
    if (-not (Test-Path -LiteralPath $Value -PathType Container)) {
        throw "$Name does not exist or is not a directory: $Value"
    }
    return (Resolve-Path -LiteralPath $Value).Path
}

function Find-VsWhere {
    $standard = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $standard -PathType Leaf) {
        return $standard
    }
    $command = Get-Command vswhere.exe -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        return $command.Source
    }
    throw 'Visual Studio Installer vswhere.exe was not found.'
}

function Import-Vs2022Environment {
    $vswhere = Find-VsWhere
    $installation = (& $vswhere -latest -products * -version '[17.0,18.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath).Trim()
    if ([string]::IsNullOrWhiteSpace($installation)) {
        throw 'Visual Studio 2022 with the x64 C++ toolchain was not found.'
    }
    $devCommand = Join-Path $installation 'Common7\Tools\VsDevCmd.bat'
    if (-not (Test-Path -LiteralPath $devCommand -PathType Leaf)) {
        throw "Visual Studio developer command file was not found: $devCommand"
    }

    $environmentLines = & $env:ComSpec /d /s /c "`"$devCommand`" -arch=x64 -host_arch=x64 >nul && set"
    if ($LASTEXITCODE -ne 0) {
        throw "VsDevCmd.bat failed with exit code $LASTEXITCODE"
    }
    foreach ($line in $environmentLines) {
        if ($line -match '^([^=]+)=(.*)$') {
            [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process')
        }
    }
    $compiler = Get-Command cl.exe -ErrorAction Stop
    $versionOutput = (@(& $compiler.Source /Bv 2>&1) -join "`n")
    if ($versionOutput -notmatch '19\.\d+') {
        throw "Unexpected MSVC compiler version output: $versionOutput"
    }
}

function Find-Tool([string]$Name, [string[]]$Fallbacks) {
    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        return $command.Source
    }
    foreach ($candidate in $Fallbacks) {
        if (-not [string]::IsNullOrWhiteSpace($candidate) -and (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            return $candidate
        }
    }
    throw "$Name was not found. Install it or add it to PATH."
}

function Invoke-Configure([string]$CMake, [string]$SelectedPreset) {
    $lines = @(& $CMake --preset $SelectedPreset 2>&1)
    $exitCode = $LASTEXITCODE
    $lines | ForEach-Object { Write-Host $_ }
    return [pscustomobject]@{ ExitCode = $exitCode; Output = ($lines -join "`n") }
}

function Find-Pkgconf([string]$RepositoryRoot, [string]$SelectedPreset, [string]$ResolvedVcpkgRoot) {
    $searchRoots = @(
        (Join-Path $RepositoryRoot "out\build\$SelectedPreset\vcpkg_installed"),
        (Join-Path $ResolvedVcpkgRoot 'downloads\tools')
    )
    foreach ($root in $searchRoots) {
        if (Test-Path -LiteralPath $root -PathType Container) {
            $candidate = Get-ChildItem -LiteralPath $root -Filter pkgconf.exe -File -Recurse -ErrorAction SilentlyContinue |
                Select-Object -First 1
            if ($null -ne $candidate) {
                return $candidate.FullName
            }
        }
    }
    return $null
}

$QtRoot = Resolve-AbsoluteDirectory 'QtRoot' $QtRoot
$VcpkgRoot = Resolve-AbsoluteDirectory 'VcpkgRoot' $VcpkgRoot
$repositoryRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path

$qtConfig = Join-Path $QtRoot 'lib\cmake\Qt6\Qt6Config.cmake'
$qtSvgConfig = Join-Path $QtRoot 'lib\cmake\Qt6Svg\Qt6SvgConfig.cmake'
$qsvgicon = Join-Path $QtRoot 'plugins\iconengines\qsvgicon.dll'
$qsvgiconDebug = Join-Path $QtRoot 'plugins\iconengines\qsvgicond.dll'
$qtpaths = Join-Path $QtRoot 'bin\qtpaths.exe'
$vcpkg = Join-Path $VcpkgRoot 'vcpkg.exe'
foreach ($required in @($qtConfig, $qtSvgConfig, $qsvgicon, $qsvgiconDebug, $qtpaths, $vcpkg)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required SDK file was not found: $required. Qt 6.8.4 official binaries are unavailable in aqt metadata; use the documented official-source fallback."
    }
}
$qtVersion = (& $qtpaths --qt-version).Trim()
if ($LASTEXITCODE -ne 0 -or $qtVersion -ne '6.8.4') {
    throw "QtRoot must contain exact Qt 6.8.4; found '$qtVersion'."
}
foreach ($configuration in @('release', 'debug')) {
    $export = Join-Path $QtRoot "lib\cmake\Qt6Core\Qt6CoreTargets-$configuration.cmake"
    if (-not (Test-Path -LiteralPath $export -PathType Leaf)) {
        throw "QtRoot must provide both Release and Debug exports; missing $export"
    }
}

Import-Vs2022Environment
$cmake = Find-Tool 'cmake.exe' @(
    (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe')
)
$ninja = Find-Tool 'ninja.exe' @(
    (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe')
)
$cmakeVersionOutput = @(& $cmake --version)
if ($LASTEXITCODE -ne 0) { throw 'CMake version check failed.' }
$cmakeVersionOutput | Select-Object -First 1
$ninjaVersionOutput = @(& $ninja --version)
if ($LASTEXITCODE -ne 0) { throw 'Ninja version check failed.' }
$ninjaVersionOutput

$env:CIMBARPUNK_QT_ROOT = $QtRoot
$env:VCPKG_ROOT = $VcpkgRoot

Push-Location $repositoryRoot
try {
    $firstAttempt = Invoke-Configure $cmake $Preset
    if ($firstAttempt.ExitCode -eq 0) {
        return
    }
    if ($firstAttempt.Output -notmatch '(?is)(OpenCV.*pkg-?config|pkg-?config.*OpenCV|pkgconf)') {
        throw "CMake configure failed with exit code $($firstAttempt.ExitCode)."
    }

    $pkgconf = Find-Pkgconf $repositoryRoot $Preset $VcpkgRoot
    if ([string]::IsNullOrWhiteSpace($pkgconf)) {
        throw 'OpenCV pkg-config parsing failed and no installed pkgconf.exe could be discovered for the single retry.'
    }
    $env:PKG_CONFIG = '"' + $pkgconf + '"'
    $env:VCPKG_KEEP_ENV_VARS = 'PKG_CONFIG'
    Write-Host "Retrying configure once with pkgconf: $pkgconf"
    $retry = Invoke-Configure $cmake $Preset
    if ($retry.ExitCode -ne 0) {
        throw "CMake configure retry failed with exit code $($retry.ExitCode)."
    }
}
finally {
    Pop-Location
}
