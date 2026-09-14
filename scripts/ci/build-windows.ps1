$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if (-not $env:GITHUB_WORKSPACE) { throw 'GITHUB_WORKSPACE is required.' }
Set-Location -LiteralPath $env:GITHUB_WORKSPACE
$dependencyRoot = Join-Path $env:GITHUB_WORKSPACE 'third_party_local'
$config = Get-Content -LiteralPath (Join-Path $dependencyRoot 'ci-build.json') -Raw | ConvertFrom-Json
if ($config.schema -ne 1) { throw 'Unsupported CI dependency manifest schema.' }
function DependencyPath([string]$Relative) {
    if (-not $Relative -or [IO.Path]::IsPathRooted($Relative)) { throw 'Dependency paths must be relative.' }
    $path = [IO.Path]::GetFullPath((Join-Path $dependencyRoot $Relative))
    $prefix = [IO.Path]::GetFullPath($dependencyRoot) + [IO.Path]::DirectorySeparatorChar
    if (-not $path.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase) -or -not (Test-Path -LiteralPath $path)) {
        throw "Dependency path is missing or outside third_party_local: $Relative"
    }
    return $path.Replace('\', '/')
}
$mapping = [ordered]@{
    VEYRA_DLSS_SDK_ROOT = 'dlss'
    VEYRA_NVOF_SDK_ROOT = 'nvof'
    VEYRA_FFMPEG_ROOT = 'ffmpeg'
    VEYRA_XESS_ROOT = 'xess'
    VEYRA_FIDELITYFX_ROOT = 'fidelityfx'
    VEYRA_RP_CHIAKI_SOURCE_DIR = 'chiakiStage'
    VEYRA_RP_CHIAKI_VERIFY_DIR = 'chiakiClean'
    CMAKE_PREFIX_PATH = 'remotePlayPrefix'
    PROTOC = 'protoc'
    PKG_CONFIG_EXECUTABLE = 'pkgConfig'
}
$paths = @{}
foreach ($key in $mapping.Keys) { $paths[$key] = DependencyPath $config.($mapping[$key]) }
# Require the tested FFmpeg binary paired with its development libraries. Never
# silently replace the PS5 slice repair with a stock vcpkg FFmpeg build.
$avcodec = Join-Path $paths.VEYRA_FFMPEG_ROOT 'bin/avcodec-63.dll'
if ((Get-FileHash -LiteralPath $avcodec -Algorithm SHA256).Hash -ne '0710F0D87A7FFCC9F998F1A35D0500345F6C66EB9A5A39C51D60D293142BD84F') {
    throw 'The dependency bundle must contain the approved PS5-patched FFmpeg build.'
}
foreach ($file in @(
    (Join-Path $paths.VEYRA_DLSS_SDK_ROOT 'include/nvsdk_ngx.h'),
    (Join-Path $paths.VEYRA_NVOF_SDK_ROOT 'NvOFInterface/nvOpticalFlowCommon.h'),
    (Join-Path $paths.VEYRA_XESS_ROOT 'inc/xess_fg/xefg_swapchain_d3d12.h'),
    (Join-Path $paths.VEYRA_FIDELITYFX_ROOT 'bin/ffx_sdk/ffx_opticalflow_x64.lib'),
    (Join-Path $paths.VEYRA_FIDELITYFX_ROOT 'bin/ffx_sdk/ffx_backend_dx12_x64.lib'),
    (Join-Path $dependencyRoot 'nvidia/nv-codec-headers/include/ffnvcodec/nvEncodeAPI.h')
)) { if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Required full-build dependency is missing: $file" } }

# Import the real x64 toolchain for CMake/Ninja and link.exe.
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vsRoot = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath) -join ''
if (-not $vsRoot) { throw 'MSVC x64 tools are missing.' }
$devShell = Join-Path $vsRoot 'Common7/Tools/Launch-VsDevShell.ps1'
& $devShell -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
if (-not (Get-Command cmake -ErrorAction SilentlyContinue) -or -not (Get-Command ninja -ErrorAction SilentlyContinue)) { throw 'CMake and Ninja are required.' }
$env:PKG_CONFIG_PATH = Join-Path $paths.CMAKE_PREFIX_PATH 'lib/pkgconfig'
$env:PATH = (Join-Path $paths.CMAKE_PREFIX_PATH 'bin') + ';' + $env:PATH
$arguments = @('--preset', 'x64-release', '-B', 'out/ci-full', '-DVEYRA_ENABLE_EXPERIMENTAL_DLSSNR=ON', '-DVEYRA_ENABLE_REMOTEPLAY=ON')
foreach ($key in $mapping.Keys) { $arguments += "-D${key}=$($paths[$key])" }
& cmake @arguments
if ($LASTEXITCODE -ne 0) { throw 'Full CMake configuration failed.' }
& cmake --build out/ci-full --target veyra --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Full application build failed.' }

# Explicit allowlist: never upload out/ci-full, dependencies, SDKs or runtime DLLs.
$artifact = Join-Path $env:GITHUB_WORKSPACE 'out/ci-artifact'
if (Test-Path -LiteralPath $artifact) { throw 'Artifact directory must be new.' }
New-Item -ItemType Directory -Path $artifact | Out-Null
Copy-Item -LiteralPath 'out/ci-full/veyra.exe' -Destination $artifact
$shaderRoot = (Resolve-Path -LiteralPath 'out/ci-full/shaders').Path
$shaders = @(Get-ChildItem -LiteralPath $shaderRoot -Recurse -File -Filter '*.dxil')
if (-not $shaders.Count) { throw 'Compiled shaders are missing.' }
foreach ($shader in $shaders) {
    $relative = [IO.Path]::GetRelativePath($shaderRoot, $shader.FullName)
    $target = Join-Path $artifact "shaders/$relative"
    New-Item -ItemType Directory -Force -Path (Split-Path $target) | Out-Null
    Copy-Item -LiteralPath $shader.FullName -Destination $target
}
Copy-Item -LiteralPath 'LICENSE', 'THIRD_PARTY_NOTICES.md' -Destination $artifact
Copy-Item -LiteralPath 'licenses' -Destination $artifact -Recurse
@{
    commit = $env:GITHUB_SHA
    dependencySha256 = (Get-FileHash -LiteralPath (Join-Path $env:RUNNER_TEMP 'veyra-ci-deps.zip')).Hash
    remotePlay = $true
    gpuTestsExecuted = $false
    portableRuntimePack = $false
    executableSha256 = (Get-FileHash -LiteralPath (Join-Path $artifact 'veyra.exe')).Hash
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $artifact 'build-info.json') -Encoding utf8
'Application build only. Requires the matching approved runtime pack; this is not a standalone portable release. GPU playback has not been tested. Source commit is recorded in build-info.json.' | Set-Content -LiteralPath (Join-Path $artifact 'BUILD-README.txt') -Encoding utf8
