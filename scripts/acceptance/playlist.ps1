[CmdletBinding()]
param([string]$Root = (Join-Path $PSScriptRoot '../..'))
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path -LiteralPath $Root).Path
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vsRoot = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath) -join ''
if (-not $vsRoot) { throw 'Visual Studio C++ tools are required.' }
$vcvars = Join-Path $vsRoot 'VC/Auxiliary/Build/vcvars64.bat'
$output = Join-Path $Root 'out/playlist'
$runId = [Guid]::NewGuid().ToString('N')
New-Item -ItemType Directory -Force -Path $output | Out-Null
$batch = Join-Path $output 'acceptance.cmd'
$flags = '/nologo /std:c++20 /EHsc /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I include'
@(
    '@echo off'
    ('call "{0}"' -f $vcvars)
    'if errorlevel 1 exit /b 1'
    "cl $flags /W4 /WX tests\unit\PlaylistTests.cpp /Foout\playlist\PlaylistTests.obj /Feout\playlist\PlaylistTests.exe"
    'if errorlevel 1 exit /b 1'
    'out\playlist\PlaylistTests.exe'
    'if errorlevel 1 exit /b 1'
    "cl $flags tests\integration\PlaylistWindowTests.cpp apps\veyra\ui\PlaylistWindow.cpp src\base\Log.cpp /Foout\playlist\ /Feout\playlist\PlaylistWindowTests.exe /link user32.lib gdi32.lib comctl32.lib comdlg32.lib shell32.lib ole32.lib"
    'if errorlevel 1 exit /b 1'
    'out\playlist\PlaylistWindowTests.exe'
    'if errorlevel 1 exit /b 1'
    "cl $flags tests\unit\UiContractTests.cpp src\engine\PresetStore.cpp /Foout\playlist\ /Feout\playlist\UiContractTests.exe"
    'if errorlevel 1 exit /b 1'
    "out\playlist\UiContractTests.exe out\playlist\ui-preferences-$runId"
    'if errorlevel 1 exit /b 1'
    "cl $flags /c apps\veyra\ui\AppShell.cpp /Foout\playlist\AppShell.obj"
    'if errorlevel 1 exit /b 1'
    "cl $flags /DVEYRA_ENABLE_REMOTEPLAY /c apps\veyra\ui\AppShell.cpp /Foout\playlist\AppShellRemotePlay.obj"
    'exit /b %errorlevel%'
) | Set-Content -LiteralPath $batch -Encoding Default
$stdout = Join-Path $output "acceptance-$runId.log"
$stderr = Join-Path $output "acceptance-$runId.stderr.log"
$process = Start-Process -FilePath $env:ComSpec -ArgumentList ('/d /c ""{0}""' -f $batch) -WorkingDirectory $Root -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
$processHandle = $process.Handle
if (-not $process.WaitForExit(300000)) { Stop-Process -Id $process.Id -Force; throw 'Playlist acceptance exceeded 300 seconds.' }
Get-Content -LiteralPath $stdout
if ($process.ExitCode -ne 0) { throw "Playlist acceptance failed: $($process.ExitCode)" }
Write-Host 'Playlist UI/queue checks passed. This does not validate EngineController playback or GPU runtime execution.'
