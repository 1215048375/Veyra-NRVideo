[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$Root)

# Veyra Phase 0 gate: runtime probe + D3D12 skeleton.
# Encodes Playbook section 16 Phase 0 acceptance:
#   - Debug and Release both configure/build through the tracked wrapper;
#   - veyra_runtime_probe runs in both configurations from THIS run-id;
#   - staged nvngx_dlssnr.dll identity (size/SHA256/Authenticode/signer) matches
#     both the pinned constants and runtime-manifest.json;
#   - all five required D3D12 exports are reported present;
#   - NVIDIA adapter (vendor 0x10DE), feature level >= 12_0, nvofapi64 present;
#   - Release probe holds a >= 300 s D3D12 window loop with no device removal;
#   - probe self-reported exe SHA256 matches the exe actually invoked;
#   - proprietary paths still ignored by Git.
# Fail-closed: any missing command, file, JSON field, or threshold breach fails.
# The gate writes only under gitignored logs/ and out/ directories.

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$failures = New-Object System.Collections.Generic.List[string]
$checkCount = 0

function Add-GateCheck {
    param([string]$Name, [bool]$Passed, [string]$Detail)
    $script:checkCount = $script:checkCount + 1
    $tag = "FAIL"
    if ($Passed) { $tag = "PASS" }
    Write-Host ("[{0}] {1} :: {2}" -f $tag, $Name, $Detail)
    if (-not $Passed) {
        $script:failures.Add($Name) | Out-Null
    }
}

$runId = [guid]::NewGuid().ToString("N")
$gateStart = Get-Date
Write-Host ("Phase 0 gate run-id: {0}" -f $runId)
Write-Host ("Root: {0}" -f $Root)

# ---------------------------------------------------------------------------
# 1. Required project inputs (tracked sources must exist before anything runs)
# ---------------------------------------------------------------------------
$requiredInputs = @(
    "CMakeLists.txt",
    "CMakePresets.json",
    "scripts\build.ps1",
    "scripts\stage-runtime.ps1",
    "tools\runtime_probe\main.cpp"
)
foreach ($relative in $requiredInputs) {
    $path = Join-Path $Root $relative
    $exists = Test-Path -LiteralPath $path -PathType Leaf
    Add-GateCheck ("input:{0}" -f $relative) $exists $("present" )
    if (-not $exists) {
        Write-Host ("Phase 0 gate cannot proceed: {0} is missing." -f $relative)
    }
}
if ($failures.Count -gt 0) {
    Write-Host ("VEYRA PHASE 0 GATE FAILED: {0} of {1} checks failed." -f $failures.Count, $checkCount)
    Write-Host ("Failed checks: {0}" -f ($failures -join ", "))
    exit 1
}

# ---------------------------------------------------------------------------
# 2. Toolchain presence (absolute, from Playbook section 1.4 layout or PATH)
# ---------------------------------------------------------------------------
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
$toolchainOk = $false
$vsRoot = ""
if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
    $vsRoot = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath) -join ""
}
if ([string]::IsNullOrWhiteSpace($vsRoot)) {
    $fallback = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
    if (Test-Path -LiteralPath $fallback -PathType Container) { $vsRoot = $fallback }
}
$cmakeExe = ""
if (-not [string]::IsNullOrWhiteSpace($vsRoot)) {
    $candidate = Join-Path $vsRoot "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    if (Test-Path -LiteralPath $candidate -PathType Leaf) { $cmakeExe = $candidate }
}
if ([string]::IsNullOrWhiteSpace($cmakeExe)) {
    $found = Get-Command cmake -ErrorAction SilentlyContinue
    if ($null -ne $found) { $cmakeExe = $found.Source }
}
Add-GateCheck "toolchain:cmake" (-not [string]::IsNullOrWhiteSpace($cmakeExe)) $("path={0}" -f $cmakeExe)

$ninjaExe = ""
if (-not [string]::IsNullOrWhiteSpace($vsRoot)) {
    $candidate = Join-Path $vsRoot "Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
    if (Test-Path -LiteralPath $candidate -PathType Leaf) { $ninjaExe = $candidate }
}
if ([string]::IsNullOrWhiteSpace($ninjaExe)) {
    $found = Get-Command ninja -ErrorAction SilentlyContinue
    if ($null -ne $found) { $ninjaExe = $found.Source }
}
Add-GateCheck "toolchain:ninja" (-not [string]::IsNullOrWhiteSpace($ninjaExe)) $("path={0}" -f $ninjaExe)

