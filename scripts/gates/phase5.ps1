[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$Root)

# Veyra Phase 5 gate (R1.1 rewrite, 2026-09-06): product-grade unified guidance
# and quality core. This replaces the invalidated gate that (a) accepted a depth
# manifest as provider completion, (b) ran a second 1080p endurance in the
# native-4K slot, (c) shortened 30-minute endurance to 5 minutes, and (d) never
# proved that a product library executed the GPU chain.
#
# Runner contract (implemented by Playbook R3/R4):
#   veyra_quality_probe.exe --corpus <manifest.json> --guidance <off|zero|motion|motion-depth|auto>
#                           --run-id <id> --log-file <p> --json-file <p>
#   veyra_quality_probe.exe --input <clip> --duration-seconds <n>
#                           --run-id <id> --log-file <p> --json-file <p>
# The runner must link veyra_pipeline/veyra_guidance (shared product graph), not
# a private copy of the chain. Every run JSON must carry:
#   runId, exeHash, inputHash, configHash, runtimeHash
#   sourceExtent{width,height}, workingExtent{width,height}, outputExtent{width,height}
#   sourceFrames, processedFrames, nrEvaluateCount, nvofExecuteCount
#   guidanceProvenance, nonZeroMotionCount, confidenceP05/P50/P95
#   depthMode, depthFallbackReason, depthAgeP95
#   resetCountsByReason, sceneCutCount, crossCutHistoryCount
#   gpuPassP50Ms, gpuPassP95Ms, normalPathReadbackCount, cpuFenceWaitPerFrame
#   queueHighWater, resourcePoolPeakMiB, vramBudgetHeadroomMiB
#   workingSetStartMiB/PeakMiB/EndMiB, deviceRemovedCount, durationSeconds
#
# Thresholds (Product Spec 11, Playbook R1/R5):
#   endurance >= 1790s measured (30-minute bar, 10s startup tolerance)
#   processedFrames >= 97000 (~30min x 60fps with tolerance)
#   vramBudgetHeadroomMiB >= 1536 (1.5 GiB)
#   queueHighWater <= 6, wsGrowth < 512 MiB, deviceRemoved == 0
#   normalPathReadbackCount == 0, cpuFenceWaitPerFrame == 0
# A depth manifest can NEVER satisfy a provider check in this gate.
# FAIL-CLOSED: missing artifact, field, hash mismatch, or threshold breach fails.

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
        Write-Host ("VEYRA PHASE 5 GATE FAILED: {0} of {1} checks." -f $script:failures.Count, $script:checkCount)
        Write-Host ("Failed: {0}" -f ($script:failures -join ", "))
        exit 1
    }
}
function Get-Field {
    param($Object, [string]$Name)
    if ($null -eq $Object) { return $null }
    $p = $Object.PSObject.Properties[$Name]
    if ($null -eq $p) { return $null }
    return $p.Value
}

