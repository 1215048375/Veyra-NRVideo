[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$Root)

# Veyra Phase 2 gate: RenoDX-equivalent parity codec (Playbook section 16
# Phase 2). Encodes every completion threshold:
#   - CPU golden tests (ParityCpuReference) pass in both configurations;
#   - GPU encode vs CPU encode within 1 code value per channel;
#   - GPU decode vs CPU decode within 0.002 abs (FP16) with no NaN/Inf;
#   - four real stage captures exist (00_original rgba16f + json,
#     01_proxy png, 02_raw_dlssnr png, 03_final rgba16f + preview + json);
#   - neutral V1 baseline (1.0/1.0/1.0) + addon hash + capture hashes recorded;
#   - Raw and Final are distinct and individually switchable (different hashes);
#   - proprietary paths remain ignored.
# Fail-closed: missing command/file/field/threshold breach fails.

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
        Write-Host ("VEYRA PHASE 2 GATE FAILED: {0} of {1} checks failed." -f $script:failures.Count, $script:checkCount)
        Write-Host ("Failed checks: {0}" -f ($script:failures -join ", "))
        exit 1
    }
}

$runId = [guid]::NewGuid().ToString("N")
$gateStart = Get-Date
Write-Host ("Phase 2 gate run-id: {0}" -f $runId)

# ---------------------------------------------------------------------------
# 1. Required project inputs
# ---------------------------------------------------------------------------
$requiredInputs = @(
    "shaders\ParityEncode.hlsl",
    "shaders\ParityDecode.hlsl",
    "src\parity\RenoDxParityCodec.cpp",
    "tests\unit\ParityCpuReference.cpp",
    "config\parity-default.json",
    "scripts\build.ps1"
)
foreach ($relative in $requiredInputs) {
    $path = Join-Path $Root $relative
    Add-GateCheck ("input:{0}" -f $relative) (Test-Path -LiteralPath $path -PathType Leaf) "present"
}
Test-GateEnd

# ---------------------------------------------------------------------------
# 2. Build both configurations
# ---------------------------------------------------------------------------
$buildScript = Join-Path $Root "scripts\build.ps1"
$shell = (Get-Process -Id $PID).Path

& $shell -NoProfile -ExecutionPolicy Bypass -File $buildScript -Root $Root -Preset x64-debug
Add-GateCheck "build:x64-debug" ($LASTEXITCODE -eq 0) ("exitCode={0}" -f $LASTEXITCODE)
& $shell -NoProfile -ExecutionPolicy Bypass -File $buildScript -Root $Root -Preset x64-release
Add-GateCheck "build:x64-release" ($LASTEXITCODE -eq 0) ("exitCode={0}" -f $LASTEXITCODE)
Test-GateEnd

# ---------------------------------------------------------------------------
# 3. CPU golden tests in both configurations
# ---------------------------------------------------------------------------
$cpuTestsDebug = Join-Path $Root "out\build\x64-debug\veyra_parity_tests.exe"
$cpuTestsRelease = Join-Path $Root "out\build\x64-release\veyra_parity_tests.exe"
Add-GateCheck "parity-tests:exe-debug" (Test-Path -LiteralPath $cpuTestsDebug -PathType Leaf) ("path={0}" -f $cpuTestsDebug)
Add-GateCheck "parity-tests:exe-release" (Test-Path -LiteralPath $cpuTestsRelease -PathType Leaf) ("path={0}" -f $cpuTestsRelease)
Test-GateEnd

$cpuDebugOutput = (& $cpuTestsDebug 2>&1 | Out-String)
Add-GateCheck "parity-tests:run-debug" ($LASTEXITCODE -eq 0 -and $cpuDebugOutput.Contains("0 failures")) ("exitCode={0}" -f $LASTEXITCODE)
$cpuReleaseOutput = (& $cpuTestsRelease 2>&1 | Out-String)
Add-GateCheck "parity-tests:run-release" ($LASTEXITCODE -eq 0 -and $cpuReleaseOutput.Contains("0 failures")) ("exitCode={0}" -f $LASTEXITCODE)
Test-GateEnd

