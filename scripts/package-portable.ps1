param(
  [Parameter(Mandatory = $true)][string]$Root,
  [Parameter(Mandatory = $true)][ValidatePattern('^\d+\.\d+\.\d+$')][string]$Version,
  [Parameter(Mandatory = $true)][string]$OutputDirectory,
  [string]$BuildDirectory
)
$ErrorActionPreference = 'Stop'
$resolvedRoot = (Resolve-Path -LiteralPath $Root).Path
$resolvedOutput = [IO.Path]::GetFullPath($OutputDirectory)
$bin = Join-Path $resolvedRoot 'out/build/x64-release'
if ($BuildDirectory) { $bin = (Resolve-Path -LiteralPath $BuildDirectory).Path }
$stage = Join-Path $resolvedOutput "Veyra-$Version-win64-portable"
$archive = "$stage.zip"
# Preserve existing candidates and their verification evidence.
if ((Test-Path -LiteralPath $stage) -or (Test-Path -LiteralPath $archive)) { throw 'Output exists; choose a new staging directory.' }
$applicationFiles = @('veyra.exe','avcodec-63.dll','avformat-63.dll','avutil-61.dll','swresample-7.dll','swscale-10.dll')
$appVersion = [Diagnostics.FileVersionInfo]::GetVersionInfo((Join-Path $bin 'veyra.exe'))
if ($appVersion.ProductVersion -ne $Version) { throw "EXE version $($appVersion.ProductVersion) does not match $Version" }
# Publisher audit only. The application permits users to replace these DLLs.
$runtimeFiles = @(
  @{ Name='nvngx_dlss.dll'; Folder='runtime/experimental'; Source='runtime_local/nvidia/nvngx_dlss.dll'; Hash='BE6E434A94CA32499515EB62CA0E6C274526055D568D0426E4C652DCDFB6EE6E'; Category='official-dlss-sdk-310.7.0-rel'; Experimental=$false },
  @{ Name='nvngx_dlssg.dll'; Folder='runtime/experimental'; Source='runtime_local/nvidia/nvngx_dlssg.dll'; Hash='135EAF0733C1E37381A8C28ABCF7A862404A54132B81787C04E35D09EFC5E36F'; Category='pinned-dlss-sdk-310.7.0-rel'; Experimental=$true },
  @{ Name='nvngx_dlssnr.dll'; Folder='runtime/experimental'; Source='runtime_local/nvidia/nvngx_dlssnr.dll'; Hash='E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E'; Category='user-provided-pinned-experimental-runtime'; Experimental=$true },
  @{ Name='nvngx_vsr.dll'; Folder='runtime/experimental'; Source='runtime_local/nvidia/nvngx_vsr.dll'; Hash='C3D88EEA5FF7A548EDEFA66414CF6E77464D0947277C904F324DD23ABF58A1ED'; Category='official-rtx-video-sdk-1.1.0'; Experimental=$false },
  @{ Name='libxell.dll'; Folder='runtime_local/intel/experimental'; Source='runtime_local/intel/experimental/libxell.dll'; Hash='D2030DCD694FDA8F2EC7E044B13E6DB8F0B56D4BA9113A5EFAD334E3F3DED8C7'; Category='official-intel-xess-sdk-3.0.2'; Experimental=$true },
  @{ Name='libxess_fg.dll'; Folder='runtime_local/intel/experimental'; Source='runtime_local/intel/experimental/libxess_fg.dll'; Hash='EC5E0C65E075570C6EDE72618BB666D0BE0C2E10B2EA9762C0FE8CB8E375AB27'; Category='official-intel-xess-sdk-3.0.2'; Experimental=$true }
)
$records = foreach ($item in $runtimeFiles) {
  $source = Join-Path $resolvedRoot $item.Source
  $file = Get-Item -LiteralPath $source
  $hash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
  $sig = Get-AuthenticodeSignature -LiteralPath $source
  if ($hash -ne $item.Hash -or $sig.Status -ne 'Valid') { throw "Publisher runtime identity rejected: $($item.Name)" }
  $v = $file.VersionInfo
  [pscustomobject][ordered]@{name=$item.Name;path="$($item.Folder)/$($item.Name)";size=$file.Length;sha256=$hash;fileVersion="$($v.FileMajorPart).$($v.FileMinorPart).$($v.FileBuildPart).$($v.FilePrivatePart)";authenticode=$sig.Status.ToString();signer=$sig.SignerCertificate.Subject;classification=$item.Category;experimental=$item.Experimental;removable=$true;source=$item.Category}
}
$allowed = [Collections.Generic.List[string]]::new()
function Copy-Payload([string]$Source,[string]$Relative) {
  $destination = Join-Path $stage $Relative
  [IO.Directory]::CreateDirectory((Split-Path $destination)) | Out-Null
  Copy-Item -LiteralPath $Source -Destination $destination
  $allowed.Add($Relative.Replace('\','/'))
}
foreach ($name in $applicationFiles) {
  $destination = if ($name -eq 'veyra.exe') {'Veyra.exe'} else {$name}
  Copy-Payload (Join-Path $bin $name) $destination
}
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vsRoot = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath) -join ''
$crt = Get-ChildItem -LiteralPath (Join-Path $vsRoot 'VC/Redist/MSVC') -Directory | Where-Object Name -Match '^\d+\.' | Sort-Object Name -Descending | Select-Object -First 1
foreach ($name in @('vcruntime140.dll','vcruntime140_1.dll','msvcp140.dll')) {
  Copy-Payload (Join-Path $crt.FullName "x64/Microsoft.VC143.CRT/$name") $name
}
foreach ($name in @('LICENSE','README.md','README_EN.md','THIRD_PARTY_NOTICES.md')) { Copy-Payload (Join-Path $resolvedRoot $name) $name }
foreach ($name in @('BUILD.md','RUNTIME_COMPONENTS_0.0.2.md')) { Copy-Payload (Join-Path $resolvedRoot "docs/$name") "docs/$name" }
Copy-Payload (Join-Path $resolvedRoot "docs/RELEASE_NOTES_$Version.md") 'RELEASE_NOTES.md'
foreach ($shader in Get-ChildItem -LiteralPath (Join-Path $bin 'shaders') -File -Filter '*.dxil') { Copy-Payload $shader.FullName "shaders/$($shader.Name)" }
Copy-Payload (Join-Path $resolvedRoot 'runtime_local/config/ngx-local.json') 'runtime/config/ngx-local.json'
Copy-Payload 'C:/veyra-deps/installed/x64-windows/share/ffmpeg/copyright' 'licenses/FFMPEG-COPYRIGHT.txt'
Copy-Payload 'C:/veyra-deps/installed/x64-windows/share/ffmpeg/vcpkg.spdx.json' 'licenses/FFMPEG-SPDX.json'
Copy-Payload (Join-Path $resolvedRoot 'assets/icons/lucide/LICENSE') 'licenses/LUCIDE-LICENSE.txt'
Copy-Payload (Join-Path $resolvedRoot 'third_party_local/nvidia/DLSS_repo/LICENSE.txt') 'licenses/NVIDIA_RTX_SDK_LICENSE.txt'
Copy-Payload (Join-Path $resolvedRoot 'third_party_local/nvidia/RTX_Video_SDK_1.1.0/NVIDIA_RTX_Video_SDK_License.pdf') 'licenses/NVIDIA_RTX_VIDEO_SDK_LICENSE.pdf'
Copy-Payload (Join-Path $resolvedRoot 'third_party_local/nvidia/Optical_Flow_SDK_5.0.7/LicenseAgreement.pdf') 'licenses/NVIDIA_OPTICAL_FLOW_SDK_LICENSE.pdf'
Copy-Payload (Join-Path $resolvedRoot 'third_party_local/intel/xess-3.0.2/LICENSE.txt') 'licenses/INTEL_XESS_LICENSE.txt'
Copy-Payload (Join-Path $resolvedRoot 'third_party_local/intel/xess-3.0.2/third-party-programs.txt') 'licenses/INTEL_THIRD_PARTY_PROGRAMS.txt'
Copy-Payload (Join-Path $resolvedRoot 'third_party_local/amd/FidelityFX-SDK/LICENSE.txt') 'licenses/AMD_FIDELITYFX_LICENSE.txt'
foreach ($item in $runtimeFiles) { Copy-Payload (Join-Path $resolvedRoot $item.Source) "$($item.Folder)/$($item.Name)" }
foreach ($folder in @('runtime/experimental','runtime_local/intel/experimental')) {
  $relative = "$folder/release-runtime-manifest.json"
  [ordered]@{schema=1;package="Veyra $Version";mode='user-authorized-runtime-pack';enforcedAtRuntime=$false;warning='Community experimental integration; not vendor certification or endorsement.';files=@($records | Where-Object {$_.path.StartsWith("$folder/")})} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $stage $relative) -Encoding UTF8
  $allowed.Add($relative)
}
$payload = @(Get-ChildItem -LiteralPath $stage -Recurse -File)
$forbidden = '\.(pdb|lib|obj|h|hpp|cpp|c|zip|pth|onnx|log|mp4|partial|addon64)$'
foreach ($file in $payload) {
  $relative = $file.FullName.Substring($stage.Length+1).Replace('\','/')
  if ($relative -notin $allowed -or $relative -match $forbidden -or $relative -match '(^|/)(third_party_local|logs|captures|\.git)/') { throw "Unexpected payload: $relative" }
}
$fileManifest = @($payload | ForEach-Object {[ordered]@{path=$_.FullName.Substring($stage.Length+1).Replace('\','/');size=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}})
[ordered]@{schema=1;version=$Version;files=$fileManifest} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $stage 'package-manifest.json') -Encoding UTF8
Compress-Archive -LiteralPath $stage -DestinationPath $archive -CompressionLevel Optimal
$archiveHash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
"$archiveHash  $([IO.Path]::GetFileName($archive))" | Set-Content -LiteralPath "$archive.sha256" -Encoding ASCII
[ordered]@{archive=$archive;sha256=$archiveHash;files=$payload.Count+1;runtime=$records;forbiddenFiles=0} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $resolvedOutput 'package-audit.json') -Encoding UTF8
Get-Item -LiteralPath $archive | Select-Object FullName,Length
Write-Output "SHA256 $archiveHash"
