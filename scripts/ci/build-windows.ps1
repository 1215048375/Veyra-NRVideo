$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$workspace = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
Set-Location -LiteralPath $workspace
$dependencyRoot = Join-Path $workspace 'third_party_local'
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
    VEYRA_DXC_EXECUTABLE = 'dxc'
}
$paths = @{}
foreach ($key in $mapping.Keys) { $paths[$key] = DependencyPath $config.($mapping[$key]) }
& python scripts/ci/dependencies.py verify-ffmpeg --prefix $paths.VEYRA_FFMPEG_ROOT
if ($LASTEXITCODE -ne 0) { throw 'PS5-patched FFmpeg provenance verification failed.' }
foreach ($file in @(
    (Join-Path $paths.VEYRA_DLSS_SDK_ROOT 'include/nvsdk_ngx.h'),
    (Join-Path $paths.VEYRA_NVOF_SDK_ROOT 'NvOFInterface/nvOpticalFlowCommon.h'),
    (Join-Path $paths.VEYRA_XESS_ROOT 'inc/xess_fg/xefg_swapchain_d3d12.h'),
    (Join-Path $paths.VEYRA_FIDELITYFX_ROOT 'bin/ffx_sdk/ffx_opticalflow_x64.lib'),
    (Join-Path $paths.VEYRA_FIDELITYFX_ROOT 'bin/ffx_sdk/ffx_backend_dx12_x64.lib'),
    (Join-Path $dependencyRoot 'nvidia/nv-codec-headers/include/ffnvcodec/nvEncodeAPI.h')
)) { if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Required full-build dependency is missing: $file" } }

# Import the real x64 toolchain for CMake/Ninja and link.exe.
& "$PSScriptRoot/toolchain.ps1"
$env:PKG_CONFIG_PATH = Join-Path $paths.CMAKE_PREFIX_PATH 'lib/pkgconfig'
$env:PATH = (Join-Path $paths.CMAKE_PREFIX_PATH 'bin') + ';' + $env:PATH
$env:PATH = (Split-Path $paths.PROTOC) + ';' + $env:PATH
$env:NANOPB_PB2_TEMP_DIR = [IO.Path]::GetTempPath()
$lock = Get-Content -LiteralPath scripts/ci/dependencies.lock.json -Raw | ConvertFrom-Json
& python -m pip install --disable-pip-version-check --target out/ci-python "protobuf==$($lock.python_protobuf)"
if ($LASTEXITCODE -ne 0) { throw 'Python protobuf dependency installation failed.' }
$env:PYTHONPATH = (Join-Path $workspace 'out/ci-python') + [IO.Path]::PathSeparator + $env:PYTHONPATH
$arguments = @('--preset', 'x64-release', '-B', 'out/ci-full', '-DVEYRA_ENABLE_EXPERIMENTAL_DLSSNR=ON', '-DVEYRA_ENABLE_REMOTEPLAY=ON')
$arguments += '-DCMAKE_POLICY_VERSION_MINIMUM=3.5'
$arguments += "-Dnanopb_PROTOC_PATH=$($paths.PROTOC)"
foreach ($key in $mapping.Keys) { $arguments += "-D${key}=$($paths[$key])" }
& cmake @arguments
if ($LASTEXITCODE -ne 0) { throw 'Full CMake configuration failed.' }
& cmake --build out/ci-full --target veyra --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Full application build failed.' }

# Share the clean application-only packager with local builds.
& "$PSScriptRoot/../package-build.ps1" -BuildDirectory (Join-Path $workspace 'out/ci-full') -OutputDirectory (Join-Path $workspace 'out/ci-artifact') -ApplicationOnly
