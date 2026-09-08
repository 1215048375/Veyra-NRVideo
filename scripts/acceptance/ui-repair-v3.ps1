[CmdletBinding()]
param([string]$Root=(Join-Path $PSScriptRoot '../..'),[ValidateRange(1,300)][int]$MaxRuntimeSeconds=300)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$Root=(Resolve-Path -LiteralPath $Root).Path
$timer=[Diagnostics.Stopwatch]::StartNew()
$dir=Join-Path $Root ('logs/ui-repair-v3/'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $dir|Out-Null
$bin=Join-Path $Root 'out/build/x64-release'
$checks=[Collections.Generic.List[object]]::new()
$oldLog=$env:VEYRA_LOG_FILE;$oldReject=$env:VEYRA_TEST_REJECT_NR_DISABLE
function Run([string]$name,[string]$exe,[string]$arguments){
 $left=$MaxRuntimeSeconds-$timer.Elapsed.TotalSeconds;if($left -lt 1){throw 'Per-invocation limit reached'}
 $env:VEYRA_LOG_FILE=Join-Path $dir ($name+'.app.log');$p=$null
 try{$p=Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory $Root -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $dir ($name+'.stdout.log')) -RedirectStandardError (Join-Path $dir ($name+'.stderr.log'));$handle=$p.Handle
  if(-not $p.WaitForExit([int]($left*1000))){throw "$name timeout"};$code=$p.ExitCode;$checks.Add(@{name=$name;exit=$code;command=$exe+' '+$arguments});if($code -ne 0){throw "$name exit $code"}
 }finally{if($p -and -not $p.HasExited){& "$env:SystemRoot/System32/taskkill.exe" /PID $p.Id /T /F|Out-Null;$p.WaitForExit()}}
}
$passed=$false;$failure=''
try{
 Run 'contracts' (Join-Path $bin 'veyra_ui_contract_tests.exe') ('"'+(Join-Path $dir 'preferences')+'"')
 $ffmpeg=(Get-Command ffmpeg).Source;$ffprobe=(Get-Command ffprobe).Source
 $image=Join-Path $dir 'aspect-4x3.png';$clip=Join-Path $dir 'aspect-4x3.mp4'
 Run 'image-fixture' $ffmpeg ('-nostdin -v error -f lavfi -i testsrc2=size=1448x1086:rate=30 -frames:v 1 "'+$image+'"')
 Run 'video-fixture' $ffmpeg ('-nostdin -v error -f lavfi -i testsrc2=size=1448x1086:rate=30 -t 20 -c:v libx264 -preset ultrafast -crf 20 -pix_fmt yuv420p "'+$clip+'"')
 foreach($case in @('image','video','reject')){
  $env:VEYRA_TEST_REJECT_NR_DISABLE=if($case -eq 'reject'){'1'}else{$null}
  $input=if($case -eq 'video'){$clip}else{$image};$flag=if($case -eq 'reject'){'--smoke-repair-ui-reject'}else{'--smoke-repair-ui'}
  Run $case (Join-Path $bin 'veyra.exe') ('--smoke-seconds 25 --nr --no-sr --no-fg '+$flag+' --smoke-save "'+(Join-Path $dir ($case+'-sr.png'))+'" "'+$input+'"')
  $log=Get-Content -LiteralPath (Join-Path $dir ($case+'.app.log')) -Raw
  if($log -match '\[ui-repair-test\].*=false' -or $log -notmatch '\[ui-repair-test\]'){throw "$case native click evidence missing or failed"}
  if($case -ne 'reject'){
   if($log -notmatch 'PASS native controls' -or $log -notmatch 'output=2880x2160'){throw "$case incomplete"}
   Run ($case+'-saved-probe') $ffprobe ('-v error -show_streams -of json "'+(Join-Path $dir ($case+'-sr.png'))+'"')
   $probe=Get-Content (Join-Path $dir ($case+'-saved-probe.stdout.log')) -Raw|ConvertFrom-Json
   if($probe.streams[0].width -ne 2880 -or $probe.streams[0].height -ne 2160){throw "$case saved extent wrong"}
  }
 }
 $env:VEYRA_TEST_REJECT_NR_DISABLE=$null
 $out=Join-Path $dir 'export-4x3.mp4'
 Run 'export' (Join-Path $bin 'veyra.exe') ('--export-out "'+$out+'" --max-frames 12 --sr --no-nr --no-fg "'+$clip+'"')
 Run 'export-decode' $ffmpeg ('-nostdin -v error -i "'+$out+'" -f null -')
 Run 'export-probe' $ffprobe ('-v error -show_streams -of json "'+$out+'"')
 $probe=Get-Content (Join-Path $dir 'export-probe.stdout.log') -Raw|ConvertFrom-Json
 if($probe.streams[0].width -ne 2880 -or $probe.streams[0].height -ne 2160 -or [int]$probe.streams[0].nb_frames -ne 12){throw 'SR export dimensions/count mismatch'}
 $passed=$true
}catch{$failure=$_.Exception.Message;Write-Host $failure}
finally{$env:VEYRA_LOG_FILE=$oldLog;$env:VEYRA_TEST_REJECT_NR_DISABLE=$oldReject;$timer.Stop();[ordered]@{passed=$passed;seconds=$timer.Elapsed.TotalSeconds;limitSeconds=$MaxRuntimeSeconds;exeSha256=(Get-FileHash (Join-Path $bin 'veyra.exe')).Hash;checks=$checks;failure=$failure}|ConvertTo-Json -Depth 8|Set-Content (Join-Path $dir 'result.json');Write-Host "UI v3 evidence=$dir passed=$passed"}
if($passed){exit 0}else{exit 1}
