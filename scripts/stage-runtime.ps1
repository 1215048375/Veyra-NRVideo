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

# Stage the official DLSS SR DLL from the SDK (Playbook section 3.2).
$sdkRel = Join-Path $Root "third_party_local\nvidia\DLSS_SDK_310.7.0\lib\Windows_x86_64\rel\nvngx_dlss.dll"
$srDll = Join-Path $runtimeDir "nvngx_dlss.dll"
if (Test-Path -LiteralPath $sdkRel -PathType Leaf) {
    if (-not (Test-Path -LiteralPath $srDll -PathType Leaf)) {
        Copy-Item -LiteralPath $sdkRel -Destination $srDll
        Write-Host "stage-runtime.ps1: copied nvngx_dlss.dll from SDK rel/"
    }
    else {
        $sdkHash = (Get-FileHash -LiteralPath $sdkRel -Algorithm SHA256).Hash.ToUpperInvariant()
        $stagedSrHash = (Get-FileHash -LiteralPath $srDll -Algorithm SHA256).Hash.ToUpperInvariant()
        if ($sdkHash -ne $stagedSrHash) {
            Copy-Item -LiteralPath $sdkRel -Destination $srDll -Force
            Write-Host "stage-runtime.ps1: staged nvngx_dlss.dll drifted; replaced from SDK"
        }
    }
}

$manifestPath = Join-Path $runtimeDir "runtime-manifest.json"
$srManifestEntry = ""
if (Test-Path -LiteralPath $srDll -PathType Leaf) {
    $srItem = Get-Item -LiteralPath $srDll
    $srHash = (Get-FileHash -LiteralPath $srDll -Algorithm SHA256).Hash.ToUpperInvariant()
    $srSig = Get-AuthenticodeSignature -LiteralPath $srDll
    $srVersion = [string]$srItem.VersionInfo.FileVersion
    $srManifestEntry = ",`n    {`n      `"name`": `"nvngx_dlss.dll`",`n      `"size`": $($srItem.Length),`n      `"sha256`": `"$srHash`",`n      `"fileVersion`": `"$srVersion`",`n      `"authenticode`": `"$([string]$srSig.Status)`",`n      `"source`": `"official DLSS SDK 310.7.0 rel`",`n      `"redistributable`": false`n    }"
}
$manifestJson = "@'`n{`n  `"schema`": 1,`n  `"mode`": `"local-experimental-only`",`n  `"files`": [`n    {`n      `"name`": `"nvngx_dlssnr.dll`",`n      `"size`": 165840496,`n      `"sha256`": `"E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E`",`n      `"fileVersion`": `"310.8.0.0`",`n      `"authenticode`": `"Valid`",`n      `"source`": `"user-provided workspace file`",`n      `"redistributable`": false`n    }$srManifestEntry`n  ]`n}`n'@"
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
