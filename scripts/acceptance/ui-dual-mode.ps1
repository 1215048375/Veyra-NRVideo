[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Root,
 [ValidateRange(1,300)][int]$MaxRuntimeSeconds=300,
 [ValidateSet('state','switch','pause','master','masterreject','audio','export','canceljob','exitjob','all')][string]$Case='all')
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$Root=(Resolve-Path -LiteralPath $Root).Path
$timer=[Diagnostics.Stopwatch]::StartNew()
$runId=[guid]::NewGuid().ToString('N')
$runDir=Join-Path $Root ('logs/ui-dual-mode/'+$runId)
New-Item -ItemType Directory -Path $runDir | Out-Null
$items=[Collections.Generic.List[object]]::new()
$oldLog=$env:VEYRA_LOG_FILE
$oldReject=$env:VEYRA_TEST_REJECT_NR_DISABLE
$exe=Join-Path $Root 'out/build/x64-release/veyra.exe'
$clip=Join-Path $Root 'loop/local/fixed_clips/corpus/translation_1080p60.mp4'
function Run-Check([string]$name,[string]$program,[string]$arguments){
 $remaining=$MaxRuntimeSeconds-$timer.Elapsed.TotalSeconds
 if($remaining -le 0){throw 'This invocation reached its time limit'}
 $clock=[Diagnostics.Stopwatch]::StartNew();$p=$null;$code=125
 try {
  $p=Start-Process -FilePath $program -ArgumentList $arguments -WorkingDirectory $Root -PassThru -WindowStyle Hidden -RedirectStandardOutput (Join-Path $runDir ($name+'.stdout.log')) -RedirectStandardError (Join-Path $runDir ($name+'.stderr.log'))
  $handle=$p.Handle
  if($p.WaitForExit([int][Math]::Max(1,$remaining*1000))){$code=$p.ExitCode}else{$code=124}
 } finally {if($null -ne $p -and -not $p.HasExited){& "$env:SystemRoot/System32/taskkill.exe" /PID $p.Id /T /F | Out-Null;$p.WaitForExit()};$clock.Stop()}
 $items.Add([ordered]@{name=$name;command=$program+' '+$arguments;exitCode=$code;seconds=$clock.Elapsed.TotalSeconds})
 if($code -ne 0){throw "$name failed: $code"}
}
$passed=$false;$problem=''
try {
 if((Get-FileHash -LiteralPath $clip).Hash -ne '62BA4375DA00C9B14ECE97485AE2C06223401AE873EF0CAD8520B736A5D4D236'){throw 'Synthetic media identity mismatch'}
 if($Case -in @('all','state')){Run-Check 'state' (Join-Path $Root 'out/build/x64-release/veyra_ui_contract_tests.exe') ('"'+(Join-Path $runDir 'preferences')+'"')}
 foreach($name in @('switch','pause','master','masterreject','audio','export','canceljob','exitjob')){
  if($Case -notin @('all',$name)){continue}
  $env:VEYRA_LOG_FILE=Join-Path $runDir ($name+'.app.log')
  $flags=switch($name){'switch'{'--smoke-dual'}'pause'{'--smoke-dual-pause'}'master'{'--smoke-master'}'masterreject'{'--smoke-master-reject'}'audio'{'--smoke-audio'}'export'{'--smoke-dual --smoke-job --smoke-dual-export "'+(Join-Path $runDir 'concurrent.mp4')+'"'}}
  if($name -in @('canceljob','exitjob')){$flags=$(if($name -eq 'canceljob'){'--smoke-job-cancel'}else{'--smoke-job-exit'})+' --smoke-dual-export "'+(Join-Path $runDir ($name+'.mp4'))+'"'}
  $input=$clip
  if($name -in @('audio','export','canceljob','exitjob')){
   $input=Join-Path $runDir ($name+'-audio.mp4')
   $fixtureSeconds=if($name -eq 'audio'){9}else{30}
   Run-Check ($name+'-fixture') (Get-Command ffmpeg).Source ('-nostdin -v error -stream_loop 3 -i "'+$clip+'" -f lavfi -i "sine=frequency=440:sample_rate=48000:duration='+$fixtureSeconds+'" -t '+$fixtureSeconds+' -c:v copy -c:a aac "'+$input+'"')
  }
  if($name -eq 'masterreject'){$env:VEYRA_TEST_REJECT_NR_DISABLE='1'}else{$env:VEYRA_TEST_REJECT_NR_DISABLE=$oldReject}
  # Two independent GPU contexts can take several seconds to initialize cold.
  # Keep foreground media playing through worker pause/resume; assertions and
  # the entire invocation's 300-second ceiling remain unchanged.
  $smokeSeconds=if($name -in @('export','canceljob','exitjob')){30}else{14}
  Run-Check $name $exe ('--smoke-seconds '+$smokeSeconds+' --nr --no-sr --no-fg '+$flags+' "'+$input+'"')
  $log=Get-Content -LiteralPath $env:VEYRA_LOG_FILE -Raw
  if($name -eq 'switch'){
   $lines=@($log -split '\r?\n');$first=[Array]::FindIndex($lines,[Predicate[string]]{param($line) $line.Contains('[ui-mode]')});$last=[Array]::FindLastIndex($lines,[Predicate[string]]{param($line) $line.Contains('[ui-mode]')});$interval=($lines[$first..$last] -join "`n")
   if($interval -match 'CreateFeature id=|Create DLSSG|\[source-file\] opened|reset=1'){throw 'Mode interval recreated feature, source or history'}
   $submits=[regex]::Matches($interval,'\[submit\].*epoch=(\d+).*host100ns=(\d+)')
   $epochs=@($submits|ForEach-Object{$_.Groups[1].Value}|Select-Object -Unique);$times=@($submits|ForEach-Object{[long]$_.Groups[2].Value});$maxGap=0.0
   for($n=1;$n -lt $times.Count;$n++){$maxGap=[Math]::Max($maxGap,($times[$n]-$times[$n-1])/10000.0)}
   [ordered]@{submissionCount=$times.Count;epochs=$epochs;maxSubmissionGapMs=$maxGap;noCreateResetOrSourceOpen=$true}|ConvertTo-Json|Set-Content (Join-Path $runDir 'switch-submission.json')
   if($times.Count -lt 10 -or $epochs.Count -ne 1 -or $maxGap -gt 100){throw "Switch submission continuity failed: maxGap=$maxGap epochs=$($epochs.Count)"}
  }
  if($name -in @('switch','pause','export')){
   $matches=[regex]::Matches($log,'\[ui-mode\].*commandMs=([0-9.]+)')
   if($matches.Count -ne 40 -or $log -match 'hostSame=false|sessionSame=false|revisionSame=false'){throw 'Mode identity contract failed'}
   $values=@($matches|ForEach-Object{[double]$_.Groups[1].Value}|Sort-Object)
   $p95=$values[[int][Math]::Floor(($values.Count-1)*.95)]
   [ordered]@{count=$values.Count;p95Ms=$p95;maxMs=$values[-1];allMs=$values}|ConvertTo-Json -Depth 5|Set-Content (Join-Path $runDir ($name+'.mode-times.json'))
   if($p95 -gt 16 -or $values[-1] -gt 50){throw "Mode UI command threshold failed: p95=$p95 max=$($values[-1])"}
  }
  if($name -eq 'masterreject' -and $log -notmatch 'rejected bypass restored actual enabled UI'){throw 'Rejected master transaction UI evidence missing'}
  if($name -eq 'audio' -and ($log -notmatch 'target=0.5 reached=0.5' -or $log -notmatch 'target=0 reached=0')){throw 'Actual PCM gain/mute evidence missing'}
  if($name -in @('canceljob','exitjob')){
   if($name -eq 'exitjob' -and $log -notmatch 'parent close with active worker; cooperative cancellation requested'){throw 'Active-worker parent close marker missing'}
   $pidMatch=[regex]::Match($log,'\[export-worker\] started pid=(\d+)')
   if(-not $pidMatch.Success -or (Get-Process -Id ([int]$pidMatch.Groups[1].Value) -ErrorAction SilentlyContinue)){throw 'Worker orphan or missing start evidence'}
   if((Test-Path (Join-Path $runDir ($name+'.mp4'))) -or -not (Test-Path (Join-Path $runDir ($name+'.mp4.partial')))){throw 'Cancelled output protection failed'}
   if($name -eq 'canceljob' -and $log -notmatch 'cancelled=true foregroundContinues=true'){throw 'Foreground cancellation continuity missing'}
  }
  if($name -eq 'export'){
   if($log -notmatch 'foregroundAdvanced=true' -or $log -notmatch 'success=true foregroundFrames='){throw 'Concurrent export evidence missing'}
   $workerId=[regex]::Match($log,'\[export-worker\] started pid=(\d+)').Groups[1].Value
   $workerLog=Get-Content -LiteralPath (Join-Path $Root ('logs/export-worker-'+$workerId+'.log')) -Raw
   $frozen=[regex]::Match($log,'jobFrozenRevisionIndependent=true frozenRevision=(\d+) frozenIntensity=([0-9.]+)')
   if(-not $frozen.Success -or $workerLog -notmatch ('\[export-frozen\] revision='+$frozen.Groups[1].Value+' .*intensity='+[regex]::Escape($frozen.Groups[2].Value)+' ')){throw 'Actual worker frozen revision/intensity mismatch'}
   Run-Check 'concurrent-decode' (Get-Command ffmpeg).Source ('-nostdin -v error -i "'+(Join-Path $runDir 'concurrent.mp4')+'" -f null -')
   Run-Check 'concurrent-probe' (Get-Command ffprobe).Source ('-v error -show_streams -of json "'+(Join-Path $runDir 'concurrent.mp4')+'"')
  }
 }
 $passed=$true
}catch{$problem=$_.Exception.Message;Write-Host $problem}
finally{
 $env:VEYRA_LOG_FILE=$oldLog;$env:VEYRA_TEST_REJECT_NR_DISABLE=$oldReject;$timer.Stop()
 [ordered]@{runId=$runId;passed=$passed;case=$Case;seconds=$timer.Elapsed.TotalSeconds;perInvocationLimit=$MaxRuntimeSeconds;exeSha256=(Get-FileHash -LiteralPath $exe).Hash;items=$items;failure=$problem;limits=@('software short tests only','capture hardware reserved for user','physical scanout/photon latency unmeasured')}|ConvertTo-Json -Depth 8|Set-Content (Join-Path $runDir 'result.json')
 Write-Host "UI evidence=$runDir passed=$passed"
}
if($passed){exit 0}else{exit 1}
