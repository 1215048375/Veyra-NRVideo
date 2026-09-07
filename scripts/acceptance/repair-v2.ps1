[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Root,
      [ValidateRange(1,300)][double]$MaxRuntimeSeconds=300,
      [ValidateSet('contracts','shaders','sr4k','mfg3','mfg4','fg2','fg2nr','player2','player3','player4','export4','exportaudio','settings','presets','flowP','flowQ','ui','parameters','rollback','rollbackflow','uicases','native4k','realtime4k','exporthevc','exportmkv','exportvfr','cancel','all')][string]$Case='all')
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$Root=(Resolve-Path -LiteralPath $Root).Path
# User correction 2026-09-08: limit applies to each invocation, not lifetime.
$invocationTimer=[Diagnostics.Stopwatch]::StartNew()
if($Case -eq 'all'){
    $combined=[guid]::NewGuid().ToString('N');$summaryDir=Join-Path $Root ('logs/repair-v2/joint-'+$combined);New-Item -ItemType Directory -Force -Path $summaryDir|Out-Null
    $results=[Collections.Generic.List[object]]::new()
    foreach($caseName in @('contracts','presets','shaders','parameters','fg2nr','sr4k','mfg3','mfg4','player3','settings','uicases','rollback','rollbackflow','exportaudio','exporthevc','exportmkv','exportvfr','cancel')){
        $ledger=Join-Path $Root 'logs/repair-v2/runtime-budget.json'
        $remainingInvocation=$MaxRuntimeSeconds-$invocationTimer.Elapsed.TotalSeconds
        if($remainingInvocation -lt 1){break}
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath -Root $Root -MaxRuntimeSeconds $remainingInvocation -Case $caseName
        $exitCode=$LASTEXITCODE;$entry=Get-Content $ledger -Raw|ConvertFrom-Json
        $results.Add([ordered]@{case=$caseName;exitCode=$exitCode;runId=$entry.lastRunId})
        if($exitCode -ne 0){break}
    }
    $passed=$results.Count -eq 18 -and @($results|Where-Object exitCode -ne 0).Count -eq 0
    [ordered]@{status='needs_review';functionalChecksPassed=$passed;cases=$results;runtimeLedger=(Get-Content $ledger -Raw|ConvertFrom-Json);limits=@('capture reserved for user','physical scanout/photon latency unmeasured','short test only','skin and UI correction effect unproven on synthetic fixture')}|ConvertTo-Json -Depth 9|Set-Content (Join-Path $summaryDir 'result.json')
    Write-Host "Joint evidence=$summaryDir functionalChecksPassed=$passed"
    if($passed){exit 0}else{exit 1}
}
$base=Join-Path $Root 'logs/repair-v2'
New-Item -ItemType Directory -Force -Path $base | Out-Null
$ledgerLock=[IO.File]::Open((Join-Path $base 'runtime-budget.lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
$ledgerPath=Join-Path $base 'runtime-budget.json'
$spent=0.0
if(Test-Path -LiteralPath $ledgerPath){$spent=[double](Get-Content -LiteralPath $ledgerPath -Raw|ConvertFrom-Json).spentSeconds}
$runId=[guid]::NewGuid().ToString('N')
$runDir=Join-Path $base $runId
New-Item -ItemType Directory -Path $runDir | Out-Null
$items=[Collections.Generic.List[object]]::new()
function Run-Check([string]$name,[string]$exe,[string]$arguments){
    $remaining=$MaxRuntimeSeconds-$invocationTimer.Elapsed.TotalSeconds
    if($remaining -le 0){throw 'This test invocation reached its runtime limit'}
    $stdout=Join-Path $runDir ($name+'.stdout.log');$stderr=Join-Path $runDir ($name+'.stderr.log')
    $before=$script:spent
    # Keep cumulative history for reporting; it is not a stop threshold.
    # Reserve before launch so an interrupted process remains visible.
    [ordered]@{spentSeconds=$before+$remaining;perInvocationLimitSeconds=$MaxRuntimeSeconds;budgetScope='per-invocation';activeRun=$runId;reservedSeconds=$remaining}|ConvertTo-Json|Set-Content -LiteralPath $ledgerPath
    $timer=[Diagnostics.Stopwatch]::StartNew();$p=$null;$code=125
    $processArgs=@{FilePath=$exe;WorkingDirectory=$Root;WindowStyle=$(if($Case -eq 'ui'){'Normal'}else{'Hidden'});PassThru=$true;RedirectStandardOutput=$stdout;RedirectStandardError=$stderr}
    if($arguments){$processArgs.ArgumentList=$arguments}
    try {
        $p=Start-Process @processArgs
        $processHandle=$p.Handle # Keep native handle until ExitCode is read.
        $timedOut=-not $p.WaitForExit([int][Math]::Floor([Math]::Max(1,$remaining*1000-$timer.Elapsed.TotalMilliseconds)))
        if($timedOut){$code=124}else{$code=$p.ExitCode}
        if($null -eq $code){$code=125}
    } finally {
        if($null -ne $p -and -not $p.HasExited){ & "$env:SystemRoot/System32/taskkill.exe" /PID $p.Id /T /F | Out-Null; $p.WaitForExit() }
        $timer.Stop();$script:spent=$before+$timer.Elapsed.TotalSeconds
        [ordered]@{spentSeconds=$script:spent;perInvocationLimitSeconds=$MaxRuntimeSeconds;budgetScope='per-invocation';lastRunId=$runId}|ConvertTo-Json|Set-Content -LiteralPath $ledgerPath
    }
    $items.Add([ordered]@{case=$name;command=$exe+' '+$arguments;exitCode=$code;seconds=$timer.Elapsed.TotalSeconds;stdout=$stdout;stderr=$stderr;exeSha256=(Get-FileHash -LiteralPath $exe).Hash})
}
try {
    if($Case -in @('all','parameters')){Run-Check 'parameters' (Join-Path $Root 'out/build/x64-release/veyra_repair_parameter_tests.exe') ''}
    if($Case -in @('all','presets')){Run-Check 'presets' (Join-Path $Root 'out/build/x64-release/veyra_repair_preset_tests.exe') ('"'+(Join-Path $runDir 'presets.v1')+'"')}
    if($Case -in @('all','contracts')){Run-Check 'contracts' (Join-Path $Root 'out/build/x64-release/veyra_repair_contract_tests.exe') ''}
    if($Case -in @('all','shaders')){Run-Check 'shaders' (Join-Path $Root 'out/build/x64-release/veyra_repair_shader_tests.exe') ''}
    if($Case -in @('flowP','flowQ')){Run-Check $Case (Join-Path $Root 'out/build/x64-release/veyra_repair_fg_tests.exe') $Case}
    if($Case -in @('all','fg2')){Run-Check 'fg2-product-content' (Join-Path $Root 'out/build/x64-release/veyra_repair_fg_tests.exe') ''}
    if($Case -in @('all','fg2nr')){Run-Check 'fg2-nr-product-content' (Join-Path $Root 'out/build/x64-release/veyra_repair_fg_tests.exe') 'nr'}
    if($Case -in @('all','sr4k')){Run-Check 'sr4k-product-content' (Join-Path $Root 'out/build/x64-release/veyra_repair_fg_tests.exe') 'sr'}
    if($Case -in @('all','mfg3')){Run-Check 'mfg3-product-content' (Join-Path $Root 'out/build/x64-release/veyra_repair_fg_tests.exe') 'mfg3'}
    if($Case -in @('all','mfg4')){Run-Check 'mfg4-product-content' (Join-Path $Root 'out/build/x64-release/veyra_repair_fg_tests.exe') 'mfg4'}
    if($Case -in @('all','player2','player3','player4','export4','exportaudio','settings','ui','rollback','rollbackflow','uicases','native4k','realtime4k','exporthevc','exportmkv','exportvfr','cancel')){
        $clip=Join-Path $Root 'loop/local/fixed_clips/corpus/translation_1080p60.mp4'
        if((Get-FileHash -LiteralPath $clip).Hash -ne '62BA4375DA00C9B14ECE97485AE2C06223401AE873EF0CAD8520B736A5D4D236'){throw 'Synthetic clip identity mismatch'}
        $oldLog=$env:VEYRA_LOG_FILE;$oldFault=$env:VEYRA_TEST_REJECT_NR_STYLE2;$oldFlowFault=$env:VEYRA_TEST_REJECT_NR_DISABLE
        try {
            $env:VEYRA_LOG_FILE=Join-Path $runDir 'player-app.log'
            $mult=if($Case -eq 'player3'){3}elseif($Case -in @('player4','export4','exportaudio','settings','ui','rollback','rollbackflow','uicases','native4k','realtime4k','exporthevc','exportmkv','exportvfr','cancel')){4}else{2}
            if($Case -eq 'exportaudio'){
                $audioClip=Join-Path $runDir 'synthetic-audio.mp4'
                Run-Check 'create-synthetic-audio' (Get-Command ffmpeg).Source ('-nostdin -v error -i "'+$clip+'" -f lavfi -i "sine=frequency=440:sample_rate=48000:duration=0.4" -t 0.4 -c:v copy -c:a aac "'+$audioClip+'"')
                if($items[$items.Count-1].exitCode -ne 0){throw 'Synthetic audio creation failed'}
                $clip=$audioClip
            }
            if($Case -in @('exportmkv','exportvfr')){
                $quantizedClip=Join-Path $runDir 'quantized.mkv'
                if($Case -eq 'exportmkv'){$makeArgs='-nostdin -v error -i "'+$clip+'" -t 0.4 -an -c:v copy "'+$quantizedClip+'"'}
                else {$makeArgs='-nostdin -v error -i "'+$clip+'" -an -vf "setpts=PTS+gte(N\,6)/(30*TB)" -frames:v 24 -fps_mode passthrough -c:v libx264 -preset ultrafast "'+$quantizedClip+'"'}
                Run-Check 'create-quantized-fixture' (Get-Command ffmpeg).Source $makeArgs
                if($items[$items.Count-1].exitCode -ne 0){throw 'Quantized fixture creation failed'}
                $clip=$quantizedClip
            }
            if($Case -in @('native4k','realtime4k')){
                $clip=Join-Path $Root 'loop/local/fixed_clips/corpus/translation_4k60.mp4'
                if((Get-FileHash $clip).Hash -ne '0470A34BA62FF994DC6806FF8E92DA1FE38E388EF527692C4591D4E5AE9AEACA'){throw '4K synthetic identity mismatch'}
            }
            $args='"'+$clip+'" --nr --no-sr --fg-multiplier '+$mult
            if($Case -eq 'native4k'){$args+=' --native --no-fg'}
            if($Case -eq 'realtime4k'){$args+=' --realtime'}
            if($Case -eq 'rollback'){$env:VEYRA_TEST_REJECT_NR_STYLE2='1';$args+=' --smoke-rollback'}
            if($Case -eq 'rollbackflow'){$env:VEYRA_TEST_REJECT_NR_DISABLE='1';$args+=' --no-fg --smoke-rollback-flow'}
            if($Case -eq 'uicases'){$args+=' --smoke-ui'}
            if($Case -eq 'exporthevc'){$args+=' --hevc'}
            if($Case -eq 'cancel'){$args+=' --cancel-after-ms 1500'}
            if($Case -in @('export4','exportaudio','exporthevc','exportmkv','exportvfr','cancel')){$args+=' --no-nr --max-frames 24 --export-out "'+(Join-Path $runDir 'export4.mp4')+'"'}else{$args+= $(if($Case -eq 'ui'){' --smoke-seconds 35'}elseif($Case -eq 'settings'){' --smoke-seconds 8 --smoke-settings'}elseif($Case -in @('rollback','rollbackflow','uicases')){' --smoke-seconds 8'}else{' --smoke-seconds 6 --smoke-controls'})}
            Run-Check $Case (Join-Path $Root 'out/build/x64-release/veyra.exe') $args
            if($Case -in @('export4','exportaudio','exporthevc','exportmkv') -and $items[$items.Count-1].exitCode -eq 0){
                Run-Check 'export-probe' (Get-Command ffprobe).Source ('-v error -show_streams -show_packets -of json "'+(Join-Path $runDir 'export4.mp4')+'"')
                $probe=Get-Content (Join-Path $runDir 'export-probe.stdout.log') -Raw|ConvertFrom-Json
                $v=@($probe.streams|Where-Object codec_type -eq 'video')[0]
                $packets=@($probe.packets|Where-Object stream_index -eq $v.index)
                $ok=$packets.Count -eq 96 -and $v.avg_frame_rate -eq '240/1'
                for($i=0;$i -lt $packets.Count;$i++){if([Math]::Abs([double]$packets[$i].pts_time-$i/240.0) -gt 0.00001){$ok=$false}}
                if($Case -eq 'exportaudio'){$ok=$ok -and @($probe.streams|Where-Object codec_type -eq 'audio').Count -eq 1}
                $items.Add([ordered]@{case='CFR-packets-audio';command='validate ffprobe packet PTS and stream count';exitCode=$(if($ok){0}else{1});seconds=0;evidence=(Join-Path $runDir 'export-probe.stdout.log')})
            }
            if($Case -eq 'exportvfr'){
                $ok=$items[$items.Count-1].exitCode -eq 1 -and (Select-String -Path $env:VEYRA_LOG_FILE -Pattern 'CFR rejected source=' -Quiet) -and -not (Test-Path (Join-Path $runDir 'export4.mp4'))
                $items.Add([ordered]@{case='VFR-rejected';command='require export exit1, explicit CFR rejection and no final output';exitCode=$(if($ok){0}else{1});seconds=0})
            }
        }
        finally {$env:VEYRA_LOG_FILE=$oldLog;$env:VEYRA_TEST_REJECT_NR_STYLE2=$oldFault;$env:VEYRA_TEST_REJECT_NR_DISABLE=$oldFlowFault}
    }
} finally {
    $failed=@($items|Where-Object {if($_.case -eq 'cancel'){$_.exitCode -ne 3}elseif($_.case -eq 'exportvfr'){$_.exitCode -ne 1}else{$_.exitCode -ne 0}}).Count
    [ordered]@{runId=$runId;status='needs_review';scope=$Case;checks=$items;failedChecks=$failed;runtimeSpentSeconds=$spent;perInvocationLimitSeconds=$MaxRuntimeSeconds;budgetScope='per-invocation';unexecuted=@('capture hardware acceptance','physical scanout/photon latency','multi-monitor physical DPI traversal','long-run stability');capture='reserved for user';displayScan='unmeasured';photonLatency='unmeasured'}|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $runDir 'result.json')
    Write-Host "repair-v2 run=$runId spent=$spent evidence=$runDir"
    $ledgerLock.Dispose()
}
if($failed){exit 1}
# Individual development checks do not certify the unexecuted repair contract.
if($Case -eq 'all'){exit 2}
exit 0

