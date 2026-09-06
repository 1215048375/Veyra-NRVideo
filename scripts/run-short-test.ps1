[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$Exe,
    [string[]]$Arguments=@(),
    [ValidateRange(1,300)][int]$TimeoutSeconds=60,
    [string]$LogPrefix='logs/short-test'
)
$ErrorActionPreference='Stop'
try {
    $path=(Resolve-Path -LiteralPath $Exe).Path
    $prefix=[IO.Path]::GetFullPath($LogPrefix)
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($prefix)) | Out-Null
    $quoted=@($Arguments | ForEach-Object { '"' + $_.Replace('"','\"') + '"' })
    $p=Start-Process -FilePath $path -ArgumentList $quoted -PassThru -WindowStyle Hidden -RedirectStandardOutput ($prefix+'.stdout.log') -RedirectStandardError ($prefix+'.stderr.log')
    $processHandle=$p.Handle
    if(-not $p.WaitForExit($TimeoutSeconds*1000)) {
        Stop-Process -Id $p.Id -Force
        Write-Host ('TIMEOUT after {0}s; stopped test PID {1}' -f $TimeoutSeconds,$p.Id)
        exit 124
    }
    $p.Refresh()
    Write-Host ('exitCode={0} stdout={1}.stdout.log stderr={1}.stderr.log' -f $p.ExitCode,$prefix)
    exit $p.ExitCode
} catch { Write-Error $_; exit 1 }
