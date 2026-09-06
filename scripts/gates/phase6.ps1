[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$Root)

# Superseded by the user's explicit short-test + realtime-profile contract.
& (Join-Path $PSScriptRoot 'delivery.ps1') -Root $Root
exit $LASTEXITCODE
# The original long gate below is retained as historical specification only.

# Veyra Phase 6 gate: DLSSG 2X and the realtime engine (Launch V1).
# Covers BACKLOG P6.0 / Playbook sections 14 and 22:
#   - nvngx_dlssg.dll staged from the SDK with the pinned identity
#   - DlssFgBackend capability query: FrameGeneration.Available,
#     FeatureInitResult, driver requirement, MultiFrameCountMax, HAGS evidence
#   - DLSSG create/evaluate/release with ResourceNeverProvided flags
#   - FG truth (Playbook 14.3): 60 real frames -> strictly 59 usable generated;
#     generated hash != neighbors; not a 50/50 blend (pixel residual); known
#     translation lands at the midpoint (direction/scale); PTS monotonic with
#     generated PTS = (prev+cur)/2
#   - Scene cut: no cross-boundary generation; backend reset; present real
#   - WASAPI event-mode audio: steady playback, underruns=0, drift logged
#   - Player probe: play/pause/seek/resize/loop, A/V drift <= 50 ms,
#     NR/SR/FG independently togglable, normal-path readback = 0
#   - Endurance (user-approved 5 min per run): 4K30 + 4K60 player runs,
#     bounded queues, no sustained backlog, memory growth bounded
# FAIL-CLOSED: every missing implementation or threshold breach fails.

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$failures = New-Object System.Collections.Generic.List[string]
$checkCount = 0

function Add-GateCheck {
    param([string]$Name, [bool]$Passed, [string]$Detail)
    $script:checkCount = $script:checkCount + 1
    $tag = "FAIL"; if ($Passed) { $tag = "PASS" }
    Write-Host ("[{0}] {1} :: {2}" -f $tag, $Name, $Detail)
    if (-not $Passed) { $script:failures.Add($Name) | Out-Null }
}
function Test-GateEnd {
    if ($script:failures.Count -gt 0) {
        Write-Host ("VEYRA PHASE 6 GATE FAILED: {0} of {1} checks." -f $failures.Count, $checkCount)
        Write-Host ("Failed: {0}" -f ($failures -join ", "))
        exit 1
    }
}

$runId = [guid]::NewGuid().ToString("N")
Write-Host ("Phase 6 gate run-id: {0}" -f $runId)

# ---------------------------------------------------------------------------
# 0. Script hygiene: this gate must contain no control characters other
#    than CR/LF/TAB (a 2026-09-04 corruption introduced U+000C/U+000B/U+0008
#    into path literals; any recurrence fails closed).
# ---------------------------------------------------------------------------
$gateSelfPath = Join-Path $Root "scripts\gates\phase6.ps1"
$gateBytes = [System.IO.File]::ReadAllBytes($gateSelfPath)
$badControl = @()
for ($i = 0; $i -lt $gateBytes.Length; ++$i) {
    $b = $gateBytes[$i]
    if (($b -lt 0x20) -and ($b -ne 0x0D) -and ($b -ne 0x0A) -and ($b -ne 0x09)) {
        $badControl += ("offset={0} byte=0x{1:X2}" -f $i, $b)
    }
}
Add-GateCheck "gate:self-control-chars" ($badControl.Count -eq 0) (
    $(if ($badControl.Count -eq 0) { "clean" } else { ($badControl[0..([Math]::Min(3, $badControl.Count - 1))] -join "; ") }))
Test-GateEnd

