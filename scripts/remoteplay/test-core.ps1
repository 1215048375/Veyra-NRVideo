[CmdletBinding()]
param(
    [string]$Root = (Resolve-Path (Join-Path $PSScriptRoot "../..")),
    [string]$BuildDirectory = "",
    [string]$Generator = "Ninja",
    [ValidateSet("Debug", "Release")][string]$Configuration = "Debug"
)
$ErrorActionPreference = "Stop"
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $Root "out/remoteplay/core-$Configuration" }
function Invoke-Checked([string]$Command, [string[]]$Arguments) {
    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Command failed with exit code $LASTEXITCODE" }
}
# For Ninja/MSVC use an x64 Developer PowerShell. No administrator permission needed.
Invoke-Checked "cmake" @("-S", (Join-Path $Root "services/remoteplay-probe"), "-B", $BuildDirectory,
    "-G", $Generator, "-DCMAKE_BUILD_TYPE=$Configuration", "-DVEYRA_RP_BUILD_NATIVE=OFF")
Invoke-Checked "cmake" @("--build", $BuildDirectory, "--config", $Configuration)
Invoke-Checked "ctest" @("--test-dir", $BuildDirectory, "-C", $Configuration, "--output-on-failure", "--timeout", "30")
Write-Host "Offline core tests completed. This does NOT test the Windows player, Chiaki native linkage, PS5 or GPU."
