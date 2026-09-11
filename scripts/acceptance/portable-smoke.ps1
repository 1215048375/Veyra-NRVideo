param([Parameter(Mandatory=$true)][string]$PackageDirectory,[Parameter(Mandatory=$true)][string]$InputFile,[Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
# A PowerShell 7 parent may omit Windows PowerShell's Utility module from the
# inherited module paths. Load this host's hashing/JSON cmdlets explicitly.
Import-Module (Join-Path $PSHOME 'Modules/Microsoft.PowerShell.Utility/Microsoft.PowerShell.Utility.psd1') -Force
$package=(Resolve-Path -LiteralPath $PackageDirectory).Path
$inputPath=(Resolve-Path -LiteralPath $InputFile).Path
$output=[IO.Path]::GetFullPath($OutputDirectory)
[IO.Directory]::CreateDirectory($output)|Out-Null
$exe=Join-Path $package 'Veyra.exe'
$savedPath=$env:PATH
$manifests=@(Get-ChildItem -LiteralPath $package -Filter '*manifest.json' -Recurse -File)
$results=[Collections.Generic.List[object]]::new()
try {
    # Prove the distributed application does not need the publisher manifests.
    foreach($file in $manifests){Move-Item -LiteralPath $file.FullName -Destination ($file.FullName+'.test-disabled')}
    $env:PATH="$env:SystemRoot/System32;$env:SystemRoot"
    foreach($case in @(
        @{name='empty';args=@('--smoke-empty','--no-nr','--no-sr','--no-fg');modules=@()},
        @{name='baseline';args=@($inputPath,'--no-nr','--no-sr','--no-fg');modules=@()},
        @{name='community-sr-nr-fg';args=@($inputPath,'--nr-community','--sr','--nr','--fg','--realtime');modules=@('nvngx_dlss.dll','nvngx_dlssnr.dll','nvngx_dlssg.dll')},
        @{name='dlss-sr-nr-fg';args=@($inputPath,'--sr','--nr','--fg','--realtime');modules=@('nvngx_dlss.dll','nvngx_dlssnr.dll','nvngx_dlssg.dll')},
        @{name='video-sr-nr-fg';args=@($inputPath,'--video-sr','1','--nr','--fg','--realtime');modules=@('nvngx_dlssnr.dll','nvngx_dlssg.dll')}
    )) {
        $name=$case.name
        $argv=@($case.args)+@('--smoke-seconds','7')
        if($name -ne 'empty'){$argv+=@('--smoke-controls','--smoke-save',"$output/$name.jpg")}
        $p=Start-Process -FilePath $exe -ArgumentList @($argv|ForEach-Object{'"'+$_+'"'}) -WorkingDirectory $env:TEMP -WindowStyle Hidden -PassThru -RedirectStandardOutput "$output/$name.stdout.log" -RedirectStandardError "$output/$name.stderr.log"
        $handle=$p.Handle
        $loaded=@{}
        $timer=[Diagnostics.Stopwatch]::StartNew()
        while(-not $p.HasExited -and $timer.Elapsed.TotalSeconds -lt 35){
            try {foreach($module in $p.Modules){$loaded[$module.ModuleName.ToLowerInvariant()]=$module.FileName}} catch {if(-not $p.HasExited){throw}}
            Start-Sleep -Milliseconds 200
            $p.Refresh()
        }
        if(-not $p.HasExited){Stop-Process -Id $p.Id -Force;throw "$name exceeded 35 seconds"}
        $p.WaitForExit()
        if($p.ExitCode -ne 0){throw "$name exit $($p.ExitCode)"}
        $log=Get-Content -LiteralPath "$output/$name.stdout.log" -Raw
        if($log -notmatch 'smoke frames=(\d+) generated=(\d+) failed=false'){throw "$name missing successful smoke output"}
        $frames=[int]$Matches[1];$generated=[int]$Matches[2]
        if($name -ne 'empty' -and ($frames -le 0 -or !(Test-Path -LiteralPath "$output/$name.jpg"))){throw "$name missing rendered output"}
        if($case.modules.Count -gt 0 -and $generated -le 0){throw "$name did not generate frames"}
        if($name -eq 'video-sr-nr-fg'){
            # NGX may provide VSR from the installed driver instead of nvngx_vsr.dll.
            if($log -notmatch '\[video-sr\] op=0 result=0x1 seh=0x0' -or $log -notmatch 'gpuSrP95Ms=[1-9]' -or -not $loaded.ContainsKey('_nvngx.dll')){throw 'VSR creation or GPU execution missing'}
        }
        foreach($module in @('avcodec-63.dll','avformat-63.dll','avutil-61.dll','swresample-7.dll','swscale-10.dll','vcruntime140.dll')+$case.modules){
            if(-not $loaded.ContainsKey($module) -or -not $loaded[$module].StartsWith($package+'\',[StringComparison]::OrdinalIgnoreCase)){throw "$name missing app-local module $module ($($loaded[$module]))"}
        }
        if($case.modules -contains 'nvngx_dlssnr.dll'){
            $nrFolder=if($name -eq 'community-sr-nr-fg'){'runtime/experimental/nr-community'}else{'runtime/experimental'}
            $expectedNr=[IO.Path]::GetFullPath((Join-Path $package "$nrFolder/nvngx_dlssnr.dll"))
            if(-not [StringComparer]::OrdinalIgnoreCase.Equals($loaded['nvngx_dlssnr.dll'],$expectedNr)){throw "$name loaded the wrong NR variant"}
        }
        $results.Add(@{name=$name;exit=$p.ExitCode;frames=$frames;generated=$generated;seconds=$timer.Elapsed.TotalSeconds;modules=$loaded})
    }
    @{passed=$true;manifestRequired=$false;exeHash=(Get-FileHash -LiteralPath $exe).Hash;cases=$results}|ConvertTo-Json -Depth 6|Set-Content -LiteralPath "$output/result.json" -Encoding UTF8
    Write-Output "PASS: $output/result.json"
} finally {
    $env:PATH=$savedPath
    foreach($file in $manifests){$disabled=$file.FullName+'.test-disabled';if(Test-Path -LiteralPath $disabled){Move-Item -LiteralPath $disabled -Destination $file.FullName}}
}