# ---------------------------------------------------------------------------
# 4. GPU parity comparison run (harness --parity-compare) with fresh run-id
# ---------------------------------------------------------------------------
$logDir = Join-Path $Root ("logs\phase2\" + $runId)
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
$captureDir = Join-Path $Root ("captures\phase2\" + $runId)
New-Item -ItemType Directory -Force -Path $captureDir | Out-Null

$harnessExe = Join-Path $Root "out\build\x64-release\veyra_nr_harness.exe"
Add-GateCheck "harness:exe-release" (Test-Path -LiteralPath $harnessExe -PathType Leaf) ("path={0}" -f $harnessExe)
Test-GateEnd

$runtimeDir = Join-Path $Root "runtime_local\nvidia"
$profilePath = Join-Path $Root "config\parity-default.json"
$parityLog = Join-Path $logDir "parity-release.log"
$parityJson = Join-Path $logDir "parity-release.json"

& $harnessExe --parity-compare --runtime-dir $runtimeDir --width 1920 --height 1080 --frames 16 --guidance zero --profile $profilePath --capture-dir $captureDir --run-id $runId --log-file $parityLog --json-file $parityJson
$parityExit = $LASTEXITCODE
Add-GateCheck "parity:run-release" ($parityExit -eq 0) ("exitCode={0}" -f $parityExit)
Add-GateCheck "parity:json-exists" (Test-Path -LiteralPath $parityJson -PathType Leaf) ("path={0}" -f $parityJson)
Test-GateEnd

$summary = $null
try {
    $summary = Get-Content -Raw -Encoding UTF8 -LiteralPath $parityJson | ConvertFrom-Json
}
catch {
    Add-GateCheck "parity:json-parse" $false $_.Exception.Message
    Test-GateEnd
}

Add-GateCheck "parity:run-id" ([string]$summary.runId -eq $runId) ("actual={0}" -f $summary.runId)

# 4a. GPU encode vs CPU encode: max channel delta in 8-bit code values.
$encodeMaxCode = [double]$summary.encode.maxCodeDelta
Add-GateCheck "parity:gpu-vs-cpu-encode-1code" ($encodeMaxCode -le 1.0) ("maxCodeDelta={0}" -f $encodeMaxCode)

# 4b. GPU decode vs CPU decode: max abs error, FP16 target, no NaN/Inf.
$decodeMaxAbs = [double]$summary.decode.maxAbsError
$decodeNan = [int64]$summary.decode.nanInfCount
Add-GateCheck "parity:gpu-vs-cpu-decode-0.002" (($decodeMaxAbs -le 0.002) -and ($decodeNan -eq 0)) ("maxAbsError={0} nanInf={1}" -f $decodeMaxAbs, $decodeNan)

# 4c. Neutral baseline + addon identity + capture hashes recorded.
$baseline = $summary.baseline
$addonHash = (Get-FileHash -LiteralPath (Join-Path $Root "renodx-dlss5-1.addon64") -Algorithm SHA256).Hash.ToUpperInvariant()
$baselineOk = ([double]$baseline.paperWhiteScale -eq 1.0) -and
    ([double]$baseline.transferStrength -eq 1.0) -and
    ([double]$baseline.colorStrength -eq 1.0) -and
    ([string]$baseline.addonSha256 -ceq $addonHash)
Add-GateCheck "parity:neutral-baseline-and-addon-hash" $baselineOk ("paperWhiteScale={0} transferStrength={1} colorStrength={2} addonHashMatch={3}" -f $baseline.paperWhiteScale, $baseline.transferStrength, $baseline.colorStrength, ($baseline.addonSha256 -ceq $addonHash))

# 4d. Four-stage captures exist and are non-trivial; Raw and Final distinct.
$stages = @(
    @{ File = "00_original.rgba16f.bin"; MinBytes = 1000 },
    @{ File = "00_original.json"; MinBytes = 50 },
    @{ File = "01_proxy.png"; MinBytes = 1000 },
    @{ File = "02_raw_dlssnr.png"; MinBytes = 1000 },
    @{ File = "03_final.rgba16f.bin"; MinBytes = 1000 },
    @{ File = "03_final-preview.png"; MinBytes = 1000 },
    @{ File = "03_final.json"; MinBytes = 50 }
)
foreach ($stage in $stages) {
    $path = Join-Path $captureDir $stage.File
    $exists = Test-Path -LiteralPath $path -PathType Leaf
    $sizeOk = $false
    if ($exists) {
        $sizeOk = (Get-Item -LiteralPath $path).Length -ge $stage.MinBytes
    }
    Add-GateCheck ("capture:{0}" -f $stage.File) ($exists -and $sizeOk) ("bytes={0}" -f $(if ($exists) { (Get-Item -LiteralPath $path).Length } else { 0 }))
}

$rawSha = [string]$summary.captureHashes.rawDlssnr
$finalSha = [string]$summary.captureHashes.final
Add-GateCheck "parity:raw-and-final-distinct" (($rawSha.Length -eq 64) -and ($finalSha.Length -eq 64) -and ($rawSha -cne $finalSha)) ("raw={0} final={1}" -f $rawSha.Substring(0, [Math]::Min(12, $rawSha.Length)), $finalSha.Substring(0, [Math]::Min(12, $finalSha.Length)))
Test-GateEnd

# ---------------------------------------------------------------------------
# 5. Proprietary paths remain ignored
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
    Add-GateCheck ("git-ignore:{0}" -f $target) ($LASTEXITCODE -eq 0) ("exitCode={0}" -f $LASTEXITCODE)
}
$trackedSensitive = @(& git -C $Root ls-files -- "nvngx_dlssnr.dll" "renodx-dlss5-1.addon64" "runtime_local" "third_party_local" "reference_local" "captures" "logs")
Add-GateCheck "git-sensitive-untracked" ($trackedSensitive.Count -eq 0) $(if ($trackedSensitive.Count -eq 0) { "none tracked" } else { $trackedSensitive -join ", " })

# ---------------------------------------------------------------------------
# Result
# ---------------------------------------------------------------------------
$elapsed = ((Get-Date) - $gateStart).TotalSeconds
Write-Host ("Phase 2 gate run-id {0} finished in {1:N1} s; logs: {2}" -f $runId, $elapsed, $logDir)
Test-GateEnd
Write-Host ("VEYRA PHASE 2 GATE PASSED: {0} checks (run-id {1})." -f $checkCount, $runId)
exit 0