$clExe = ""
if (-not [string]::IsNullOrWhiteSpace($vsRoot)) {
    $msvcRoot = Join-Path $vsRoot "VC\Tools\MSVC"
    if (Test-Path -LiteralPath $msvcRoot -PathType Container) {
        $clCandidates = @(Get-ChildItem -LiteralPath $msvcRoot -Directory | Sort-Object Name -Descending)
        foreach ($dir in $clCandidates) {
            $candidate = Join-Path $dir.FullName "bin\Hostx64\x64\cl.exe"
            if (Test-Path -LiteralPath $candidate -PathType Leaf) { $clExe = $candidate; break }
        }
    }
}
Add-GateCheck "toolchain:cl-x64" (-not [string]::IsNullOrWhiteSpace($clExe)) $("path={0}" -f $clExe)

if ($failures.Count -gt 0) {
    Write-Host ("VEYRA PHASE 0 GATE FAILED: {0} of {1} checks failed." -f $failures.Count, $checkCount)
    Write-Host ("Failed checks: {0}" -f ($failures -join ", "))
    exit 1
}

# ---------------------------------------------------------------------------
# 3. Staged runtime + manifest consistency (Playbook sections 1.1 and 4.3)
# ---------------------------------------------------------------------------
$expectedDllSize = [long]165840496
$expectedDllSha256 = "E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E"
$expectedFileVersion = "310.8.0.0"
$requiredExports = @(
    "NVSDK_NGX_D3D12_Init_Ext",
    "NVSDK_NGX_D3D12_CreateFeature",
    "NVSDK_NGX_D3D12_EvaluateFeature",
    "NVSDK_NGX_D3D12_ReleaseFeature",
    "NVSDK_NGX_D3D12_Shutdown1"
)

$runtimeDir = Join-Path $Root "runtime_local\nvidia"
$stagedDll = Join-Path $runtimeDir "nvngx_dlssnr.dll"
$manifestPath = Join-Path $runtimeDir "runtime-manifest.json"

$dllStaged = Test-Path -LiteralPath $stagedDll -PathType Leaf
Add-GateCheck "runtime:staged-dll" $dllStaged $("path={0}" -f $stagedDll)
$manifestExists = Test-Path -LiteralPath $manifestPath -PathType Leaf
Add-GateCheck "runtime:manifest" $manifestExists $("path={0}" -f $manifestPath)

if ($dllStaged) {
    $item = Get-Item -LiteralPath $stagedDll
    Add-GateCheck "runtime:size" ($item.Length -eq $expectedDllSize) $("actual={0} expected={1}" -f $item.Length, $expectedDllSize)
    $dllHash = (Get-FileHash -LiteralPath $stagedDll -Algorithm SHA256).Hash.ToUpperInvariant()
    Add-GateCheck "runtime:sha256" ($dllHash -eq $expectedDllSha256) $("actual={0}" -f $dllHash)
    $sig = Get-AuthenticodeSignature -LiteralPath $stagedDll
    Add-GateCheck "runtime:signature" ([string]$sig.Status -eq "Valid") $("actual={0}" -f [string]$sig.Status)
    $signer = ""
    if ($null -ne $sig.SignerCertificate) { $signer = [string]$sig.SignerCertificate.Subject }
    Add-GateCheck "runtime:signer" ($signer -like "*NVIDIA*") $("subject={0}" -f $signer)
    $fileVersion = [string]$item.VersionInfo.FileVersion
    Add-GateCheck "runtime:fileversion" ($fileVersion -eq $expectedFileVersion) $("actual={0} expected={1}" -f $fileVersion, $expectedFileVersion)
}

if ($manifestExists) {
    try {
        $manifest = Get-Content -Raw -Encoding UTF8 -LiteralPath $manifestPath | ConvertFrom-Json
        $entry = $null
        foreach ($candidate in @($manifest.files)) {
            if ([string]$candidate.name -eq "nvngx_dlssnr.dll") { $entry = $candidate }
        }
        $manifestSchemaOk = ($manifest.schema -eq 1) -and ($manifest.mode -eq "local-experimental-only") -and ($null -ne $entry)
        Add-GateCheck "runtime:manifest-schema" $manifestSchemaOk $("schema={0} mode={1} entry={2}" -f $manifest.schema, $manifest.mode, $(if ($null -ne $entry) { "found" } else { "missing" })
        )
        if ($null -ne $entry) {
            Add-GateCheck "runtime:manifest-size" ([long]$entry.size -eq $expectedDllSize) $("manifest={0}" -f $entry.size)
            Add-GateCheck "runtime:manifest-sha256" ([string]$entry.sha256 -ceq $expectedDllSha256) $("manifest={0}" -f $entry.sha256)
            Add-GateCheck "runtime:manifest-redistribution" ([bool]$entry.redistributable -eq $false) $("redistributable={0}" -f $entry.redistributable)
        }
    }
    catch {
        Add-GateCheck "runtime:manifest-parse" $false $_.Exception.Message
    }
}

