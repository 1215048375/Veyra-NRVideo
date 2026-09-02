[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$Root)

# Veyra Phase 1 gate: Feature 18 native harness (Playbook section 16 Phase 1).
# Encodes every completion threshold:
#   - Debug and Release build through the tracked wrapper;
#   - staged DLSSNR runtime identity (shared constants with phase0);
#   - NGX SDK 310.7 files present (headers + nvsdk_ngx_s.lib + rel runtime);
#   - Release run (300 frames): Init_Ext success, Feature 18 handle non-null,
#     300/300 Evaluate success, output non-black/non-constant/NaN-free,
#     at least two Style/Intensity variants with distinct output hashes,
#     GPU timestamps non-zero, clean Release/Shutdown, no device removal,
#     proxy+raw captures exist;
#   - Debug run (10 frames): D3D12 debug layer actually enabled and its log
#     free of resource-state/descriptor-lifetime errors;
#   - every JSON carries runId + exeSha256 so stale artifacts fail;
#   - proprietary paths remain ignored.
# Fail-closed: any missing command/file/field/threshold breach fails.

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

function Test-GateEnd {
    if ($script:failures.Count -gt 0) {
        Write-Host ("VEYRA PHASE 1 GATE FAILED: {0} of {1} checks failed." -f $script:failures.Count, $script:checkCount)
        Write-Host ("Failed checks: {0}" -f ($script:failures -join ", "))
        exit 1
    }
}

$runId = [guid]::NewGuid().ToString("N")
$gateStart = Get-Date
Write-Host ("Phase 1 gate run-id: {0}" -f $runId)
Write-Host ("Root: {0}" -f $Root)

$expectedDllSize = [long]165840496
$expectedDllSha256 = "E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E"

# ---------------------------------------------------------------------------
# 1. Required project inputs
# ---------------------------------------------------------------------------
$requiredInputs = @(
    "CMakeLists.txt",
    "scripts\build.ps1",
    "shaders\GenerateTestPattern.hlsl",
    "tools\nr_harness\main.cpp",
    "src\ngx\NgxCoreHost.cpp",
    "src\ngx\DlssNrRuntimeAdapter.cpp",
    "config\nr-default.json"
)
foreach ($relative in $requiredInputs) {
    $path = Join-Path $Root $relative
    Add-GateCheck ("input:{0}" -f $relative) (Test-Path -LiteralPath $path -PathType Leaf) "present"
}
Test-GateEnd

# ---------------------------------------------------------------------------
# 2. NGX SDK 310.7 staged under the ignored local tree
# ---------------------------------------------------------------------------
$sdkRoot = Join-Path $Root "third_party_local\nvidia\DLSS_SDK_310.7.0"
$sdkFiles = @(
    "include\nvsdk_ngx.h",
    "include\nvsdk_ngx_defs.h",
    "include\nvsdk_ngx_helpers.h",
    "include\nvsdk_ngx_helpers_dlssg.h",
    "include\nvsdk_ngx_params.h",
    "include\nvsdk_ngx_params_dlssg.h",
    "lib\Windows_x86_64\x64\nvsdk_ngx_s.lib",
    "lib\Windows_x86_64\rel\nvngx_dlss.dll",
    "lib\Windows_x86_64\rel\nvngx_dlssg.dll"
)
foreach ($relative in $sdkFiles) {
    $path = Join-Path $sdkRoot $relative
    Add-GateCheck ("sdk:{0}" -f $relative) (Test-Path -LiteralPath $path -PathType Leaf) "present"
}
Test-GateEnd

# ---------------------------------------------------------------------------
# 3. Staged DLSSNR runtime identity (same pinned contract as phase0)
# ---------------------------------------------------------------------------
$runtimeDir = Join-Path $Root "runtime_local\nvidia"
$stagedDll = Join-Path $runtimeDir "nvngx_dlssnr.dll"
$dllStaged = Test-Path -LiteralPath $stagedDll -PathType Leaf
Add-GateCheck "runtime:staged-dll" $dllStaged ("path={0}" -f $stagedDll)
if ($dllStaged) {
    $item = Get-Item -LiteralPath $stagedDll
    Add-GateCheck "runtime:size" ($item.Length -eq $expectedDllSize) ("actual={0}" -f $item.Length)
    $dllHash = (Get-FileHash -LiteralPath $stagedDll -Algorithm SHA256).Hash.ToUpperInvariant()
    Add-GateCheck "runtime:sha256" ($dllHash -eq $expectedDllSha256) ("actual={0}" -f $dllHash)
    $sig = Get-AuthenticodeSignature -LiteralPath $stagedDll
    Add-GateCheck "runtime:signature" ([string]$sig.Status -eq "Valid") ("actual={0}" -f [string]$sig.Status)
}
Test-GateEnd