# ---------------------------------------------------------------------------
# 1. Build
# ---------------------------------------------------------------------------
$buildScript = Join-Path $Root "scripts\build.ps1"
$shell = (Get-Process -Id $PID).Path
& $shell -NoProfile -ExecutionPolicy Bypass -File $buildScript -Root $Root -Preset x64-release
Add-GateCheck "build:x64-release" ($LASTEXITCODE -eq 0) "exitCode=$LASTEXITCODE"
Test-GateEnd

$fgHarnessExe = Join-Path $Root "out\build\x64-release\veyra_fg_harness.exe"
Add-GateCheck "harness:fg-exe" (Test-Path -LiteralPath $fgHarnessExe -PathType Leaf) "present"
Test-GateEnd

$playerProbeExe = Join-Path $Root "out\build\x64-release\veyra_player_probe.exe"
Add-GateCheck "harness:player-probe-exe" (Test-Path -LiteralPath $playerProbeExe -PathType Leaf) "present"

$runtimeDir = Join-Path $Root "runtime_local\nvidia"
$logDir = Join-Path $Root ("logs\phase6\" + $runId)
New-Item -ItemType Directory -Force -Path $logDir | Out-Null

# ---------------------------------------------------------------------------
# 2. DLSSG runtime staged with the pinned identity (Playbook section 3.2/14)
# ---------------------------------------------------------------------------
$dlssgDll = Join-Path $runtimeDir "nvngx_dlssg.dll"
$manifestPath = Join-Path $runtimeDir "runtime-manifest.json"
Add-GateCheck "runtime:dlssg-staged" (Test-Path -LiteralPath $dlssgDll -PathType Leaf) "path=$dlssgDll"
if (Test-Path -LiteralPath $manifestPath -PathType Leaf) {
    $manifest = Get-Content -Raw -Encoding UTF8 -LiteralPath $manifestPath | ConvertFrom-Json
    $fgEntries = @($manifest.files | Where-Object { $_.name -eq "nvngx_dlssg.dll" })
    $fgEntryOk = ($fgEntries.Count -eq 1) -and
        ([int64]$fgEntries[0].size -eq 7519856) -and
        ([string]$fgEntries[0].sha256 -ceq "135EAF0733C1E37381A8C28ABCF7A862404A54132B81787C04E35D09EFC5E36F") -and
        ([string]$fgEntries[0].authenticode -ceq "Valid")
    Add-GateCheck "runtime:dlssg-manifest-pinned" $fgEntryOk "entries=$($fgEntries.Count)"
    if ($fgEntries.Count -eq 1) {
        $stagedHash = (Get-FileHash -LiteralPath $dlssgDll -Algorithm SHA256).Hash.ToUpperInvariant()
        Add-GateCheck "runtime:dlssg-hash" ($stagedHash -ceq [string]$fgEntries[0].sha256) "sha256=$($stagedHash.Substring(0, 12))..."
    }
}
else {
    Add-GateCheck "runtime:dlssg-manifest-pinned" $false "manifest missing"
}
Test-GateEnd

# ---------------------------------------------------------------------------
# 3. FG capability probe (DlssFgBackend::queryCapability)
# ---------------------------------------------------------------------------
$capJson = Join-Path $logDir "fg-capability.json"
& $fgHarnessExe --fg-cap --runtime-dir $runtimeDir --run-id $runId --log-file (Join-Path $logDir "fg-capability.log") --json-file $capJson
$capExit = $LASTEXITCODE
Add-GateCheck "fg:capability-run" ($capExit -eq 0) "exitCode=$capExit"
if (Test-Path -LiteralPath $capJson -PathType Leaf) {
    $cap = Get-Content -Raw -Encoding UTF8 -LiteralPath $capJson | ConvertFrom-Json
    Add-GateCheck "fg:capability-available" ([bool]$cap.fgAvailable -eq $true) "available=$($cap.fgAvailable)"
    Add-GateCheck "fg:capability-initresult-logged" ($null -ne $cap.featureInitResult) "featureInitResult=$($cap.featureInitResult)"
    Add-GateCheck "fg:capability-driver-logged" ($null -ne $cap.needsUpdatedDriver) "needsUpdatedDriver=$($cap.needsUpdatedDriver)"
    Add-GateCheck "fg:capability-multiframe-max" ($null -ne $cap.multiFrameCountMax -and [int64]$cap.multiFrameCountMax -ge 1) "multiFrameCountMax=$($cap.multiFrameCountMax)"
    Add-GateCheck "fg:capability-hags-evidence" (-not [string]::IsNullOrWhiteSpace([string]$cap.hagsEvidence)) "hags=$($cap.hagsEvidence)"
}
else {
    Add-GateCheck "fg:capability-json" $false "missing"
}
Test-GateEnd

# ---------------------------------------------------------------------------
# 4. FG truth harness: synthetic translation + scene cut (Playbook 14.3)
# ---------------------------------------------------------------------------
$fgJson = Join-Path $logDir "fg-truth.json"
& $fgHarnessExe --fg-test --runtime-dir $runtimeDir --run-id $runId --log-file (Join-Path $logDir "fg-truth.log") --json-file $fgJson --capture-dir (Join-Path $logDir "captures")
$fgExit = $LASTEXITCODE
Add-GateCheck "fg:truth-run" ($fgExit -eq 0) "exitCode=$fgExit"
if (Test-Path -LiteralPath $fgJson -PathType Leaf) {
    $fg = Get-Content -Raw -Encoding UTF8 -LiteralPath $fgJson | ConvertFrom-Json
    # Create / structure
    Add-GateCheck "fg:create-success" ([bool]$fg.createSuccess -eq $true) "createResult=0x$([string]$fg.createResultHex)"
    Add-GateCheck "fg:never-provided-flags" ([bool]$fg.neverProvidedFlagsSet -eq $true) "flags=0x$([string]$fg.neverProvidedFlagsHex)"
    # Translation phase (strict 59 of 60)
    Add-GateCheck "fg:translation-real-frames" ([int64]$fg.translation.realFrames -eq 60) "real=$($fg.translation.realFrames)"
    Add-GateCheck "fg:translation-generated" ([int64]$fg.translation.usableGenerated -eq 59) "usable=$($fg.translation.usableGenerated)"
    Add-GateCheck "fg:translation-eval-results" ([int64]$fg.translation.evaluateFailures -eq 0) "failures=$($fg.translation.evaluateFailures)"
    Add-GateCheck "fg:translation-non-duplicate" ([int64]$fg.translation.duplicateHashCount -eq 0) "duplicates=$($fg.translation.duplicateHashCount)"
    # Self-calibrating blend truth (harness renders the exact midpoint frame):
    # blend baseline >= 0.5 (discriminative scene), |gen-blend| > 0.6x baseline
    # (not a blend), |gen-trueInterp| < 0.5x baseline (matches the midpoint).
    $baseline = [double]$fg.translation.meanBlendBaseline
    $genBlend = [double]$fg.translation.minBlendResidual
    $genTrue = [double]$fg.translation.maxTrueResidual
    Add-GateCheck "fg:translation-baseline" ($baseline -ge 0.5) "baseline=$baseline"
    Add-GateCheck "fg:translation-not-blend" ($genBlend -gt (0.6 * $baseline)) "minResidual=$genBlend vs 0.6*baseline=$(0.6 * $baseline)"
    Add-GateCheck "fg:translation-matches-truth" ($genTrue -lt (0.5 * $baseline)) "maxTrueResidual=$genTrue vs 0.5*baseline=$(0.5 * $baseline)"
    Add-GateCheck "fg:translation-midpoint" ([double]$fg.translation.maxMidpointErrorPx -le 6.0) "maxErr=$($fg.translation.maxMidpointErrorPx)px"
    Add-GateCheck "fg:translation-direction" ([bool]$fg.translation.directionCorrect -eq $true) "dir=$($fg.translation.directionCorrect)"
    Add-GateCheck "fg:translation-pts-monotonic" ([bool]$fg.translation.ptsMonotonic -eq $true) "monotonic=$($fg.translation.ptsMonotonic)"
    Add-GateCheck "fg:translation-output-nonblack" ([double]$fg.translation.generatedMeanLuma -gt 0.5) "meanLuma=$($fg.translation.generatedMeanLuma)"
    # Mvec convention must be reported, not assumed (Playbook 14.2)
    Add-GateCheck "fg:mvec-convention-reported" (-not [string]::IsNullOrWhiteSpace([string]$fg.mvecConventionWinner)) "winner=$($fg.mvecConventionWinner)"
    # Scene cut phase
    Add-GateCheck "fg:cut-no-cross-generation" ([bool]$fg.cut.crossCutGenerated -eq $false) "crossCut=$($fg.cut.crossCutGenerated)"
    Add-GateCheck "fg:cut-reset-issued" ([bool]$fg.cut.resetIssued -eq $true) "reset=$($fg.cut.resetIssued)"
    Add-GateCheck "fg:cut-usable-bounded" (([int64]$fg.cut.usableGenerated -ge 56) -and ([int64]$fg.cut.usableGenerated -le 58)) "usable=$($fg.cut.usableGenerated)"
}
else {
    Add-GateCheck "fg:truth-json" $false "missing"
}
Test-GateEnd

# ---------------------------------------------------------------------------
# 5. WASAPI event-mode audio probe (P6.4)
# ---------------------------------------------------------------------------
$audioJson = Join-Path $logDir "audio-wasapi.json"
& $fgHarnessExe --audio-test --run-id $runId --log-file (Join-Path $logDir "audio-wasapi.log") --json-file $audioJson
$audioExit = $LASTEXITCODE
Add-GateCheck "audio:wasapi-run" ($audioExit -eq 0) "exitCode=$audioExit"
if (Test-Path -LiteralPath $audioJson -PathType Leaf) {
    $au = Get-Content -Raw -Encoding UTF8 -LiteralPath $audioJson | ConvertFrom-Json
    Add-GateCheck "audio:event-mode" ([bool]$au.eventMode -eq $true) "eventMode=$($au.eventMode)"
    Add-GateCheck "audio:steady-underruns" ([int64]$au.underrunsSteady -eq 0) "underruns=$($au.underrunsSteady)"
    Add-GateCheck "audio:frames-written" ([int64]$au.framesWritten -ge [int64]$au.framesPlanned) "written=$($au.framesWritten)/$($au.framesPlanned)"
    Add-GateCheck "audio:drift-bounded" ([double]$au.maxAbsDriftMs -le 5.0) "drift=$($au.maxAbsDriftMs)ms"
    Add-GateCheck "audio:pause-flush" ([bool]$au.pauseFlushWorks -eq $true) "pauseFlush=$($au.pauseFlushWorks)"
}
else {
    Add-GateCheck "audio:wasapi-json" $false "missing"
}
Test-GateEnd

# ---------------------------------------------------------------------------
# 6. Player probe: real media + present + audio + FG (P6.6)
# Two scenarios: 1080p (full SR->NR->FG chain) and 4K (1:1 SR bypass, all
# toggles). The 1080p run proves the scaling chain; the 4K run proves the
# native-4K path and the SR bypass toggle definition.
# ---------------------------------------------------------------------------
$clip1080 = Join-Path $Root "loop\local\fixed_clips\test_av_1080p.mp4"
$clip4k = Join-Path $Root "loop\local\fixed_clips\test_av_4k.mp4"
$playerJson = Join-Path $logDir "player-1080p.json"
if ((Test-Path -LiteralPath $playerProbeExe -PathType Leaf) -and (Test-Path -LiteralPath $clip1080 -PathType Leaf)) {
    $ffmpegBin = "C:\veyra-deps\installed\x64-windows\bin"
    if (Test-Path -LiteralPath (Join-Path $ffmpegBin "avcodec-63.dll") -PathType Leaf) {
        $env:PATH = "$ffmpegBin;" + $env:PATH
    }
    & $playerProbeExe --input $clip1080 --runtime-dir $runtimeDir --run-id $runId --log-file (Join-Path $logDir "player-1080p.log") --json-file $playerJson
    Add-GateCheck "player:scenario-1080p-run" ($LASTEXITCODE -eq 0) "exitCode=$LASTEXITCODE"
}
else {
    Add-GateCheck "player:scenario-1080p-run" $false "player_probe exe or 1080p clip missing"
}
if (Test-Path -LiteralPath $playerJson -PathType Leaf) {
    $pp = Get-Content -Raw -Encoding UTF8 -LiteralPath $playerJson | ConvertFrom-Json
    Add-GateCheck "player:play-pause" ([bool]$pp.playPauseWorks -eq $true) "playPause=$($pp.playPauseWorks)"
    Add-GateCheck "player:seek" ([bool]$pp.seekWorks -eq $true) "seeks=$($pp.seekCount)"
    Add-GateCheck "player:resize" ([bool]$pp.resizeWorks -eq $true) "resize=$($pp.resizeWorks)"
    Add-GateCheck "player:av-drift" ([double]$pp.maxAvDriftMs -le 50.0) "drift=$($pp.maxAvDriftMs)ms"
    Add-GateCheck "player:fg-toggable" ([bool]$pp.fgToggleWorks -eq $true) "fgToggle=$($pp.fgToggleWorks)"
    Add-GateCheck "player:nr-toggable" ([bool]$pp.nrToggleWorks -eq $true) "nrToggle=$($pp.nrToggleWorks)"
    Add-GateCheck "player:normal-path-readback" ([int64]$pp.normalPathReadbackCount -eq 0) "readbacks=$($pp.normalPathReadbackCount)"
    Add-GateCheck "player:present-count" ([int64]$pp.presentCount -ge 200) "presents=$($pp.presentCount)"
    Add-GateCheck "player:sr-chain" ([int64]$pp.srEvaluateCount -ge 100) "srEvaluates=$($pp.srEvaluateCount)"
    Add-GateCheck "player:nr-chain" ([int64]$pp.nrEvaluateCount -ge 100) "nrEvaluates=$($pp.nrEvaluateCount)"
    Add-GateCheck "player:fg-generated" ([int64]$pp.fgGeneratedFrames -ge 100) "fg=$($pp.fgGeneratedFrames)"
    Add-GateCheck "player:mvec-honest" (-not [string]::IsNullOrWhiteSpace([string]$pp.mvecSource)) "mvec=$($pp.mvecSource)"
}
else {
    Add-GateCheck "player:scenario-1080p-json" $false "missing"
}
Test-GateEnd

$player4kJson = Join-Path $logDir "player-4k.json"
if ((Test-Path -LiteralPath $playerProbeExe -PathType Leaf) -and (Test-Path -LiteralPath $clip4k -PathType Leaf)) {
    & $playerProbeExe --input $clip4k --runtime-dir $runtimeDir --run-id "$runId-4k" --log-file (Join-Path $logDir "player-4k.log") --json-file $player4kJson
    Add-GateCheck "player:scenario-4k-run" ($LASTEXITCODE -eq 0) "exitCode=$LASTEXITCODE"
}
else {
    Add-GateCheck "player:scenario-4k-run" $false "player_probe exe or 4k clip missing"
}
if (Test-Path -LiteralPath $player4kJson -PathType Leaf) {
    $p4 = Get-Content -Raw -Encoding UTF8 -LiteralPath $player4kJson | ConvertFrom-Json
    Add-GateCheck "player:4k-seek" ([bool]$p4.seekWorks -eq $true) "seeks=$($p4.seekCount)"
    Add-GateCheck "player:4k-av-drift" ([double]$p4.maxAvDriftMs -le 50.0) "drift=$($p4.maxAvDriftMs)ms"
    Add-GateCheck "player:sr-toggable" ([bool]$p4.srToggleWorks -eq $true) "srToggle=$($p4.srToggleWorks) (1:1 bypass)"
    Add-GateCheck "player:4k-fg-generated" ([int64]$p4.fgGeneratedFrames -ge 100) "fg=$($p4.fgGeneratedFrames)"
}
else {
    Add-GateCheck "player:scenario-4k-json" $false "missing"
}
Test-GateEnd

# ---------------------------------------------------------------------------
# 7. Endurance: 4K30 + 4K60 player (user-approved 5 minutes per run)
# ---------------------------------------------------------------------------
$enduranceJson = Join-Path $logDir "endurance-4k.json"
if ((Test-Path -LiteralPath $playerProbeExe -PathType Leaf) -and (Test-Path -LiteralPath $clip4k -PathType Leaf)) {
    Write-Host "[INFO] endurance: player 4K30+4K60 (5 min each)..."
    & $playerProbeExe --input $clip4k --runtime-dir $runtimeDir --endurance --duration-seconds 300 --run-id "$runId-end" --log-file (Join-Path $logDir "endurance-4k.log") --json-file $enduranceJson
    $endExit = $LASTEXITCODE
    Add-GateCheck "endurance:player-run" ($endExit -eq 0) "exitCode=$endExit"
}
else {
    Add-GateCheck "endurance:player-run" $false "player_probe exe or clip missing"
}
if (Test-Path -LiteralPath $enduranceJson -PathType Leaf) {
    $en = Get-Content -Raw -Encoding UTF8 -LiteralPath $enduranceJson | ConvertFrom-Json
    Add-GateCheck "endurance:4k30-duration" ([double]$en.run4k30.durationSeconds -ge 290) "duration=$($en.run4k30.durationSeconds)s"
    Add-GateCheck "endurance:4k60-duration" ([double]$en.run4k60.durationSeconds -ge 290) "duration=$($en.run4k60.durationSeconds)s"
    Add-GateCheck "endurance:4k60-fg-generated" ([int64]$en.run4k60.fgGeneratedFrames -ge 8000) "fg=$($en.run4k60.fgGeneratedFrames)"
    Add-GateCheck "endurance:4k60-internal-timeline" ([double]$en.run4k60.internalTimelineHz -ge 110.0) "hz=$($en.run4k60.internalTimelineHz)"
    Add-GateCheck "endurance:memory-bounded" ([double]$en.workingSetGrowthMB -lt 512) "wsGrowth=$($en.workingSetGrowthMB)MB"
    Add-GateCheck "endurance:queue-bounded" ([int64]$en.maxInFlightFrames -le 4) "maxInFlight=$($en.maxInFlightFrames)"
}
Test-GateEnd

# ---------------------------------------------------------------------------
# 8. Proprietary paths remain ignored.
# ---------------------------------------------------------------------------
$ignoreTargets = @("nvngx_dlssnr.dll", "renodx-dlss5-1.addon64",
    "runtime_local/.veyra-ignore-probe", "third_party_local/.veyra-ignore-probe",
    "captures/.veyra-ignore-probe", "logs/.veyra-ignore-probe")
foreach ($target in $ignoreTargets) {
    & git -C $Root check-ignore -q --no-index -- $target
    Add-GateCheck ("git-ignore:{0}" -f $target) ($LASTEXITCODE -eq 0) "exitCode=$LASTEXITCODE"
}

Write-Host ("Phase 6 gate run-id {0}; logs: {1}" -f $runId, $logDir)
Test-GateEnd
Write-Host ("VEYRA PHASE 6 GATE PASSED: {0} checks (run-id {1})." -f $checkCount, $runId)
exit 0
