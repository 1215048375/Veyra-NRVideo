[CmdletBinding()]
param([string]$Root=(Join-Path $PSScriptRoot '../..'),[ValidateRange(1,300)][int]$MaxRuntimeSeconds=60)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$Root=(Resolve-Path -LiteralPath $Root).Path
$timer=[Diagnostics.Stopwatch]::StartNew()
$dir=Join-Path $Root ('logs/ui-controls-v4/'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $dir|Out-Null
$checks=[Collections.Generic.List[object]]::new();$passed=$false;$failure=''
function Run([string]$name,[string]$binary,[string]$arguments=''){
 $left=$MaxRuntimeSeconds-$timer.Elapsed.TotalSeconds;if($left -lt 1){throw 'Per-invocation limit reached'}
 $p=$null;$clock=[Diagnostics.Stopwatch]::StartNew()
 try{
  $options=@{FilePath=$binary;WorkingDirectory=$Root;WindowStyle='Hidden';PassThru=$true;RedirectStandardOutput=(Join-Path $dir ($name+'.stdout.log'));RedirectStandardError=(Join-Path $dir ($name+'.stderr.log'))}
  if($arguments){$options.ArgumentList=$arguments};$p=Start-Process @options;$handle=$p.Handle
  if(-not $p.WaitForExit([int]($left*1000))){throw "$name timeout"}
  $checks.Add(@{name=$name;command=$binary+' '+$arguments;exit=$p.ExitCode;seconds=$clock.Elapsed.TotalSeconds})
  if($p.ExitCode -ne 0){throw "$name exit $($p.ExitCode)"}
 }finally{if($p -and -not $p.HasExited){Stop-Process -Id $p.Id;$p.WaitForExit()}}
}
try{
 $manifest=Get-Content (Join-Path $Root 'assets/icons/lucide/manifest.json') -Raw|ConvertFrom-Json
 foreach($entry in $manifest.sha256.PSObject.Properties){$actual=(Get-FileHash (Join-Path $Root ('assets/icons/lucide/'+$entry.Name)) -Algorithm SHA256).Hash;if($actual -ne $entry.Value){throw "Icon license/asset identity mismatch: $($entry.Name)"}}
 $checks.Add(@{name='lucide-pinned-assets-and-license';revision=$manifest.revision;files=@($manifest.sha256.PSObject.Properties).Count;exit=0})
 Run 'layout-state' (Join-Path $Root 'out/build/x64-release/veyra_ui_contract_tests.exe') ('"'+(Join-Path $dir 'preferences')+'"')
 Run 'native-popup-interaction' (Join-Path $Root 'out/build/x64-release/veyra_popup_selector_tests.exe')
 $passed=$true
}catch{$failure=$_.Exception.Message}
$result=@{passed=$passed;scope='UI controls and selectors only; not Phase7 product approval';exeSha256=(Get-FileHash (Join-Path $Root 'out/build/x64-release/veyra.exe') -Algorithm SHA256).Hash;seconds=$timer.Elapsed.TotalSeconds;maxRuntimeSeconds=$MaxRuntimeSeconds;checks=$checks;failure=$failure}
$result|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $dir 'result.json') -Encoding UTF8
Write-Output (Join-Path $dir 'result.json');if(-not $passed){Write-Error $failure;exit 1}