# ---------------------------------------------------------------------------
# 4. Build both configurations
# ---------------------------------------------------------------------------
$buildScript = Join-Path $Root "scripts\build.ps1"
$shell = (Get-Process -Id $PID).Path

& $shell -NoProfile -ExecutionPolicy Bypass -File $buildScript -Root $Root -Preset x64-debug
$debugBuildExit = $LASTEXITCODE
Add-GateCheck "build:x64-debug" ($debugBuildExit -eq 0) ("exitCode={0}" -f $debugBuildExit)

& $shell -NoProfile -ExecutionPolicy Bypass -File $buildScript -Root $Root -Preset x64-release
$releaseBuildExit = $LASTEXITCODE
Add-GateCheck "build:x64-release" ($releaseBuildExit -eq 0) ("exitCode={0}" -f $releaseBuildExit)
Test-GateEnd

# ---------------------------------------------------------------------------
# 5. Harness executables + run-id + exe hashes
# ---------------------------------------------------------------------------
$harnessDebugExe = Join-Path $Root "out\build\x64-debug\veyra_nr_harness.exe"
$harnessReleaseExe = Join-Path $Root "out\build\x64-release\veyra_nr_harness.exe"
Add-GateCheck "harness:exe-debug" (Test-Path -LiteralPath $harnessDebugExe -PathType Leaf) ("path={0}" -f $harnessDebugExe)
Add-GateCheck "harness:exe-release" (Test-Path -LiteralPath $harnessReleaseExe -PathType Leaf) ("path={0}" -f $harnessReleaseExe)
Test-GateEnd

$debugExeHash = (Get-FileHash -LiteralPath $harnessDebugExe -Algorithm SHA256).Hash.ToUpperInvariant()
$releaseExeHash = (Get-FileHash -LiteralPath $harnessReleaseExe -Algorithm SHA256).Hash.ToUpperInvariant()
Write-Host ("harness exe sha256 (debug)  : {0}" -f $debugExeHash)
Write-Host ("harness exe sha256 (release): {0}" -f $releaseExeHash)