if ($failures.Count -gt 0) {
    Write-Host ("VEYRA PHASE 0 GATE FAILED: {0} of {1} checks failed." -f $failures.Count, $checkCount)
    Write-Host ("Failed checks: {0}" -f ($failures -join ", "))
    exit 1
}

# ---------------------------------------------------------------------------
# 4. Build both configurations through the tracked wrapper
# ---------------------------------------------------------------------------
$buildScript = Join-Path $Root "scripts\build.ps1"
$shell = (Get-Process -Id $PID).Path

& $shell -NoProfile -ExecutionPolicy Bypass -File $buildScript -Root $Root -Preset x64-debug -Clean:$false
$debugBuildExit = $LASTEXITCODE
Add-GateCheck "build:x64-debug" ($debugBuildExit -eq 0) ("exitCode={0}" -f $debugBuildExit)

& $shell -NoProfile -ExecutionPolicy Bypass -File $buildScript -Root $Root -Preset x64-release -Clean:$false
$releaseBuildExit = $LASTEXITCODE
Add-GateCheck "build:x64-release" ($releaseBuildExit -eq 0) ("exitCode={0}" -f $releaseBuildExit)

if ($failures.Count -gt 0) {
    Write-Host ("VEYRA PHASE 0 GATE FAILED: {0} of {1} checks failed." -f $failures.Count, $checkCount)
    Write-Host ("Failed checks: {0}" -f ($failures -join ", "))
    exit 1
}

# ---------------------------------------------------------------------------
# 5. Probe executables exist; record the exact binaries under test
# ---------------------------------------------------------------------------
$probeDebugExe = Join-Path $Root "out\build\x64-debug\veyra_runtime_probe.exe"
$probeReleaseExe = Join-Path $Root "out\build\x64-release\veyra_runtime_probe.exe"
$debugExeExists = Test-Path -LiteralPath $probeDebugExe -PathType Leaf
$releaseExeExists = Test-Path -LiteralPath $probeReleaseExe -PathType Leaf
Add-GateCheck "probe:exe-debug" $debugExeExists $("path={0}" -f $probeDebugExe)
Add-GateCheck "probe:exe-release" $releaseExeExists $("path={0}" -f $probeReleaseExe)
if (-not ($debugExeExists -and $releaseExeExists)) {
    Write-Host ("VEYRA PHASE 0 GATE FAILED: {0} of {1} checks failed." -f $failures.Count, $checkCount)
    Write-Host ("Failed checks: {0}" -f ($failures -join ", "))
    exit 1
}
$debugExeHash = (Get-FileHash -LiteralPath $probeDebugExe -Algorithm SHA256).Hash.ToUpperInvariant()
$releaseExeHash = (Get-FileHash -LiteralPath $probeReleaseExe -Algorithm SHA256).Hash.ToUpperInvariant()
Write-Host ("probe exe sha256 (debug)  : {0}" -f $debugExeHash)
Write-Host ("probe exe sha256 (release): {0}" -f $releaseExeHash)

