[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$ModulePath,
    [Parameter(Mandatory = $true)][string]$ToolPath
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
}
finally {
    [Environment]::SetEnvironmentVariable('PKG_CONFIG', $savedPkgConfig, 'Process')
    [Environment]::SetEnvironmentVariable('VCPKG_KEEP_ENV_VARS', $savedKeepVars, 'Process')
}
