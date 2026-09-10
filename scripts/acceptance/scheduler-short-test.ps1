param(
    [Parameter(Mandatory=$true)][ValidatePattern('^[A-Za-z0-9][A-Za-z0-9_-]*$')][string]$Name,
    [Parameter(Mandatory=$true)][string]$Exe,
    [string[]]$TestArgs=@()
)
$ErrorActionPreference='Stop'
$dir=Join-Path (Get-Location) 'logs/scheduler-repair-20260910'
[IO.Directory]::CreateDirectory($dir)|Out-Null
$runDir=Join-Path $dir $Name
[IO.Directory]::CreateDirectory($runDir)|Out-Null
$watch=[Diagnostics.Stopwatch]::StartNew()
$start=@{FilePath=(Resolve-Path $Exe).Path;PassThru=$true;WindowStyle='Hidden';RedirectStandardOutput="$dir/$Name.stdout.log";RedirectStandardError="$dir/$Name.stderr.log"}
if($TestArgs.Count){$start.ArgumentList=@($TestArgs|ForEach-Object{'"'+$_.Replace('"','\"')+'"'})}
$previousLog=$env:VEYRA_LOG_FILE
try {
    $env:VEYRA_LOG_FILE=Join-Path $runDir 'runtime.log'
    $process=Start-Process @start
} finally { $env:VEYRA_LOG_FILE=$previousLog }
$handle=$process.Handle
if(-not $process.WaitForExit(290000)){Stop-Process -Id $process.Id -Force;throw "$Name exceeded 290 seconds"}
$inputs=@($TestArgs|Where-Object {$_ -match '\.(mp4|nv12)$' -and (Test-Path -LiteralPath $_ -PathType Leaf)}|ForEach-Object {@{path=$_;sha256=(Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash}})
$worker=Join-Path (Split-Path $start.FilePath) 'veyra_fruc_worker.exe'
$workerHash=if(Test-Path -LiteralPath $worker -PathType Leaf){(Get-FileHash -LiteralPath $worker -Algorithm SHA256).Hash}else{$null}
$result=[ordered]@{test=$Name;exitCode=$process.ExitCode;seconds=$watch.Elapsed.TotalSeconds;exeHash=(Get-FileHash $Exe -Algorithm SHA256).Hash;workerHash=$workerHash;args=$TestArgs;inputs=$inputs}
$result|ConvertTo-Json|Set-Content "$dir/$Name.result.json"
Get-Content "$dir/$Name.stdout.log" -Tail 28
Write-Output ($result|ConvertTo-Json -Compress)
exit $process.ExitCode
