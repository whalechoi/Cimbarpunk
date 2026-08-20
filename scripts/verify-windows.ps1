[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$QtRoot,

    [Parameter(Mandatory = $true)]
    [string]$VcpkgRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'WindowsBuildHelpers.psm1') -Force

function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Program failed with exit code $LASTEXITCODE"
    }
}

function Assert-MirroredFiles(
    [string]$SourceRoot,
    [string]$DestinationRoot,
    [string[]]$NamePatterns
) {
    if (-not (Test-Path -LiteralPath $SourceRoot -PathType Container)) {
        throw "License source directory is missing: $SourceRoot"
    }
    $sourceRootPath = (Resolve-Path -LiteralPath $SourceRoot).Path.TrimEnd('\', '/')
    $sourceFiles = @(Get-ChildItem -LiteralPath $sourceRootPath -File -Recurse | Where-Object {
        $included = $false
        foreach ($pattern in $NamePatterns) {
            if ($_.Name -like $pattern) { $included = $true; break }
        }
        $included
    })
    if ($sourceFiles.Count -eq 0) {
        throw "No license files matched under: $SourceRoot"
    }
    foreach ($sourceFile in $sourceFiles) {
        $relative = $sourceFile.FullName.Substring($sourceRootPath.Length).TrimStart('\', '/')
        $destination = Join-Path $DestinationRoot $relative
        if (-not (Test-Path -LiteralPath $destination -PathType Leaf)) {
            throw "Packaged license file is missing: $destination"
        }
        $sourceHash = (Get-FileHash -LiteralPath $sourceFile.FullName -Algorithm SHA256).Hash
        $destinationHash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash
        if ($sourceHash -ne $destinationHash) {
            throw "Packaged license file differs from its source: $destination"
        }
    }
}

function Assert-StagedModule([Diagnostics.Process]$Process, [string]$ModuleName, [string]$ExpectedPath) {
    $module = @($Process.Modules | Where-Object ModuleName -IEQ $ModuleName) | Select-Object -First 1
    if ($null -eq $module) {
        throw "Staged application did not load required module: $ModuleName"
    }
    $actual = [IO.Path]::GetFullPath($module.FileName)
    $expected = [IO.Path]::GetFullPath($ExpectedPath)
    if (-not $actual.Equals($expected, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Staged application loaded $ModuleName outside the package: $actual"
    }
}

& (Join-Path $PSScriptRoot 'configure-windows.ps1') -QtRoot $QtRoot -VcpkgRoot $VcpkgRoot -Preset windows-release
if ($LASTEXITCODE -ne 0) { throw 'Windows configure script failed.' }

$repositoryRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$cmake = (Get-Command cmake.exe -ErrorAction Stop).Source
$ctest = Join-Path (Split-Path -Parent $cmake) 'ctest.exe'
$installDirectory = Join-Path $repositoryRoot 'out\install\windows-release'
$resolvedInstall = [IO.Path]::GetFullPath($installDirectory)
Remove-CimbarpunkStagingDirectory -RepositoryRoot $repositoryRoot -StagingDirectory $resolvedInstall

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
    Assert-CimbarpunkStagedFfmpegBackend -StagingRoot $resolvedInstall

    foreach ($required in @(
        $application,
        (Join-Path $resolvedInstall 'z.dll'),
        (Join-Path $resolvedInstall 'Qt6Svg.dll'),
        (Join-Path $resolvedInstall 'plugins\iconengines\qsvgicon.dll'),
        (Join-Path $resolvedInstall 'plugins\platforms\qwindows.dll'),
        (Join-Path $resolvedInstall 'share\cimbarpunk\qml\SelectionOverlay.qml'),
        (Join-Path $resolvedInstall 'share\icons\hicolor\scalable\apps\cimbarpunk.svg'),
        (Join-Path $resolvedInstall 'share\licenses\cimbarpunk\LICENSE'),
        (Join-Path $resolvedInstall 'share\licenses\cimbarpunk\THIRD_PARTY_NOTICES.md'),
        (Join-Path $resolvedInstall 'share\licenses\cimbarpunk\libcimbar-MPL-2.0.txt')
    )) {
        if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
            throw "Verification artifact is missing: $required"
        }
    }

    foreach ($systemDll in @('kernel32.dll', 'user32.dll', 'ntdll.dll')) {
        if (Test-Path -LiteralPath (Join-Path $resolvedInstall $systemDll) -PathType Leaf) {
            throw "Windows system DLL must not be packaged: $systemDll"
        }
    }

    $licenseRoot = Join-Path $resolvedInstall 'share\licenses\cimbarpunk'
    Assert-MirroredFiles (Join-Path $QtRoot 'share\licenses\qt') (Join-Path $licenseRoot 'qt') @('*')
    Assert-MirroredFiles (Join-Path $QtRoot 'sbom') (Join-Path $licenseRoot 'qt-sbom') @('*.spdx')
    Assert-CimbarpunkQtSbomCorpus -SbomRoot (Join-Path $licenseRoot 'qt-sbom')
    Assert-MirroredFiles (Join-Path $repositoryRoot 'out\build\windows-release\vcpkg_installed\x64-windows\share') (Join-Path $licenseRoot 'vcpkg') @('copyright')
    Assert-MirroredFiles (Join-Path $repositoryRoot 'external\libcimbar\src\third_party_lib') (Join-Path $licenseRoot 'libcimbar-vendored') @('LICENSE', 'LICENSE.*', 'COPYING', 'NOTICE*', 'base.hpp')
    $libcimbarHash = (Get-FileHash -LiteralPath (Join-Path $repositoryRoot 'external\libcimbar\LICENSE') -Algorithm SHA256).Hash
    $packagedLibcimbarHash = (Get-FileHash -LiteralPath (Join-Path $licenseRoot 'libcimbar-MPL-2.0.txt') -Algorithm SHA256).Hash
    if ($libcimbarHash -ne $packagedLibcimbarHash) {
        throw 'Packaged libcimbar MPL-2.0 text differs from the fixed submodule license.'
    }

    $environmentNames = @(
        'PATH',
        'QT_PLUGIN_PATH',
        'QT_QPA_PLATFORM_PLUGIN_PATH',
        'QML2_IMPORT_PATH',
        'QML_IMPORT_PATH',
        'QT_MEDIA_BACKEND',
        'CIMBARPUNK_EXPECT_FFMPEG_PLUGIN'
    )
    $savedEnvironment = @{}
    foreach ($name in $environmentNames) {
        $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
    }
    $stagedProcess = $null
    $captureProcess = $null
    $captureProbeDirectory = $null
    try {
        $captureProbeSource = Join-Path $repositoryRoot 'out\build\windows-release\tst_windows_screen_capture.exe'
        if (-not (Test-Path -LiteralPath $captureProbeSource -PathType Leaf)) {
            throw "Windows screen-capture probe is missing: $captureProbeSource"
        }
        $qtTestSource = Join-Path $QtRoot 'bin\Qt6Test.dll'
        if (-not (Test-Path -LiteralPath $qtTestSource -PathType Leaf)) {
            throw "QtTest runtime for the isolated capture probe is missing: $qtTestSource"
        }
        $temporaryRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\', '/')
        $captureProbeDirectory = Join-Path $temporaryRoot (
            'cimbarpunk-staged-capture-probe-' + [Guid]::NewGuid().ToString('N'))
        $captureProbeDirectory = [IO.Path]::GetFullPath($captureProbeDirectory)
        if (-not $captureProbeDirectory.StartsWith(
                $temporaryRoot + [IO.Path]::DirectorySeparatorChar,
                [StringComparison]::OrdinalIgnoreCase)) {
            throw "Capture probe directory escaped the temporary root: $captureProbeDirectory"
        }
        New-Item -ItemType Directory -Path $captureProbeDirectory | Out-Null
        $captureProbe = Join-Path $captureProbeDirectory 'tst_windows_screen_capture.exe'
        $captureLog = Join-Path $captureProbeDirectory 'windows-screen-capture-staged.txt'
        Copy-Item -LiteralPath $captureProbeSource -Destination $captureProbe
        Copy-Item -LiteralPath $qtTestSource -Destination (Join-Path $captureProbeDirectory 'Qt6Test.dll')

        $env:PATH = "$resolvedInstall;$env:SystemRoot\System32;$env:SystemRoot"
        foreach ($name in $environmentNames | Where-Object { $_ -ne 'PATH' }) {
            [Environment]::SetEnvironmentVariable($name, $null, 'Process')
        }
        $env:QT_PLUGIN_PATH = Join-Path $resolvedInstall 'plugins'
        $env:CIMBARPUNK_EXPECT_FFMPEG_PLUGIN =
            Join-Path $resolvedInstall 'plugins\multimedia\ffmpegmediaplugin.dll'

        $captureProcess = Start-Process -FilePath $captureProbe `
            -ArgumentList @('-o', ('"{0},txt"' -f $captureLog)) `
            -WorkingDirectory $resolvedInstall `
            -WindowStyle Hidden `
            -PassThru
        try {
            if (-not $captureProcess.WaitForExit(30000)) {
                throw 'Staged Windows screen-capture probe exceeded its 30-second deadline.'
            }
            if ($captureProcess.ExitCode -ne 0) {
                $captureOutput = if (Test-Path -LiteralPath $captureLog -PathType Leaf) {
                    Get-Content -LiteralPath $captureLog -Raw
                }
                else {
                    '<no QtTest output was written>'
                }
                throw "Staged Windows screen-capture probe failed with exit code $($captureProcess.ExitCode):`n$captureOutput"
            }
        }
        finally {
            if ($null -ne $captureProcess -and -not $captureProcess.HasExited) {
                Stop-Process -Id $captureProcess.Id -Force
                $captureProcess.WaitForExit()
            }
        }
        Write-Host 'Staged Windows screen capture produced a real desktop frame through the package-local FFmpeg plugin.'

        $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
        [Environment]::SetEnvironmentVariable('QT_PLUGIN_PATH', $null, 'Process')
        [Environment]::SetEnvironmentVariable('CIMBARPUNK_EXPECT_FFMPEG_PLUGIN', $null, 'Process')
        $stagedProcess = Start-Process -FilePath $application -WorkingDirectory $resolvedInstall -WindowStyle Hidden -PassThru
        try {
            Start-Sleep -Seconds 3
            $stagedProcess.Refresh()
            if ($stagedProcess.HasExited) {
                throw "Staged application exited during bounded launch verification: $($stagedProcess.ExitCode)"
            }
            if ($stagedProcess.MainWindowHandle -ne 0) {
                throw 'Staged tray application unexpectedly created a main window.'
            }
            Assert-StagedModule $stagedProcess 'Qt6Core.dll' (Join-Path $resolvedInstall 'Qt6Core.dll')
            Assert-StagedModule $stagedProcess 'qsvgicon.dll' (Join-Path $resolvedInstall 'plugins\iconengines\qsvgicon.dll')
        }
        finally {
            if ($null -ne $stagedProcess -and -not $stagedProcess.HasExited) {
                Stop-Process -Id $stagedProcess.Id -Force
                $stagedProcess.WaitForExit()
            }
        }
        Write-Host 'Bounded staged launch passed with package-local Qt6Core and qsvgicon; process was force-stopped after verification.'
    }
    finally {
        try {
            if ($null -ne $captureProbeDirectory -and
                (Test-Path -LiteralPath $captureProbeDirectory -PathType Container)) {
                $temporaryRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\', '/')
                $resolvedProbeDirectory = [IO.Path]::GetFullPath($captureProbeDirectory)
                if (-not $resolvedProbeDirectory.StartsWith(
                        $temporaryRoot + [IO.Path]::DirectorySeparatorChar,
                        [StringComparison]::OrdinalIgnoreCase)) {
                    throw "Refusing to remove capture probe directory outside the temporary root: $resolvedProbeDirectory"
                }
                Remove-Item -LiteralPath $resolvedProbeDirectory -Recurse -Force
            }
        }
        finally {
            foreach ($name in $environmentNames) {
                [Environment]::SetEnvironmentVariable($name, $savedEnvironment[$name], 'Process')
            }
        }
    }
}
finally {
    Pop-Location
}