# ---------------------------------------------------------------------------
# 6. Run probes with a fresh run-id; logs go to gitignored logs/phase0/<runId>
# ---------------------------------------------------------------------------
$logDir = Join-Path $Root ("logs\phase0\" + $runId)
New-Item -ItemType Directory -Force -Path $logDir | Out-Null

$debugLog = Join-Path $logDir "probe-debug.log"
$debugJson = Join-Path $logDir "probe-debug.json"
$debugLogFound = $false
try {
    & $probeDebugExe --runtime-dir $runtimeDir --window-seconds 3 --run-id $runId --log-file $debugLog --json-file $debugJson
    $debugProbeExit = $LASTEXITCODE
}
catch {
    $debugProbeExit = -1
}
Add-GateCheck "probe:run-debug" ($debugProbeExit -eq 0) ("exitCode={0}" -f $debugProbeExit)
if (Test-Path -LiteralPath $debugJson -PathType Leaf) { $debugLogFound = $true }
Add-GateCheck "probe:json-debug-exists" $debugLogFound $("path={0}" -f $debugJson)

$releaseLog = Join-Path $logDir "probe-release.log"
$releaseJson = Join-Path $logDir "probe-release.json"
$releaseLogFound = $false
try {
    & $probeReleaseExe --runtime-dir $runtimeDir --window-seconds 300 --run-id $runId --log-file $releaseLog --json-file $releaseJson
    $releaseProbeExit = $LASTEXITCODE
}
catch {
    $releaseProbeExit = -1
}
Add-GateCheck "probe:run-release" ($releaseProbeExit -eq 0) ("exitCode={0}" -f $releaseProbeExit)
if (Test-Path -LiteralPath $releaseJson -PathType Leaf) { $releaseLogFound = $true }
Add-GateCheck "probe:json-release-exists" $releaseLogFound $("path={0}" -f $releaseJson)

if ($failures.Count -gt 0) {
    Write-Host ("VEYRA PHASE 0 GATE FAILED: {0} of {1} checks failed." -f $failures.Count, $checkCount)
    Write-Host ("Failed checks: {0}" -f ($failures -join ", "))
    Write-Host ("Log directory: {0}" -f $logDir)
    exit 1
}

# ---------------------------------------------------------------------------
# 7. Parse probe JSON summaries and enforce thresholds
# ---------------------------------------------------------------------------
function Test-ProbeJson {
    param(
        [string]$Label,
        [string]$JsonPath,
        [string]$ExpectedRunId,
        [string]$ExpectedExeSha256,
        [int]$MinWindowSeconds
    )

    $summary = $null
    try {
        $summary = Get-Content -Raw -Encoding UTF8 -LiteralPath $JsonPath | ConvertFrom-Json
    }
    catch {
        Add-GateCheck ("json:{0}:parse" -f $Label) $false $_.Exception.Message
        return
    }
    Add-GateCheck ("json:{0}:probe-name" -f $Label) ([string]$summary.probe -eq "veyra_runtime_probe") ("actual={0}" -f $summary.probe)
    Add-GateCheck ("json:{0}:run-id" -f $Label) ([string]$summary.runId -eq $ExpectedRunId) ("actual={0} expected={1}" -f $summary.runId, $ExpectedRunId)
    Add-GateCheck ("json:{0}:exe-sha256" -f $Label) ([string]$summary.exeSha256 -ceq $ExpectedExeSha256) ("actual={0}" -f $summary.exeSha256)

    Add-GateCheck ("json:{0}:os-build" -f $Label) (-not [string]::IsNullOrWhiteSpace([string]$summary.osBuild)) ("osBuild={0}" -f $summary.osBuild)

    $adapter = $summary.adapter
    $adapterOk = ($null -ne $adapter) -and
        ([bool]$adapter.isNvidia) -and
        ([string]$adapter.vendorId -eq "0x10DE") -and
        (-not [string]::IsNullOrWhiteSpace([string]$adapter.name)) -and
        (-not [string]::IsNullOrWhiteSpace([string]$adapter.luid))
    Add-GateCheck ("json:{0}:adapter-nvidia" -f $Label) $adapterOk ("name={0} vendorId={1} luid={2} vramMiB={3}" -f $adapter.name, $adapter.vendorId, $adapter.luid, $adapter.dedicatedVideoMemoryMiB)

    Add-GateCheck ("json:{0}:driver-version" -f $Label) (-not [string]::IsNullOrWhiteSpace([string]$summary.driverVersion)) ("driver={0}" -f $summary.driverVersion)

    $featureLevelOk = $false
    $levelMajor = 0
    $levelMinor = 0
    $fl = [string]$summary.featureLevel
    if ($fl -match '^(\d+)_(\d+)$') {
        $levelMajor = [int]$Matches[1]
        $levelMinor = [int]$Matches[2]
    }
    if (($levelMajor -gt 12) -or (($levelMajor -eq 12) -and ($levelMinor -ge 0))) { $featureLevelOk = $true }
    Add-GateCheck ("json:{0}:feature-level>=12_0" -f $Label) $featureLevelOk ("actual={0}" -f $fl)

    Add-GateCheck ("json:{0}:command-slots==4" -f $Label) ([int]$summary.commandSlots -eq 4) ("actual={0}" -f $summary.commandSlots)

    $runtime = $summary.runtime
    $runtimeOk = ($null -ne $runtime) -and
        ([long]$runtime.sizeBytes -eq $expectedDllSize) -and
        ([string]$runtime.sha256 -ceq $expectedDllSha256) -and
        ([string]$runtime.signature -eq "Valid") -and
        ([string]$runtime.signer -like "*NVIDIA*") -and
        ([string]$runtime.fileVersion -eq $expectedFileVersion)
    Add-GateCheck ("json:{0}:runtime-identity" -f $Label) $runtimeOk ("size={0} sha256={1} signature={2} fileVersion={3}" -f $runtime.sizeBytes, $runtime.sha256, $runtime.signature, $runtime.fileVersion)

    $exportsOk = $true
    $missingExports = New-Object System.Collections.Generic.List[string]
    if ($null -ne $summary.exports) {
        foreach ($exportName in $requiredExports) {
            $present = [bool]($summary.exports.PSObject.Properties[$exportName].Value)
            if (-not $present) { $missingExports.Add($exportName) | Out-Null; $exportsOk = $false }
        }
    }
    else { $exportsOk = $false }
    Add-GateCheck ("json:{0}:required-exports" -f $Label) $exportsOk $(if ($missingExports.Count -eq 0) { "5/5 present" } else { "missing={0}" -f ($missingExports -join ",") })

    $nvof = $summary.nvofapi
    $nvofOk = ($null -ne $nvof) -and ([bool]$nvof.present) -and (-not [string]::IsNullOrWhiteSpace([string]$nvof.fileVersion))
    Add-GateCheck ("json:{0}:nvofapi-system32" -f $Label) $nvofOk ("path={0} version={1}" -f $nvof.path, $nvof.fileVersion)

    Add-GateCheck ("json:{0}:experimental-flag" -f $Label) ([bool]$summary.experimentalDlssnr -eq $true) ("experimentalDlssnr={0}" -f $summary.experimentalDlssnr)

    $window = $summary.windowLoop
    $windowOk = ($null -ne $window) -and
        ([int]$window.durationSeconds -ge $MinWindowSeconds) -and
        ([long]$window.framesPresented -gt 0) -and
        ([bool]$window.deviceRemoved -eq $false)
    Add-GateCheck ("json:{0}:window-loop" -f $Label) $windowOk ("durationSeconds={0} (>={1}) framesPresented={2} deviceRemoved={3} reason=0x{4:X8}" -f $window.durationSeconds, $MinWindowSeconds, $window.framesPresented, $window.deviceRemoved, $window.deviceRemovedReason)
}

Test-ProbeJson -Label "debug" -JsonPath $debugJson -ExpectedRunId $runId -ExpectedExeSha256 $debugExeHash -MinWindowSeconds 2
Test-ProbeJson -Label "release" -JsonPath $releaseJson -ExpectedRunId $runId -ExpectedExeSha256 $releaseExeHash -MinWindowSeconds 299

# ---------------------------------------------------------------------------
# 8. Proprietary paths remain ignored
# ---------------------------------------------------------------------------
$ignoreTargets = @(
    "nvngx_dlssnr.dll",
    "renodx-dlss5-1.addon64",
    "runtime_local",
    "third_party_local",
    "reference_local",
    "captures",
    "logs"
)
foreach ($target in $ignoreTargets) {
    & git -C $Root check-ignore -q --no-index -- $target
    $ignoreExit = $LASTEXITCODE
    Add-GateCheck ("git-ignore:{0}" -f $target) ($ignoreExit -eq 0) ("git check-ignore exitCode={0}" -f $ignoreExit)
}

$trackedSensitive = @(& git -C $Root ls-files -- "nvngx_dlssnr.dll" "renodx-dlss5-1.addon64" "runtime_local" "third_party_local" "reference_local" "captures" "logs")
Add-GateCheck "git-sensitive-untracked" ($trackedSensitive.Count -eq 0) $(if ($trackedSensitive.Count -eq 0) { "none tracked" } else { $trackedSensitive -join ", " })

# ---------------------------------------------------------------------------
# Result
# ---------------------------------------------------------------------------
$elapsed = ((Get-Date) - $gateStart).TotalSeconds
Write-Host ("Phase 0 gate run-id {0} finished in {1:N1} s; logs: {2}" -f $runId, $elapsed, $logDir)

if ($failures.Count -gt 0) {
    Write-Host ("VEYRA PHASE 0 GATE FAILED: {0} of {1} checks failed." -f $failures.Count, $checkCount)
    Write-Host ("Failed checks: {0}" -f ($failures -join ", "))
    exit 1
}

Write-Host ("VEYRA PHASE 0 GATE PASSED: {0} checks (run-id {1})." -f $checkCount, $runId)
exit 0
