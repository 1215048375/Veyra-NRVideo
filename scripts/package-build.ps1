param(
    [string]$BuildDirectory = 'out/ci-full',
    [string]$OutputDirectory,
    [switch]$ApplicationOnly
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$bin = (Resolve-Path -LiteralPath $BuildDirectory).Path
$exe = Join-Path $bin 'veyra.exe'
$version = [Diagnostics.FileVersionInfo]::GetVersionInfo($exe).ProductVersion
if (!$OutputDirectory) { $OutputDirectory = Join-Path $root "out/packages/Veyra-$version-local-$(Get-Date -Format yyyyMMdd-HHmmss)" }
$stage = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $stage) { throw 'Output exists; choose a new directory. Existing builds are never overwritten.' }
$cache = Get-Content -LiteralPath (Join-Path $bin 'CMakeCache.txt') -Raw
$payload = [ordered]@{}
function Add-Payload([string]$Source, [string]$Relative) {
    if (!(Test-Path -LiteralPath $Source -PathType Leaf)) { throw "Missing build input: $Source" }
    $payload[$Relative] = $Source
}
Add-Payload $exe 'veyra.exe'
$shaderRoot = Join-Path $bin 'shaders'
$shaders = @(Get-ChildItem -LiteralPath $shaderRoot -Recurse -File -Filter '*.dxil')
if (!$shaders.Count) { throw 'Compiled shaders are missing.' }
foreach ($file in $shaders) { Add-Payload $file.FullName ('shaders/' + [IO.Path]::GetRelativePath($shaderRoot,$file.FullName).Replace('\','/')) }
foreach ($name in @('LICENSE','THIRD_PARTY_NOTICES.md')) { Add-Payload (Join-Path $root $name) $name }
foreach ($file in Get-ChildItem -LiteralPath (Join-Path $root 'licenses') -Recurse -File) {
    if ($file.Extension -notin @('', '.txt', '.json')) { throw "Unexpected notice: $($file.Name)" }
    Add-Payload $file.FullName ('licenses/' + [IO.Path]::GetRelativePath((Join-Path $root 'licenses'),$file.FullName).Replace('\','/'))
}
if (!$ApplicationOnly) {
    if ($cache -notmatch '(?m)^VEYRA_FFMPEG_ROOT:PATH=(.+)$') { throw 'FFmpeg prefix missing from CMake cache.' }
    $ffmpeg = $Matches[1].Trim()
    & python (Join-Path $root 'scripts/ci/dependencies.py') verify-ffmpeg --prefix $ffmpeg
    if ($LASTEXITCODE -ne 0) { throw 'Patched FFmpeg verification failed.' }
    foreach ($name in @('avcodec-63.dll','avformat-63.dll','avutil-61.dll','swresample-7.dll','swscale-10.dll')) {
        $source = Join-Path $bin $name
        if ((Get-FileHash -LiteralPath $source).Hash -ne (Get-FileHash -LiteralPath (Join-Path $ffmpeg "bin/$name")).Hash) { throw "Build DLL differs from verified FFmpeg prefix: $name" }
        Add-Payload $source $name
    }
    foreach ($name in @('copyright','vcpkg.spdx.json','veyra-local-build.json')) { Add-Payload (Join-Path $ffmpeg "share/ffmpeg/$name") "licenses/ffmpeg/$name" }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $vs = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath) -join ''
    $crt = Get-ChildItem -LiteralPath (Join-Path $vs 'VC/Redist/MSVC') -Directory | Where-Object Name -Match '^\d+\.' | Sort-Object {[version]$_.Name} -Descending | Select-Object -First 1
    foreach ($name in @('vcruntime140.dll','vcruntime140_1.dll','msvcp140.dll')) { Add-Payload (Join-Path $crt.FullName "x64/Microsoft.VC143.CRT/$name") $name }
    foreach ($name in @('LICENSE','NOTICE','PROVENANCE.md','VEYRA_INTEGRATION.md')) { Add-Payload (Join-Path $root "third_party/gpu-dis/$name") "licenses/gpu-dis/$name" }
    foreach ($file in Get-ChildItem -LiteralPath (Join-Path $root 'third_party/gpu-dis/licenses') -File) { Add-Payload $file.FullName "licenses/gpu-dis/licenses/$($file.Name)" }
}
# Copy only the planned files; never copy the build tree, runtime, SDK or settings.
foreach ($relative in $payload.Keys) {
    $target = Join-Path $stage $relative
    [IO.Directory]::CreateDirectory((Split-Path $target)) | Out-Null
    Copy-Item -LiteralPath $payload[$relative] -Destination $target
}
$commit = (& git -C $root rev-parse HEAD) -join ''
$dirty = [bool]((& git -C $root status --porcelain) -join '')
@{
    version = $version; commit = $commit; sourceDirty = $dirty
    packagedUtc = [DateTime]::UtcNow.ToString('o')
    dependencyLockSha256 = (Get-FileHash -LiteralPath (Join-Path $root 'scripts/ci/dependencies.lock.json')).Hash
    remotePlay = ($cache -match 'VEYRA_ENABLE_REMOTEPLAY:BOOL=ON')
    gpuTestsExecuted = $false; portableRuntimePack = $false
    playbackDependenciesIncluded = !$ApplicationOnly
    executableSha256 = (Get-FileHash -LiteralPath (Join-Path $stage 'veyra.exe')).Hash
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $stage 'build-info.json') -Encoding utf8
$message = if ($ApplicationOnly) {
    'Application/shaders only, matching the GitHub Actions artifact. Requires matching playback dependencies and an approved runtime pack.'
} else {
    'Local development build. Run veyra.exe. Basic playback dependencies included. NVIDIA/Intel enhancement runtimes are not included; this is not a complete Release Runtime Pack.'
}
$message + "`nPackaging does not perform GPU enhancement tests. Version/commit/dirty state are in build-info.json. No SDKs, private settings or test media included." | Set-Content -LiteralPath (Join-Path $stage 'BUILD-README.txt') -Encoding utf8
$files = @(Get-ChildItem -LiteralPath $stage -Recurse -File | ForEach-Object {
    $relative = [IO.Path]::GetRelativePath($stage,$_.FullName).Replace('\','/')
    if (!$payload.Contains($relative) -and $relative -notin @('build-info.json','BUILD-README.txt')) { throw "Unexpected payload: $relative" }
    @{path=$relative;size=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName).Hash}
})
@{schema=1;files=$files} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $stage 'package-manifest.json') -Encoding utf8
Write-Host "Clean build: $stage"
