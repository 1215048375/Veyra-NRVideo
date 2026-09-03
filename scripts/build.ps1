[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Root,
    [Parameter(Mandatory = $true)][ValidateSet("x64-debug", "x64-release")][string]$Preset,
    [switch]$Clean
)

# Veyra build wrapper: resolves the MSVC environment on this machine and runs
# the tracked CMake presets. Keeps machine-specific paths out of the presets.
# Writes only under the gitignored out/ directory.

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath (Join-Path $Root "CMakePresets.json") -PathType Leaf)) {
    Write-Host "build.ps1: CMakePresets.json not found under $Root"
    exit 2
}

# --- Resolve Visual Studio installation (vswhere first, known path fallback)
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
$vsRoot = ""
if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
    $vsRoot = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath) -join ""
}
if ([string]::IsNullOrWhiteSpace($vsRoot)) {
    $fallback = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
    if (Test-Path -LiteralPath $fallback -PathType Container) { $vsRoot = $fallback }
}
if ([string]::IsNullOrWhiteSpace($vsRoot)) {
    Write-Host "build.ps1: Visual Studio 2022 VC tools not found"
    exit 3
}

$cmakeExe = Join-Path $vsRoot "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if (-not (Test-Path -LiteralPath $cmakeExe -PathType Leaf)) {
    $found = Get-Command cmake -ErrorAction SilentlyContinue
    if ($null -ne $found) { $cmakeExe = $found.Source }
}
if ([string]::IsNullOrWhiteSpace($cmakeExe)) {
    Write-Host "build.ps1: cmake not found"
    exit 3
}

$vcvars = Join-Path $vsRoot "VC\Auxiliary\Build\vcvars64.bat"
if (-not (Test-Path -LiteralPath $vcvars -PathType Leaf)) {
    Write-Host "build.ps1: vcvars64.bat not found under $vsRoot"
    exit 3
}

$buildDir = Join-Path $Root ("out\build\" + $Preset)
if ($Clean -and (Test-Path -LiteralPath $buildDir -PathType Container)) {
    Remove-Item -LiteralPath $buildDir -Recurse -Force
}

# Local experimental DLSSNR support: enable when the staged official SDK tree
# exists; the CMake configure itself fail-closes on missing paths.
$configureExtra = ""
$sdkRoot = Join-Path $Root "third_party_local\nvidia\DLSS_SDK_310.7.0"
if (Test-Path -LiteralPath (Join-Path $sdkRoot "include\nvsdk_ngx.h") -PathType Leaf) {
    $configureExtra = ' -DVEYRA_ENABLE_EXPERIMENTAL_DLSSNR=ON -DVEYRA_DLSS_SDK_ROOT="{0}"' -f $sdkRoot
}
else {
    $configureExtra = " -DVEYRA_ENABLE_EXPERIMENTAL_DLSSNR=OFF"
}

# FFmpeg dependency roots (C:\veyra-deps: the project path contains spaces,
# which FFmpeg's build refuses; see loop/JOURNAL.md).
$ffmpegRoot = "C:\veyra-deps\installed\x64-windows"
if (Test-Path -LiteralPath (Join-Path $ffmpegRoot "include\libavformat\avformat.h") -PathType Leaf) {
    $configureExtra = $configureExtra + (' -DVEYRA_FFMPEG_ROOT="{0}"' -f $ffmpegRoot)
}
$clipToolsRoot = "C:\veyra-deps\tools-installed\x64-windows"
if (Test-Path -LiteralPath (Join-Path $clipToolsRoot "include\libavcodec\avcodec.h") -PathType Leaf) {
    $configureExtra = $configureExtra + (' -DVEYRA_CLIP_TOOLS_ROOT="{0}"' -f $clipToolsRoot)
}

# --- Configure + build inside one vcvars environment, from a temp batch file
$batchDir = Join-Path $Root "out\build"
New-Item -ItemType Directory -Force -Path $batchDir | Out-Null
$batchFile = Join-Path $batchDir ("veyra-build-" + $Preset + ".cmd")

$batchLines = @(
    '@echo off',
    ('call "{0}" >nul 2>&1' -f $vcvars),
    ('if errorlevel 1 exit /b 4' -f $null),
    ('cd /d "{0}"' -f $Root),
    ('"{0}" --preset {1}{2}' -f $cmakeExe, $Preset, $configureExtra),
    'if errorlevel 1 exit /b 5',
    ('"{0}" --build --preset {1}' -f $cmakeExe, $Preset),
    'if errorlevel 1 exit /b 6',
    'exit /b 0'
)
Set-Content -LiteralPath $batchFile -Value $batchLines -Encoding ASCII

& cmd.exe /c ('"{0}"' -f $batchFile)
$exitCode = $LASTEXITCODE
Write-Host ("build.ps1: preset {0} exitCode={1}" -f $Preset, $exitCode)
exit $exitCode