$runId = [guid]::NewGuid().ToString("N")
Write-Host ("Phase 5 gate run-id: {0}" -f $runId)
$logDir = Join-Path $Root ("logs\phase5\" + $runId)
New-Item -ItemType Directory -Force -Path $logDir | Out-Null

# ---------------------------------------------------------------------------
# 0. Gate self-hygiene: no control characters (PowerShell 5.1 codepage safety)
# ---------------------------------------------------------------------------
$scriptText = Get-Content -Raw -LiteralPath $PSCommandPath
$badChars = [regex]::Matches($scriptText, "[\x00-\x08\x0B\x0C\x0E-\x1F]")
Add-GateCheck "gate:self-control-chars" ($badChars.Count -eq 0) ("count=" + $badChars.Count)

# ---------------------------------------------------------------------------
# 1. Build (required before anything else)
# ---------------------------------------------------------------------------
$buildScript = Join-Path $Root "scripts\build.ps1"
$shell = (Get-Process -Id $PID).Path
& $shell -NoProfile -ExecutionPolicy Bypass -File $buildScript -Root $Root -Preset x64-release
Add-GateCheck "build:x64-release" ($LASTEXITCODE -eq 0) "exitCode=$LASTEXITCODE"
Test-GateEnd

# ---------------------------------------------------------------------------
# 2. Product library wiring (static fail-closed inputs)
# ---------------------------------------------------------------------------
$cmakePath = Join-Path $Root "CMakeLists.txt"
$cmakeText = ""
if (Test-Path -LiteralPath $cmakePath -PathType Leaf) {
    $cmakeText = Get-Content -Raw -Encoding UTF8 -LiteralPath $cmakePath
}
$hasPipelineTarget = ($cmakeText -match "add_library\s*\(\s*veyra_pipeline\b")
$hasGuidanceTarget = ($cmakeText -match "add_library\s*\(\s*veyra_guidance\b")
$hasRunnerTarget = ($cmakeText -match "veyra_quality_probe\b")
$playerLinksPipeline = ($cmakeText -match "(?s)veyra_player_probe.*?veyra_pipeline") -or ($cmakeText -match "(?s)veyra_pipeline.*?veyra_player_probe")
Add-GateCheck "product:veyra_pipeline-target" $hasPipelineTarget "CMakeLists add_library(veyra_pipeline ...) present=$hasPipelineTarget"
Add-GateCheck "product:veyra_guidance-target" $hasGuidanceTarget "CMakeLists add_library(veyra_guidance ...) present=$hasGuidanceTarget"
Add-GateCheck "product:quality-runner-target" $hasRunnerTarget "CMakeLists references veyra_quality_probe present=$hasRunnerTarget"
Add-GateCheck "product:player-links-pipeline" $playerLinksPipeline "player_probe links veyra_pipeline present=$playerLinksPipeline"

$enhanceGraphSrc = Join-Path $Root "src\pipeline\EnhanceGraph.cpp"
$enhanceOk = $false
if (Test-Path -LiteralPath $enhanceGraphSrc -PathType Leaf) {
    $eg = Get-Content -Raw -Encoding UTF8 -LiteralPath $enhanceGraphSrc
    # The graph source must submit real GPU work, not only copy packets and bump counters.
    $enhanceOk = ($eg -match "ExecuteCommandLists") -and ($eg -match "EvaluateFeature|Evaluate")
}
Add-GateCheck "product:enhance-graph-submits-gpu" $enhanceOk "src/pipeline/EnhanceGraph.cpp executes command lists + NGX evaluate present=$enhanceOk"

# ---------------------------------------------------------------------------
# 3. Deterministic corpus (veyra_clip_gen; no dependency on deleted tracked MP4)
# ---------------------------------------------------------------------------
$corpusManifestPath = Join-Path $Root "loop\local\fixed_clips\corpus-manifest.json"
$corpusManifestDir = Split-Path -Parent $corpusManifestPath
$corpus = $null
Add-GateCheck "corpus:manifest-present" (Test-Path -LiteralPath $corpusManifestPath -PathType Leaf) $corpusManifestPath
if (Test-Path -LiteralPath $corpusManifestPath -PathType Leaf) {
    try { $corpus = Get-Content -Raw -Encoding UTF8 -LiteralPath $corpusManifestPath | ConvertFrom-Json } catch { $corpus = $null }
}
$corpusEntries = @()
if ($null -ne $corpus) {
    $corpusEntries = @(Get-Field $corpus "clips")
    if ($corpusEntries.Count -eq 1 -and $null -eq $corpusEntries[0]) { $corpusEntries = @() }
}
$expectedScenarios = @("translation", "occlusion", "cut-flash-duplicate", "particles", "ui-text")
$corpusComplete = $false
$corpusFilesOk = $false
$corpusHashOk = $false
if ($corpusEntries.Count -ge 10) {
    $corpusComplete = $true
    $corpusFilesOk = $true
    $corpusHashOk = $true
    foreach ($entry in $corpusEntries) {
        $scen = [string](Get-Field $entry "scenario")
        $w = [int](Get-Field $entry "width"); $h = [int](Get-Field $entry "height")
        $fps = [int](Get-Field $entry "fps")
        if ($expectedScenarios -notcontains $scen) { $corpusComplete = $false }
        if ((-not ($w -eq 1920 -and $h -eq 1080)) -and (-not ($w -eq 3840 -and $h -eq 2160))) { $corpusComplete = $false }
        if ($fps -ne 60) { $corpusComplete = $false }
        $clipPath = [string](Get-Field $entry "path")
        $sha = [string](Get-Field $entry "sha256")
        if ([string]::IsNullOrWhiteSpace($clipPath) -or ($sha -notmatch "^[0-9A-Fa-f]{64}$")) { $corpusComplete = $false; continue }
        $absClip = if ([System.IO.Path]::IsPathRooted($clipPath)) { $clipPath } else { Join-Path $corpusManifestDir $clipPath }
        if (-not (Test-Path -LiteralPath $absClip -PathType Leaf)) { $corpusFilesOk = $false; continue }
        $item = Get-Item -LiteralPath $absClip
        if ($item.Length -le 0) { $corpusFilesOk = $false; continue }
        $actual = (Get-FileHash -LiteralPath $absClip -Algorithm SHA256).Hash
        if ($actual -ne $sha) { $corpusHashOk = $false }
    }
    # Both resolutions must be covered per scenario.
    foreach ($scen in $expectedScenarios) {
        $hd = @($corpusEntries | Where-Object { [string](Get-Field $_ "scenario") -eq $scen -and [int](Get-Field $_ "width") -eq 1920 })
        $uhd = @($corpusEntries | Where-Object { [string](Get-Field $_ "scenario") -eq $scen -and [int](Get-Field $_ "width") -eq 3840 })
        if ($hd.Count -lt 1 -or $uhd.Count -lt 1) { $corpusComplete = $false }
    }
}
Add-GateCheck "corpus:manifest-complete" $corpusComplete ("entries=" + $corpusEntries.Count + " scenarios=5 x {1080p,4K} @60fps with sha256/size")
Add-GateCheck "corpus:clip-files" $corpusFilesOk "all manifest clip files exist non-empty"
Add-GateCheck "corpus:clip-sha256" $corpusHashOk "recomputed SHA256 matches manifest"

# ---------------------------------------------------------------------------
# 4. Quality matrix via the shared product runner
# ---------------------------------------------------------------------------
$runnerExe = Join-Path $Root "out\build\x64-release\veyra_quality_probe.exe"
$runnerPresent = (Test-Path -LiteralPath $runnerExe -PathType Leaf)
Add-GateCheck "quality:runner-exe" $runnerPresent $runnerExe

$runtimeDll = Join-Path $Root "runtime_local\nvidia\nvngx_dlssnr.dll"
$runtimeHashExpected = $null
if (Test-Path -LiteralPath $runtimeDll -PathType Leaf) {
    $runtimeHashExpected = (Get-FileHash -LiteralPath $runtimeDll -Algorithm SHA256).Hash
}

$motionJson = $null
if ($runnerPresent -and $corpusComplete) {
    $guidanceModes = @("off", "zero", "motion", "motion-depth", "auto")
    foreach ($mode in $guidanceModes) {
        $qj = Join-Path $logDir ("quality-" + $mode + ".json")
        & $runnerExe --corpus $corpusManifestPath --guidance $mode --run-id ($runId + "-q-" + $mode) --log-file (Join-Path $logDir ("quality-" + $mode + ".log")) --json-file $qj
        Add-GateCheck ("quality:run-" + $mode) ($LASTEXITCODE -eq 0) "exitCode=$LASTEXITCODE"
        if (($mode -eq "motion") -and (Test-Path -LiteralPath $qj -PathType Leaf)) {
            $motionJson = Get-Content -Raw -Encoding UTF8 -LiteralPath $qj | ConvertFrom-Json
        }
    }
}
else {
    foreach ($mode in @("off", "zero", "motion", "motion-depth", "auto")) {
        Add-GateCheck ("quality:run-" + $mode) $false ("not run: runner present=$runnerPresent corpus complete=$corpusComplete")
    }
}

if ($null -ne $motionJson) {
    # Extent contract: 1080p corpus in, SR to 4K working/output extent.
    $srcW = Get-Field (Get-Field $motionJson "sourceExtent") "width"
    $workW = Get-Field (Get-Field $motionJson "workingExtent") "width"
    $outH = Get-Field (Get-Field $motionJson "outputExtent") "height"
    $extentOk = ($srcW -eq 1920) -and ($workW -eq 3840) -and ($outH -eq 2160)
    Add-GateCheck "quality:extent-matrix" $extentOk ("source=$srcW working=$workW outputH=$outH (expect 1920->3840x2160)")

    $srcFrames = [int64](Get-Field $motionJson "sourceFrames")
    $procFrames = [int64](Get-Field $motionJson "processedFrames")
    $nrEval = [int64](Get-Field $motionJson "nrEvaluateCount")
    Add-GateCheck "quality:nr-per-frame" (($srcFrames -gt 0) -and ($procFrames -eq $srcFrames) -and ($nrEval -eq $procFrames)) "source=$srcFrames processed=$procFrames nr=$nrEval"

    $nvofExec = [int64](Get-Field $motionJson "nvofExecuteCount")
    $nonZero = [int64](Get-Field $motionJson "nonZeroMotionCount")
    $prov = [string](Get-Field $motionJson "guidanceProvenance")
    Add-GateCheck "quality:nvof-nonzero-motion" (($nvofExec -gt 0) -and ($nonZero -gt 0) -and ($prov -eq "nvof")) "exec=$nvofExec nonZero=$nonZero provenance=$prov"

    $c05 = [double](Get-Field $motionJson "confidenceP05")
    $c50 = [double](Get-Field $motionJson "confidenceP50")
    $c95 = [double](Get-Field $motionJson "confidenceP95")
    Add-GateCheck "quality:confidence-stats" (($c05 -ge 0) -and ($c05 -le $c50) -and ($c50 -le $c95) -and ($c95 -le 1) -and ($c95 -gt 0)) "p05=$c05 p50=$c50 p95=$c95"

    $resetCounts = Get-Field $motionJson "resetCountsByReason"
    $resetOk = ($null -ne $resetCounts)
    if ($resetOk) { $resetOk = (@($resetCounts.PSObject.Properties).Count -ge 1) }
    $sceneCut = [int64](Get-Field $motionJson "sceneCutCount")
    $crossCut = [int64](Get-Field $motionJson "crossCutHistoryCount")
    Add-GateCheck "quality:reset-contract" ($resetOk -and ($sceneCut -ge 1) -and ($crossCut -eq 0)) "reasons present=$resetOk sceneCut=$sceneCut crossCutHistory=$crossCut (corpus contains cut scenario)"

    $g50 = [double](Get-Field $motionJson "gpuPassP50Ms")
    $g95 = [double](Get-Field $motionJson "gpuPassP95Ms")
    Add-GateCheck "quality:gpu-timing" (($g50 -gt 0) -and ($g95 -ge $g50)) "p50=$g50 ms p95=$g95 ms"

    # Hash binding: independent recompute of exe + runtime identity.
    $exeReported = [string](Get-Field $motionJson "exeHash")
    $exeActual = (Get-FileHash -LiteralPath $runnerExe -Algorithm SHA256).Hash
    $rtReported = [string](Get-Field $motionJson "runtimeHash")
    $rtOk = ($rtReported -eq $runtimeHashExpected)
    $inHash = [string](Get-Field $motionJson "inputHash")
    $cfgHash = [string](Get-Field $motionJson "configHash")
    $runIdReported = [string](Get-Field $motionJson "runId")
    Add-GateCheck "quality:hash-binding" (($exeReported -eq $exeActual) -and $rtOk -and ($inHash -match "^[0-9A-Fa-f]{64}$") -and ($cfgHash -match "^[0-9A-Fa-f]{64}$") -and ($runIdReported -like ($runId + "*"))) "exe match/runId match/input+config sha256/runtime match=$rtOk"
}
else {
    foreach ($n in @("quality:extent-matrix", "quality:nr-per-frame", "quality:nvof-nonzero-motion",
                     "quality:confidence-stats", "quality:reset-contract", "quality:gpu-timing", "quality:hash-binding")) {
        Add-GateCheck $n $false "not available: motion-mode run JSON missing (runner/library/corpus gap)"
    }
}

# ---------------------------------------------------------------------------
# 5. Depth provider: current-run JSON only; manifest can never satisfy this.
# ---------------------------------------------------------------------------
$autoJson = $null
$autoPath = Join-Path $logDir "quality-auto.json"
if (Test-Path -LiteralPath $autoPath -PathType Leaf) {
    try { $autoJson = Get-Content -Raw -Encoding UTF8 -LiteralPath $autoPath | ConvertFrom-Json } catch { $autoJson = $null }
}
if ($null -ne $autoJson) {
    $depthMode = [string](Get-Field $autoJson "depthMode")
    $depthReason = [string](Get-Field $autoJson "depthFallbackReason")
    $depthAge = Get-Field $autoJson "depthAgeP95"
    $modeOk = ($depthMode -eq "dav2") -or ($depthMode -eq "motion-only-auto") -or ($depthMode -eq "disabled")
    $reasonOk = $true
    if ($depthMode -ne "dav2") { $reasonOk = (-not [string]::IsNullOrWhiteSpace($depthReason)) }
    $ageOk = $true
    if ($depthMode -eq "dav2") { $ageOk = (($null -ne $depthAge) -and ([double]$depthAge -le 8)) }
    Add-GateCheck "depth:provider-from-run" ($modeOk -and $reasonOk -and $ageOk) "mode=$depthMode reason=$depthReason ageP95=$depthAge"
}
else {
    Add-GateCheck "depth:provider-from-run" $false "not available: auto-mode run JSON missing (manifest-only depth is not accepted by design)"
}

# ---------------------------------------------------------------------------
# 6. Endurance: 1080p60 and native-4K60, 30 minutes each, via shared graph.
#    Extents must come from this run's JSON; never from filename or a 1080p rerun.
# ---------------------------------------------------------------------------
$translationHd = $null
$translationUhd = $null
if ($corpusComplete) {
    $translationHd = @($corpusEntries | Where-Object { [string](Get-Field $_ "scenario") -eq "translation" -and [int](Get-Field $_ "width") -eq 1920 })[0]
    $translationUhd = @($corpusEntries | Where-Object { [string](Get-Field $_ "scenario") -eq "translation" -and [int](Get-Field $_ "width") -eq 3840 })[0]
}
$enduranceJsons = @{}
if ($runnerPresent -and ($null -ne $translationHd)) {
    $hdPath = [string](Get-Field $translationHd "path")
    if (-not [System.IO.Path]::IsPathRooted($hdPath)) { $hdPath = Join-Path $corpusManifestDir $hdPath }
    $ej = Join-Path $logDir "endurance-1080p60.json"
    Write-Host "[INFO] endurance:1080p60 running (30 minutes, shared graph)..."
    & $runnerExe --input $hdPath --duration-seconds 1800 --run-id ($runId + "-end-1080p60") --log-file (Join-Path $logDir "endurance-1080p60.log") --json-file $ej
    Add-GateCheck "endurance:1080p60-run" ($LASTEXITCODE -eq 0) "exitCode=$LASTEXITCODE"
    if (Test-Path -LiteralPath $ej -PathType Leaf) { $enduranceJsons["hd"] = Get-Content -Raw -Encoding UTF8 -LiteralPath $ej | ConvertFrom-Json }
}
else {
    Add-GateCheck "endurance:1080p60-run" $false "not run: runner present=$runnerPresent corpus translation 1080p present=$($null -ne $translationHd)"
}

if ($runnerPresent -and ($null -ne $translationUhd)) {
    $uhdPath = [string](Get-Field $translationUhd "path")
    if (-not [System.IO.Path]::IsPathRooted($uhdPath)) { $uhdPath = Join-Path $corpusManifestDir $uhdPath }
    $ej4 = Join-Path $logDir "endurance-4k60.json"
    Write-Host "[INFO] endurance:4k60-native running (30 minutes, shared graph)..."
    & $runnerExe --input $uhdPath --duration-seconds 1800 --run-id ($runId + "-end-4k60") --log-file (Join-Path $logDir "endurance-4k60.log") --json-file $ej4
    Add-GateCheck "endurance:4k60-run" ($LASTEXITCODE -eq 0) "exitCode=$LASTEXITCODE"
    if (Test-Path -LiteralPath $ej4 -PathType Leaf) { $enduranceJsons["uhd"] = Get-Content -Raw -Encoding UTF8 -LiteralPath $ej4 | ConvertFrom-Json }
}
else {
    Add-GateCheck "endurance:4k60-run" $false "not run: runner present=$runnerPresent corpus translation 4K present=$($null -ne $translationUhd)"
}

foreach ($pair in @(@("hd", "endurance:1080p60", 1920, 1080), @("uhd", "endurance:4k60-native", 3840, 2160))) {
    $key = $pair[0]; $name = $pair[1]; $expW = $pair[2]; $expH = $pair[3]
    $j = $enduranceJsons[$key]
    if ($null -eq $j) {
        foreach ($n in @(($name + "-duration"), ($name + "-extent"), ($name + "-throughput"), ($name + "-resources"))) {
            Add-GateCheck $n $false "not available: run JSON missing"
        }
        continue
    }
    $dur = [double](Get-Field $j "durationSeconds")
    Add-GateCheck ($name + "-duration") ($dur -ge 1790) "duration=${dur}s (bar: 1800s, tolerance 1790s)"
    $sw = Get-Field (Get-Field $j "sourceExtent") "width"
    $sh = Get-Field (Get-Field $j "sourceExtent") "height"
    $ww = Get-Field (Get-Field $j "workingExtent") "width"
    $wh = Get-Field (Get-Field $j "workingExtent") "height"
    Add-GateCheck ($name + "-extent") (($sw -eq $expW) -and ($sh -eq $expH) -and ($ww -eq $expW) -and ($wh -eq $expH)) "source=${sw}x${sh} working=${ww}x${wh} (native pass-through expected)"
    $proc = [int64](Get-Field $j "processedFrames")
    $nr = [int64](Get-Field $j "nrEvaluateCount")
    Add-GateCheck ($name + "-throughput") (($proc -ge 97000) -and ($nr -eq $proc)) "processed=$proc nr=$nr (>=97000 frames in 30min at 60fps)"
    $wsStart = [double](Get-Field $j "workingSetStartMiB")
    $wsEnd = [double](Get-Field $j "workingSetEndMiB")
    $vram = [double](Get-Field $j "vramBudgetHeadroomMiB")
    $queue = [int64](Get-Field $j "queueHighWater")
    Add-GateCheck ($name + "-resources") ((($wsEnd - $wsStart) -lt 512) -and ($vram -ge 1536) -and ($queue -le 6)) "wsGrowth=" + [math]::Round($wsEnd - $wsStart, 1) + "MiB vramHeadroom=$vram MiB queueHighWater=$queue"
}

# Main-path discipline (from whichever endurance JSON exists; motion JSON used as fallback).
$discJson = $enduranceJsons["uhd"]; if ($null -eq $discJson) { $discJson = $enduranceJsons["hd"] }; if ($null -eq $discJson) { $discJson = $motionJson }
if ($null -ne $discJson) {
    $readback = [int64](Get-Field $discJson "normalPathReadbackCount")
    $cpuWaits = [double](Get-Field $discJson "cpuFenceWaitPerFrame")
    $removed = [int64](Get-Field $discJson "deviceRemovedCount")
    Add-GateCheck "discipline:main-path" (($readback -eq 0) -and ($cpuWaits -eq 0) -and ($removed -eq 0)) "readback=$readback cpuWaitsPerFrame=$cpuWaits deviceRemoved=$removed"
}
else {
    Add-GateCheck "discipline:main-path" $false "not available: no endurance or motion JSON"
}

# ---------------------------------------------------------------------------
# 7. Proprietary paths remain ignored.
# ---------------------------------------------------------------------------
$ignoreTargets = @("nvngx_dlssnr.dll", "renodx-dlss5-1.addon64",
    "runtime_local/.veyra-ignore-probe", "third_party_local/.veyra-ignore-probe",
    "captures/.veyra-ignore-probe", "logs/.veyra-ignore-probe")
foreach ($target in $ignoreTargets) {
    & git -C $Root check-ignore -q --no-index -- $target
    Add-GateCheck ("git-ignore:{0}" -f $target) ($LASTEXITCODE -eq 0) "exitCode=$LASTEXITCODE"
}

Write-Host ("Phase 5 gate run-id {0}; logs: {1}" -f $runId, $logDir)
Test-GateEnd
Write-Host ("VEYRA PHASE 5 GATE PASSED: {0} checks (run-id {1})." -f $checkCount, $runId)
exit 0
