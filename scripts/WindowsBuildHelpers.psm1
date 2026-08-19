Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

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

Export-ModuleMember -Function Enable-CimbarpunkPkgconfRetry
