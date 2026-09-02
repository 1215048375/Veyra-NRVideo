[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Root
)

# Stage the local NVIDIA DLSSNR runtime for Phase 0+ probes (Playbook 4.3/4.4).
# - Copies (never moves) the workspace-root nvngx_dlssnr.dll into
#   runtime_local/nvidia/ after verifying its pinned identity.
# - Writes runtime-manifest.json with the real size/hash and local-experimental
#   mode markers.
# - Creates a persistent runtime_local/config/ngx-local.json identity exactly
#   once; reruns never regenerate the GUID.

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$expectedSize = [long]165840496
$expectedSha256 = "E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E"

$sourceDll = Join-Path $Root "nvngx_dlssnr.dll"
if (-not (Test-Path -LiteralPath $sourceDll -PathType Leaf)) {
    Write-Host "stage-runtime.ps1: source DLL missing: $sourceDll"
    exit 2
}

$item = Get-Item -LiteralPath $sourceDll
if ($item.Length -ne $expectedSize) {
    Write-Host ("stage-runtime.ps1: size mismatch actual={0} expected={1}" -f $item.Length, $expectedSize)
    exit 3
}
$hash = (Get-FileHash -LiteralPath $sourceDll -Algorithm SHA256).Hash.ToUpperInvariant()
if ($hash -ne $expectedSha256) {
    Write-Host ("stage-runtime.ps1: sha256 mismatch actual={0}" -f $hash)
    exit 3
}
$signature = Get-AuthenticodeSignature -LiteralPath $sourceDll
if ([string]$signature.Status -ne "Valid") {
    Write-Host ("stage-runtime.ps1: signature status {0}" -f [string]$signature.Status)
    exit 3
}

$runtimeDir = Join-Path $Root "runtime_local\nvidia"
New-Item -ItemType Directory -Force -Path $runtimeDir | Out-Null
$targetDll = Join-Path $runtimeDir "nvngx_dlssnr.dll"

if (-not (Test-Path -LiteralPath $targetDll -PathType Leaf)) {
    Copy-Item -LiteralPath $sourceDll -Destination $targetDll
    Write-Host "stage-runtime.ps1: copied nvngx_dlssnr.dll into runtime_local/nvidia"
}
else {
    $stagedHash = (Get-FileHash -LiteralPath $targetDll -Algorithm SHA256).Hash.ToUpperInvariant()
    if ($stagedHash -ne $expectedSha256) {
        Write-Host "stage-runtime.ps1: staged DLL identity drifted; replacing from workspace root"
        Copy-Item -LiteralPath $sourceDll -Destination $targetDll -Force
    }
    else {
        Write-Host "stage-runtime.ps1: staged DLL already matches the pinned identity"
    }
}

$manifestPath = Join-Path $runtimeDir "runtime-manifest.json"
$manifestJson = @'
{
  "schema": 1,
  "mode": "local-experimental-only",
  "files": [
    {
      "name": "nvngx_dlssnr.dll",
      "size": 165840496,
      "sha256": "E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E",
      "fileVersion": "310.8.0.0",
      "authenticode": "Valid",
      "source": "user-provided workspace file",
      "redistributable": false
    }
  ]
}
'@
Set-Content -LiteralPath $manifestPath -Value $manifestJson -Encoding utf8
Write-Host "stage-runtime.ps1: runtime-manifest.json written"

$configDir = Join-Path $Root "runtime_local\config"
New-Item -ItemType Directory -Force -Path $configDir | Out-Null
$identityPath = Join-Path $configDir "ngx-local.json"
if (-not (Test-Path -LiteralPath $identityPath -PathType Leaf)) {
    $id = [guid]::NewGuid().ToString()
    $identityJson = @{
        ngxProjectId = $id
        engineType = 'custom'
        engineVersion = 'Veyra-Experimental-0.1.0'
    } | ConvertTo-Json
    Set-Content -LiteralPath $identityPath -Value $identityJson -Encoding utf8
    Write-Host "stage-runtime.ps1: persistent local NGX identity created"
}
else {
    Write-Host "stage-runtime.ps1: local NGX identity already present (kept)"
}

exit 0
