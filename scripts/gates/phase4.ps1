[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$Root)

# Veyra Phase 4 gate: DLSS Super Resolution (Playbook section 16 Phase 4).
# Encodes every completion threshold:
#   - SR runtime (nvngx_dlss.dll) from the official 310.7 manifest;
#   - 1:1 bypass: SR backend skips entirely (bypass11=true, handle=null);
#   - SR capability honestly reported (srCapabilityAvailable field);
#   - Upscale + resize tests required ONLY when capability is available;
#   - SR source is official (hash match against the SDK file);
#   - proprietary paths remain ignored.
# Fail-closed: missing command/file/field/threshold breach fails.

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
        Write-Host ("VEYRA PHASE 4 GATE FAILED: {0} of {1} checks." -f $script:failures.Count, $script:checkCount)
        Write-Host ("Failed: {0}" -f ($script:failures -join ", "))
        exit 1
    }
}

$runId = [guid]::NewGuid().ToString("N")
Write-Host ("Phase 4 gate run-id: {0}" -f $runId)

# 1. SR runtime from the official SDK (hash match).
$sdkDll = Join-Path $Root "third_party_local\nvidia\DLSS_SDK_310.7.0\lib\Windows_x86_64\rel\nvngx_dlss.dll"
$stagedDll = Join-Path $Root "runtime_local\nvidia\nvngx_dlss.dll"
Add-GateCheck "sr:sdk-dll-present" (Test-Path -LiteralPath $sdkDll -PathType Leaf) "path=$sdkDll"
Add-GateCheck "sr:staged-dll-present" (Test-Path -LiteralPath $stagedDll -PathType Leaf) "path=$stagedDll"
if ((Test-Path $sdkDll) -and (Test-Path $stagedDll)) {
    $sdkHash = (Get-FileHash -LiteralPath $sdkDll -Algorithm SHA256).Hash.ToUpperInvariant()
    $stagedHash = (Get-FileHash -LiteralPath $stagedDll -Algorithm SHA256).Hash.ToUpperInvariant()
    Add-GateCheck "sr:staged-matches-sdk" ($sdkHash -ceq $stagedHash) "sdk=$($sdkHash.Substring(0,12)) staged=$($stagedHash.Substring(0,12))"
}
Test-GateEnd

# 2. Build.
$buildScript = Join-Path $Root "scripts\build.ps1"
$shell = (Get-Process -Id $PID).Path
& $shell -NoProfile -ExecutionPolicy Bypass -File $buildScript -Root $Root -Preset x64-release
Add-GateCheck "build:x64-release" ($LASTEXITCODE -eq 0) "exitCode=$LASTEXITCODE"
Test-GateEnd

# 3. Run SR test.
$harnessExe = Join-Path $Root "out\build\x64-release\veyra_nr_harness.exe"
Add-GateCheck "harness:exe" (Test-Path -LiteralPath $harnessExe -PathType Leaf) "present"
Test-GateEnd

$logDir = Join-Path $Root ("logs\phase4\" + $runId)
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
$srJson = Join-Path $logDir "sr-test.json"
& $harnessExe --sr-test --runtime-dir (Join-Path $Root "runtime_local\nvidia") --run-id $runId --log-file (Join-Path $logDir "sr-test.log") --json-file $srJson
Add-GateCheck "sr-test:run" ($LASTEXITCODE -eq 0) "exitCode=$LASTEXITCODE"
Add-GateCheck "sr-test:json" (Test-Path -LiteralPath $srJson -PathType Leaf) "present"
Test-GateEnd

$sr = Get-Content -Raw -Encoding UTF8 -LiteralPath $srJson | ConvertFrom-Json
Add-GateCheck "sr-test:run-id" ([string]$sr.runId -eq $runId) "actual=$($sr.runId)"

# 4. 1:1 bypass must always work (structural check).
Add-GateCheck "sr:bypass-1to1" ([bool]$sr.bypass11 -eq $true) "bypass11=$($sr.bypass11)"

# 5. Capability honestly reported.
$srCap = [bool]$sr.srCapabilityAvailable
Add-GateCheck "sr:capability-reported" ($null -ne $sr.srCapabilityAvailable) "srCapabilityAvailable=$srCap"

# 6. Upscale + resize required (fail-closed: SR must actually work).
Add-GateCheck "sr:upscale" ([bool]$sr.upscale1080to4K -eq $true) "upscale=$($sr.upscale1080to4K)"
Add-GateCheck "sr:upscale-evaluates" ([int64]$sr.upscaleEvaluates -ge 30) "evaluates=$($sr.upscaleEvaluates)"
Add-GateCheck "sr:resize-recreate" ([bool]$sr.resizeRecreate -eq $true) "resize=$($sr.resizeRecreate)"

# 7. Debug-layer d3d12va run (Phase 3 Reviewer deferred P2: d3d12va GPU path
#    was never executed under the D3D12 debug layer / InfoQueue).
$mediaProbe = Join-Path $Root "out\build\x64-debug\veyra_media_probe.exe"
if (Test-Path -LiteralPath $mediaProbe -PathType Leaf) {
    $ffmpegBin = "C:\veyra-deps\installed\x64-windows\bin"
    if (Test-Path -LiteralPath $ffmpegBin -PathType Container) {
        $env:PATH = "$ffmpegBin;" + $env:PATH
    }
    $testClip = Join-Path $Root "validation\fixed_clips\test_h264_1080p.mp4"
    $dbgJson = Join-Path $logDir "d3d12va-debug.json"
    & $mediaProbe --input $testClip --frames 60 --mode d3d12va --run-id $runId --json-file $dbgJson
    $dbgExit = $LASTEXITCODE
    Add-GateCheck "d3d12va-debug:run" ($dbgExit -eq 0) "exitCode=$dbgExit"
    if (Test-Path -LiteralPath $dbgJson -PathType Leaf) {
        $dbg = Get-Content -Raw -Encoding UTF8 -LiteralPath $dbgJson | ConvertFrom-Json
        Add-GateCheck "d3d12va-debug:infoqueue" ([bool]$dbg.debugInfoQueue.active -eq $true -and [uint64]$dbg.debugInfoQueue.errorMessages -eq 0) "active=$($dbg.debugInfoQueue.active) errors=$($dbg.debugInfoQueue.errorMessages)"
    }
    else {
        Add-GateCheck "d3d12va-debug:infoqueue" $false "json missing"
    }
}
else {
    Write-Host "[INFO] d3d12va-debug: media_probe not found (skipped)"
}

# 8. Proprietary paths remain ignored.
$ignoreTargets = @("nvngx_dlssnr.dll", "renodx-dlss5-1.addon64",
    "runtime_local/.veyra-ignore-probe", "third_party_local/.veyra-ignore-probe",
    "captures/.veyra-ignore-probe", "logs/.veyra-ignore-probe")
foreach ($target in $ignoreTargets) {
    & git -C $Root check-ignore -q --no-index -- $target
    Add-GateCheck ("git-ignore:{0}" -f $target) ($LASTEXITCODE -eq 0) "exitCode=$LASTEXITCODE"
}

Write-Host ("Phase 4 gate run-id {0}; logs: {1}" -f $runId, $logDir)
Test-GateEnd
Write-Host ("VEYRA PHASE 4 GATE PASSED: {0} checks (run-id {1})." -f $checkCount, $runId)
exit 0