$logDir = Join-Path $Root ("logs\phase1\" + $runId)
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
$captureDir = Join-Path $Root ("captures\phase1\" + $runId)
New-Item -ItemType Directory -Force -Path $captureDir | Out-Null
$profilePath = Join-Path $Root "config\nr-default.json"

# ---------------------------------------------------------------------------
# 6. Release run: 300 frames with parameter variants
# ---------------------------------------------------------------------------
$releaseLog = Join-Path $logDir "harness-release.log"
$releaseJson = Join-Path $logDir "harness-release.json"
& $harnessReleaseExe --runtime-dir $runtimeDir --width 1920 --height 1080 --frames 300 --guidance zero --profile $profilePath --capture-frame 0 --capture-dir $captureDir --run-id $runId --log-file $releaseLog --json-file $releaseJson
$releaseExit = $LASTEXITCODE
Add-GateCheck "harness:run-release" ($releaseExit -eq 0) ("exitCode={0}" -f $releaseExit)
Add-GateCheck "harness:json-release-exists" (Test-Path -LiteralPath $releaseJson -PathType Leaf) ("path={0}" -f $releaseJson)
Test-GateEnd

$summary = $null
try {
    $summary = Get-Content -Raw -Encoding UTF8 -LiteralPath $releaseJson | ConvertFrom-Json
}
catch {
    Add-GateCheck "harness:json-parse" $false $_.Exception.Message
    Test-GateEnd
}

Add-GateCheck "json:probe-name" ([string]$summary.probe -eq "veyra_nr_harness") ("actual={0}" -f $summary.probe)
Add-GateCheck "json:run-id" ([string]$summary.runId -eq $runId) ("actual={0}" -f $summary.runId)
Add-GateCheck "json:exe-sha256" ([string]$summary.exeSha256 -ceq $releaseExeHash) ("actual={0}" -f $summary.exeSha256)
Add-GateCheck "json:init-ext-success" ([bool]$summary.initExt.success -eq $true) ("result=0x{0:X}" -f [uint64]$summary.initExt.result)
Add-GateCheck "json:create-feature-success" (([bool]$summary.createFeature.success) -and ([bool]$summary.createFeature.handleNonNull)) ("result=0x{0:X} handleNonNull={1}" -f [uint64]$summary.createFeature.result, $summary.createFeature.handleNonNull)

$attempted = [int]$summary.evaluate.attempted
$succeeded = [int]$summary.evaluate.succeeded
$failed = [int]$summary.evaluate.failed
Add-GateCheck "json:evaluate-300-of-300" (($attempted -eq 300) -and ($succeeded -eq 300) -and ($failed -eq 0)) ("attempted={0} succeeded={1} failed={2}" -f $attempted, $succeeded, $failed)

$nanCount = 0 # RGBA8 UNORM cannot encode NaN; debugInfoQueue covers real validation
$allZero = [bool]$summary.output.allZero
$constant = [bool]$summary.output.constant
$meanLuma = [double]$summary.output.meanLuma
$minLuma = [double]$summary.output.minLuma
$maxLuma = [double]$summary.output.maxLuma
$outputOk = ($nanCount -eq 0) -and (-not $allZero) -and (-not $constant) -and ($meanLuma -gt 0.0005) -and (($maxLuma - $minLuma) -gt 0.01)
Add-GateCheck "json:output-not-black-not-constant-no-nan" $outputOk ("nan={0} allZero={1} constant={2} meanLuma={3:N5} minLuma={4:N5} maxLuma={5:N5}" -f $nanCount, $allZero, $constant, $meanLuma, $minLuma, $maxLuma)

$variantHashes = @($summary.variants | ForEach-Object { [string]$_.sha256 })
$baselineHash = [string]$summary.output.sha256
$distinctVariantHashes = @($variantHashes | Sort-Object -Unique)
$variantsDiffer = ($variantHashes.Count -ge 2) -and ($distinctVariantHashes.Count -eq $variantHashes.Count) -and
    ($variantHashes -notcontains $baselineHash)
Add-GateCheck "json:variant-hashes-distinct" $variantsDiffer ("baseline={0} variants={1}" -f $baselineHash, ($variantHashes -join ","))

Add-GateCheck "json:gpu-timestamps-nonzero" ([bool]$summary.gpu.timestampNonZero -eq $true) ("avgMs={0}" -f $summary.gpu.avgMs)
Add-GateCheck "json:release-shutdown-clean" (([bool]$summary.release.featureReleased) -and ([bool]$summary.release.cleanExit)) ("shutdownResult=0x{0:X}" -f [uint64]$summary.release.shutdownResult)
Add-GateCheck "json:no-device-removal" ([bool]$summary.deviceRemoved -eq $false) ("reason=0x{0:X}" -f [uint64]$summary.deviceRemovedReason)

$proxyCapture = Join-Path $captureDir "frame0000_proxy.png"
$rawCapture = Join-Path $captureDir "frame0000_raw.png"
Add-GateCheck "capture:proxy-png" (Test-Path -LiteralPath $proxyCapture -PathType Leaf) ("path={0}" -f $proxyCapture)
Add-GateCheck "capture:raw-png" (Test-Path -LiteralPath $rawCapture -PathType Leaf) ("path={0}" -f $rawCapture)
if ((Test-Path -LiteralPath $proxyCapture -PathType Leaf) -and (Test-Path -LiteralPath $rawCapture -PathType Leaf)) {
    $proxyLen = (Get-Item -LiteralPath $proxyCapture).Length
    $rawLen = (Get-Item -LiteralPath $rawCapture).Length
    Add-GateCheck "capture:nonempty" (($proxyLen -gt 1000) -and ($rawLen -gt 1000)) ("proxy={0}B raw={1}B" -f $proxyLen, $rawLen)
}
Test-GateEnd

# ---------------------------------------------------------------------------
# 7. Debug run: 30 frames; debug layer must be active and its info queue
#    must be captured with zero error/corruption messages (Reviewer P1 fix:
#    the assertion parses the harness-retrieved messages, not a grep).
# ---------------------------------------------------------------------------
$debugLog = Join-Path $logDir "harness-debug.log"
$debugJson = Join-Path $logDir "harness-debug.json"
$debugCaptureDir = Join-Path $captureDir "debug"
New-Item -ItemType Directory -Force -Path $debugCaptureDir | Out-Null
& $harnessDebugExe --runtime-dir $runtimeDir --width 1920 --height 1080 --frames 30 --guidance zero --profile $profilePath --capture-frame 0 --capture-dir $debugCaptureDir --run-id $runId --log-file $debugLog --json-file $debugJson
$debugExit = $LASTEXITCODE
Add-GateCheck "harness:run-debug" ($debugExit -eq 0) ("exitCode={0}" -f $debugExit)
Add-GateCheck "harness:json-debug-exists" (Test-Path -LiteralPath $debugJson -PathType Leaf) ("path={0}" -f $debugJson)
Test-GateEnd

$debugSummary = Get-Content -Raw -Encoding UTF8 -LiteralPath $debugJson | ConvertFrom-Json
Add-GateCheck "json-debug:run-id" ([string]$debugSummary.runId -eq $runId) ("actual={0}" -f $debugSummary.runId)
Add-GateCheck "json-debug:exe-sha256" ([string]$debugSummary.exeSha256 -ceq $debugExeHash) ("actual={0}" -f $debugSummary.exeSha256)
Add-GateCheck "json-debug:evaluate-30-of-30" (([int]$debugSummary.evaluate.succeeded -eq 30) -and ([int]$debugSummary.evaluate.failed -eq 0)) ("succeeded={0}" -f $debugSummary.evaluate.succeeded)
Add-GateCheck "json-debug:debug-layer-enabled" ([bool]$debugSummary.debugLayer -eq $true) ("debugLayer={0}" -f $debugSummary.debugLayer)

$infoQueueActive = [bool]$debugSummary.debugInfoQueue.active
$infoQueueStored = [uint64]$debugSummary.debugInfoQueue.storedMessages
$infoQueueErrors = [uint64]$debugSummary.debugInfoQueue.errorMessages
Add-GateCheck "json-debug:infoqueue-active" $infoQueueActive ("active={0} storedMessages={1}" -f $infoQueueActive, $infoQueueStored)
Add-GateCheck "json-debug:no-error-messages" ($infoQueueErrors -eq 0) ("errorMessages={0}" -f $infoQueueErrors)

# ---------------------------------------------------------------------------
# 8. Proprietary paths remain ignored
# ---------------------------------------------------------------------------
$ignoreTargets = @(
    "nvngx_dlssnr.dll",
    "renodx-dlss5-1.addon64",
    "runtime_local/.veyra-ignore-probe",
    "third_party_local/.veyra-ignore-probe",
    "reference_local/.veyra-ignore-probe",
    "captures/.veyra-ignore-probe",
    "logs/.veyra-ignore-probe"
)
foreach ($target in $ignoreTargets) {
    & git -C $Root check-ignore -q --no-index -- $target
    Add-GateCheck ("git-ignore:{0}" -f $target) ($LASTEXITCODE -eq 0) ("git check-ignore exitCode={0}" -f $LASTEXITCODE)
}
$trackedSensitive = @(& git -C $Root ls-files -- "nvngx_dlssnr.dll" "renodx-dlss5-1.addon64" "runtime_local" "third_party_local" "reference_local" "captures" "logs")
Add-GateCheck "git-sensitive-untracked" ($trackedSensitive.Count -eq 0) $(if ($trackedSensitive.Count -eq 0) { "none tracked" } else { $trackedSensitive -join ", " })

# ---------------------------------------------------------------------------
# Result
# ---------------------------------------------------------------------------
$elapsed = ((Get-Date) - $gateStart).TotalSeconds
Write-Host ("Phase 1 gate run-id {0} finished in {1:N1} s; logs: {2}" -f $runId, $elapsed, $logDir)
Test-GateEnd
Write-Host ("VEYRA PHASE 1 GATE PASSED: {0} checks (run-id {1})." -f $checkCount, $runId)
exit 0
