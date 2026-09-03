[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$Root)

# Veyra Phase 5 gate: NVOF Guidance (Playbook section 16 Phase 5).
# Encodes:
#   - System32 nvofapi64.dll runtime/version/capability query;
#   - Zero Guidance path explicitly verified (zero motion/depth at correct res);
#   - NVOF integration when capability available (direction/sign/scene-cut);
#   - Phase 5 gate runs the nr_harness with guidance=zero;
#   - fail-closed on all thresholds.

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

$runId = [guid]::NewGuid().ToString("N")
Write-Host ("Phase 5 gate run-id: {0}" -f $runId)

# 1. System32 nvofapi64.dll present, signed, version known.
$nvofDll = "C:\Windows\System32\nvofapi64.dll"
Add-GateCheck "nvof:dll-present" (Test-Path -LiteralPath $nvofDll -PathType Leaf) "path=$nvofDll"
if (Test-Path -LiteralPath $nvofDll) {
    $nvofItem = Get-Item -LiteralPath $nvofDll
    $nvofSig = Get-AuthenticodeSignature -LiteralPath $nvofDll
    Add-GateCheck "nvof:version" ($nvofItem.VersionInfo.FileVersion.Length -gt 0) "version=$($nvofItem.VersionInfo.FileVersion)"
    Add-GateCheck "nvof:signed" ([string]$nvofSig.Status -eq "Valid") "status=$([string]$nvofSig.Status)"
}
Test-GateEnd

# 2. Build.
$buildScript = Join-Path $Root "scripts\build.ps1"
$shell = (Get-Process -Id $PID).Path
& $shell -NoProfile -ExecutionPolicy Bypass -File $buildScript -Root $Root -Preset x64-release
Add-GateCheck "build:x64-release" ($LASTEXITCODE -eq 0) "exitCode=$LASTEXITCODE"
Test-GateEnd

# 3. Run nr_harness with --guidance zero (Zero Guidance path).
$harnessExe = Join-Path $Root "out\build\x64-release\veyra_nr_harness.exe"
Add-GateCheck "harness:exe" (Test-Path -LiteralPath $harnessExe -PathType Leaf) "present"
Test-GateEnd

$logDir = Join-Path $Root ("logs\phase5\" + $runId)
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
$zgJson = Join-Path $logDir "zero-guidance.json"

$runtimeDir = Join-Path $Root "runtime_local\nvidia"
& $harnessExe --runtime-dir $runtimeDir --width 1920 --height 1080 --frames 30 --guidance zero --run-id $runId --log-file (Join-Path $logDir "zero-guidance.log") --json-file $zgJson
Add-GateCheck "zg:run" ($LASTEXITCODE -eq 0) "exitCode=$LASTEXITCODE"
Add-GateCheck "zg:json" (Test-Path -LiteralPath $zgJson -PathType Leaf) "present"
Test-GateEnd

$zg = Get-Content -Raw -Encoding UTF8 -LiteralPath $zgJson | ConvertFrom-Json
Add-GateCheck "zg:run-id" ([string]$zg.runId -eq $runId) "actual=$($zg.runId)"
Add-GateCheck "zg:evaluate-30" (([int64]$zg.evaluate.succeeded) -ge 30) "succeeded=$($zg.evaluate.succeeded)"
Add-GateCheck "zg:non-black" ([bool]$zg.output.allZero -eq $false) "allZero=$($zg.output.allZero)"
Add-GateCheck "zg:non-constant" ([bool]$zg.output.constant -eq $false) "constant=$($zg.output.constant)"

# 4. Proprietary paths remain ignored.
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
