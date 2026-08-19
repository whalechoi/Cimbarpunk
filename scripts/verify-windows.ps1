[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$QtRoot,

    [Parameter(Mandatory = $true)]
    [string]$VcpkgRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Program failed with exit code $LASTEXITCODE"
    }
}

& (Join-Path $PSScriptRoot 'configure-windows.ps1') -QtRoot $QtRoot -VcpkgRoot $VcpkgRoot -Preset windows-release
if ($LASTEXITCODE -ne 0) { throw 'Windows configure script failed.' }

$repositoryRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$cmake = (Get-Command cmake.exe -ErrorAction Stop).Source
$ctest = Join-Path (Split-Path -Parent $cmake) 'ctest.exe'
$installDirectory = Join-Path $repositoryRoot 'out\install\windows-release'
$expectedInstallParent = [IO.Path]::GetFullPath((Join-Path $repositoryRoot 'out\install'))
$resolvedInstall = [IO.Path]::GetFullPath($installDirectory)
if (-not $resolvedInstall.StartsWith($expectedInstallParent + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to replace unexpected install directory: $resolvedInstall"
}
if (Test-Path -LiteralPath $resolvedInstall) {
    Remove-Item -LiteralPath $resolvedInstall -Recurse -Force
}

Push-Location $repositoryRoot
try {
    Invoke-Checked $cmake @('--build', '--preset', 'windows-release', '--target', 'cimbarpunk', 'cimbarpunk_tests', 'cimbarpunk_frame_player')
    Invoke-Checked $ctest @('--preset', 'windows-release', '--output-on-failure')
    Invoke-Checked $cmake @('--install', 'out/build/windows-release', '--prefix', $resolvedInstall)

    $windeployqt = Join-Path $QtRoot 'bin\windeployqt.exe'
    if (-not (Test-Path -LiteralPath $windeployqt -PathType Leaf)) {
        throw "windeployqt was not found: $windeployqt"
    }
    $application = Join-Path $resolvedInstall 'cimbarpunk.exe'
    Invoke-Checked $windeployqt @(
        '--release',
        '--qmldir', (Join-Path $repositoryRoot 'src\selection\qml'),
        '--include-plugins', 'qsvgicon',
        '--plugindir', (Join-Path $resolvedInstall 'plugins'),
        '--qml-deploy-dir', (Join-Path $resolvedInstall 'qml'),
        '--dir', $resolvedInstall,
        $application
    )

    foreach ($required in @(
        $application,
        (Join-Path $resolvedInstall 'Qt6Svg.dll'),
        (Join-Path $resolvedInstall 'plugins\iconengines\qsvgicon.dll'),
        (Join-Path $resolvedInstall 'plugins\platforms\qwindows.dll'),
        (Join-Path $resolvedInstall 'share\licenses\cimbarpunk\LICENSE'),
        (Join-Path $resolvedInstall 'share\licenses\cimbarpunk\THIRD_PARTY_NOTICES.md'),
        (Join-Path $resolvedInstall 'share\licenses\cimbarpunk\libcimbar-MPL-2.0.txt')
    )) {
        if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
            throw "Verification artifact is missing: $required"
        }
    }
}
finally {
    Pop-Location
}
