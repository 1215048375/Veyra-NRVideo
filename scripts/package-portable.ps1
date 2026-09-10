param(
  [Parameter(Mandatory = $true)][string]$Root,
  [Parameter(Mandatory = $true)][string]$Version,
  [Parameter(Mandatory = $true)][string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'
$resolvedRoot = [IO.Path]::GetFullPath($Root)
$resolvedOutput = [IO.Path]::GetFullPath($OutputDirectory)
$bin = Join-Path $resolvedRoot 'out/build/x64-release'
$stage = Join-Path $resolvedOutput ("Veyra-" + $Version + "-win64-portable")
$archive = $stage + '.zip'
$runtimeStage = Join-Path $stage 'runtime_local/nvidia'
$licensesStage = Join-Path $stage 'licenses'

$applicationFiles = @('veyra.exe', 'veyra_fruc_worker.exe', 'avcodec-63.dll', 'avformat-63.dll', 'avutil-61.dll', 'swresample-7.dll', 'swscale-10.dll')
foreach ($file in $applicationFiles) {
  if (-not (Test-Path -LiteralPath (Join-Path $bin $file))) { throw "Missing Release file: $file" }
}

# Explicit user-authorized Runtime Pack allowlist. No wildcards are permitted.
$runtimeFiles = @(
  @{ Name = 'nvngx_dlss.dll'; Source = (Join-Path $resolvedRoot 'runtime_local/nvidia/nvngx_dlss.dll'); Sha256 = 'BE6E434A94CA32499515EB62CA0E6C274526055D568D0426E4C652DCDFB6EE6E'; Classification = 'official-sdk-runtime'; Experimental = $false },
  @{ Name = 'nvngx_dlssg.dll'; Source = (Join-Path $resolvedRoot 'runtime_local/nvidia/nvngx_dlssg.dll'); Sha256 = '135EAF0733C1E37381A8C28ABCF7A862404A54132B81787C04E35D09EFC5E36F'; Classification = 'community-experimental-runtime'; Experimental = $true },
  @{ Name = 'nvngx_dlssnr.dll'; Source = (Join-Path $resolvedRoot 'runtime_local/nvidia/nvngx_dlssnr.dll'); Sha256 = 'E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E'; Classification = 'community-experimental-runtime'; Experimental = $true },
  @{ Name = 'nvngx_vsr.dll'; Source = (Join-Path $resolvedRoot 'runtime_local/nvidia/nvngx_vsr.dll'); Sha256 = 'C3D88EEA5FF7A548EDEFA66414CF6E77464D0947277C904F324DD23ABF58A1ED'; Classification = 'official-rtx-video-sdk-runtime'; Experimental = $false },
  @{ Name = 'NvOFFRUC.dll'; Source = (Join-Path $resolvedRoot 'third_party_local/nvidia/Optical_Flow_SDK_5.0.7/NvOFFRUC/NvOFFRUCSample/bin/win64/NvOFFRUC.dll'); Sha256 = '5A0B6701D30709E25E7E5B92CA46B18AAB1459160CECD4F629872369D85C8B0A'; Classification = 'community-experimental-runtime'; Experimental = $true },
  # Direct PE import of NvOFFRUC.dll. Keep the pinned dependency beside it;
  # do not assume a CUDA Toolkit or a globally installed cudart on user PCs.
  @{ Name = 'cudart64_110.dll'; Source = (Join-Path $resolvedRoot 'third_party_local/nvidia/Optical_Flow_SDK_5.0.7/NvOFFRUC/NvOFFRUCSample/bin/win64/cudart64_110.dll'); Sha256 = 'EDC35E7D0FA3F257BBEDFA7888911080C5696ACDD40B6187B6DD0173F20759AD'; Classification = 'cuda-runtime-dependency'; Experimental = $false }
)
foreach ($item in $runtimeFiles) {
  if (-not (Test-Path -LiteralPath $item.Source)) { throw "Missing approved runtime: $($item.Name)" }
  if ((Get-FileHash -Algorithm SHA256 -LiteralPath $item.Source).Hash.ToUpperInvariant() -ne $item.Sha256) { throw "Runtime hash mismatch for $($item.Name)" }
  if ((Get-AuthenticodeSignature -LiteralPath $item.Source).Status -ne 'Valid') { throw "Runtime signature is not Valid for $($item.Name)" }
}

if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
if (Test-Path -LiteralPath $archive) { Remove-Item -LiteralPath $archive -Force }
New-Item -ItemType Directory -Path $runtimeStage -Force | Out-Null
New-Item -ItemType Directory -Path $licensesStage -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $stage 'runtime_local/config') -Force | Out-Null

foreach ($file in $applicationFiles) { Copy-Item -LiteralPath (Join-Path $bin $file) -Destination $stage }
Copy-Item -LiteralPath (Join-Path $resolvedRoot 'LICENSE') -Destination $stage
Copy-Item -LiteralPath (Join-Path $resolvedRoot 'THIRD_PARTY_NOTICES.md') -Destination $stage
Copy-Item -LiteralPath (Join-Path $resolvedRoot 'docs/RELEASE_NOTES_0.0.1.md') -Destination (Join-Path $stage 'RELEASE_NOTES.md')
Copy-Item -LiteralPath 'C:/veyra-deps/installed/x64-windows/share/ffmpeg/copyright' -Destination (Join-Path $stage 'FFMPEG-COPYRIGHT.txt')
Copy-Item -LiteralPath (Join-Path $bin 'shaders') -Destination (Join-Path $stage 'shaders') -Recurse
Copy-Item -LiteralPath (Join-Path $resolvedRoot 'runtime_local/config/ngx-local.json') -Destination (Join-Path $stage 'runtime_local/config/ngx-local.json')
Copy-Item -LiteralPath (Join-Path $resolvedRoot 'third_party_local/nvidia/DLSS_repo/LICENSE.txt') -Destination (Join-Path $licensesStage 'NVIDIA_RTX_SDK_LICENSE.txt')
Copy-Item -LiteralPath (Join-Path $resolvedRoot 'third_party_local/nvidia/RTX_Video_SDK_1.1.0/NVIDIA_RTX_Video_SDK_License.pdf') -Destination (Join-Path $licensesStage 'NVIDIA_RTX_VIDEO_SDK_LICENSE.pdf')
Copy-Item -LiteralPath (Join-Path $resolvedRoot 'third_party_local/nvidia/Optical_Flow_SDK_5.0.7/LicenseAgreement.pdf') -Destination (Join-Path $licensesStage 'NVIDIA_OPTICAL_FLOW_SDK_LICENSE.pdf')

$manifestFiles = foreach ($item in $runtimeFiles) {
  Copy-Item -LiteralPath $item.Source -Destination (Join-Path $runtimeStage $item.Name)
  $file = Get-Item -LiteralPath $item.Source
  $signature = Get-AuthenticodeSignature -LiteralPath $item.Source
  $version = [Diagnostics.FileVersionInfo]::GetVersionInfo($item.Source).FileVersion
  [ordered]@{ name = $item.Name; size = $file.Length; sha256 = $item.Sha256; fileVersion = $version; authenticode = $signature.Status.ToString(); signer = if ($signature.SignerCertificate) { $signature.SignerCertificate.Subject } else { $null }; classification = $item.Classification; experimental = $item.Experimental }
}
[ordered]@{ schema = 1; package = "Veyra $Version"; mode = 'user-authorized-runtime-pack'; warning = 'Experimental runtime components are not an NVIDIA official endorsement or support statement.'; files = @($manifestFiles) } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $runtimeStage 'release-runtime-manifest.json') -Encoding UTF8

@'
This directory contains the Veyra experimental Runtime Pack selected by the
publisher. It is not source code and must not be copied into the Veyra Git
repository. The package checks release-runtime-manifest.json before enabling
NVIDIA-backed enhancement features.
'@ | Set-Content -LiteralPath (Join-Path $runtimeStage 'README.txt') -Encoding UTF8

Compress-Archive -LiteralPath $stage -DestinationPath $archive -CompressionLevel Optimal
Get-ChildItem -LiteralPath $stage -Recurse -File | Get-FileHash -Algorithm SHA256 | Select-Object Path, Hash | ConvertTo-Json | Set-Content -LiteralPath ($archive + '.sha256.json') -Encoding UTF8
Get-FileHash -LiteralPath $archive -Algorithm SHA256 | Format-List
