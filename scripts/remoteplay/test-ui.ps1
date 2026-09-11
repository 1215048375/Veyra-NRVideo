param([Parameter(Mandatory=$true)][string]$PlayerExe,[Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$PlayerExe=(Resolve-Path -LiteralPath $PlayerExe).Path
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
[IO.Directory]::CreateDirectory($OutputDirectory)|Out-Null
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;using System.Text;using System.Runtime.InteropServices;
public static class RpUi {
 public delegate bool EnumProc(IntPtr h,IntPtr p);
 [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb,IntPtr p);
 [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr h,EnumProc cb,IntPtr p);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h,StringBuilder s,int n);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h,StringBuilder s,int n);
 [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h,int id);
 [DllImport("user32.dll")] public static extern int GetDlgCtrlID(IntPtr h);
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern bool SetWindowText(IntPtr h,string s);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h,out Rect r);
 [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h,IntPtr dc,uint flags);
 public struct Rect {public int left,top,right,bottom;}
 public static string Text(IntPtr h){var s=new StringBuilder(1024);GetWindowText(h,s,1024);return s.ToString();}
 public static IntPtr Find(uint pid,string cls){IntPtr found=IntPtr.Zero;EnumWindows((h,p)=>{uint owner;GetWindowThreadProcessId(h,out owner);var s=new StringBuilder(128);GetClassName(h,s,128);if(owner==pid&&s.ToString()==cls)found=h;return true;},IntPtr.Zero);return found;}
 public static IntPtr Button(IntPtr root,string text){IntPtr found=IntPtr.Zero;EnumChildWindows(root,(h,p)=>{if(Text(h)==text)found=h;return true;},IntPtr.Zero);return found;}
}
'@
$oldLog=$env:VEYRA_LOG_FILE
$process=$null
try {
 $env:VEYRA_LOG_FILE=Join-Path $OutputDirectory 'ui-app.log'
 $process=Start-Process -FilePath $PlayerExe -ArgumentList @('--smoke-empty','--smoke-seconds','45','--no-nr','--no-sr','--no-fg') -PassThru -WindowStyle Hidden
 $deadline=[DateTime]::UtcNow.AddSeconds(8)
 do {$main=[RpUi]::Find($process.Id,'VeyraApp');if($main -eq [IntPtr]::Zero){Start-Sleep -Milliseconds 50}}while($main -eq [IntPtr]::Zero -and [DateTime]::UtcNow -lt $deadline)
 if($main -eq [IntPtr]::Zero){throw 'Veyra main window did not open'}
 $button=[RpUi]::Button($main,'PS5');if($button -eq [IntPtr]::Zero){throw 'PS5 entry missing'}
 $id=[RpUi]::GetDlgCtrlID($button)
 for($round=0;$round -lt 2;$round++){
  [RpUi]::PostMessage($main,0x111,[IntPtr]$id,[IntPtr]::Zero)|Out-Null
  $deadline=[DateTime]::UtcNow.AddSeconds(3)
  do {$panel=[RpUi]::Find($process.Id,'VeyraRemotePlaySetup');if($panel -eq [IntPtr]::Zero){Start-Sleep -Milliseconds 25}}while($panel -eq [IntPtr]::Zero -and [DateTime]::UtcNow -lt $deadline)
  if($panel -eq [IntPtr]::Zero){throw 'Remote Play panel missing'}
  foreach($control in @(15,16)){if([RpUi]::GetDlgItem($panel,$control) -eq [IntPtr]::Zero){throw 'Bitrate/profile management missing'}}
  # Invalid local validation only; never contact a console or use a real key.
  foreach($field in @(1,2,3,11)){[RpUi]::SetWindowText([RpUi]::GetDlgItem($panel,$field),'')|Out-Null}
  [RpUi]::PostMessage($panel,0x111,[IntPtr]6,[IntPtr]::Zero)|Out-Null
  Start-Sleep -Milliseconds 150
  if(-not [RpUi]::Text([RpUi]::GetDlgItem($panel,13)).Contains('8 位配对码')){throw 'Pairing validation feedback missing'}
  if($round -eq 0){
   $rect=New-Object RpUi+Rect;[RpUi]::GetWindowRect($panel,[ref]$rect)|Out-Null
   $bitmap=New-Object Drawing.Bitmap(($rect.right-$rect.left),($rect.bottom-$rect.top))
   $graphics=[Drawing.Graphics]::FromImage($bitmap);$dc=$graphics.GetHdc()
   try {if(-not [RpUi]::PrintWindow($panel,$dc,2)){throw 'Panel render capture failed'}}finally{$graphics.ReleaseHdc($dc)}
   $bitmap.Save((Join-Path $OutputDirectory 'remoteplay-panel.png'));$graphics.Dispose();$bitmap.Dispose()
  }
  [RpUi]::PostMessage($panel,0x10,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null
  Start-Sleep -Milliseconds 150
  if([RpUi]::Find($process.Id,'VeyraRemotePlaySetup') -ne [IntPtr]::Zero){throw 'Panel did not close'}
 }
 # Exercise the real mode animation in the RemotePlay-enabled executable.
 for($cycle=0;$cycle -lt 20;$cycle++){
  [RpUi]::PostMessage($main,0x111,[IntPtr]220,[IntPtr]::Zero)|Out-Null
  Start-Sleep -Milliseconds 310
 }
 $modeLog=Get-Content (Join-Path $OutputDirectory 'ui-app.log') -Raw
 if(([regex]::Matches($modeLog,'completed; final layout')).Count -lt 20){throw 'Mode animation timer did not complete all transitions'}
 for($cycle=0;$cycle -lt 20;$cycle++){
  $fullscreen=[RpUi]::GetDlgCtrlID([RpUi]::Button($main,'全屏 F11'))
  if($fullscreen -eq 0){$fullscreen=[RpUi]::GetDlgCtrlID([RpUi]::Button($main,'全屏'))}
  if($fullscreen -eq 0){$fullscreen=[RpUi]::GetDlgCtrlID([RpUi]::Button($main,'退出全屏'))}
  if($fullscreen -eq 0){throw 'Fullscreen button not found'}
  [RpUi]::PostMessage($main,0x111,[IntPtr]$fullscreen,[IntPtr]::Zero)|Out-Null
  Start-Sleep -Milliseconds 50
 }
 [RpUi]::PostMessage($main,0x10,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null
 if(-not $process.WaitForExit(5000)){throw 'Owned UI test did not exit'}
 Write-Output 'REMOTEPLAY_UI_PASS entry=1 panel_open_close=2 invalid_pairing_rejected=1 PS5_CONNECTION_NOT_TESTED=1'
} finally {
 $env:VEYRA_LOG_FILE=$oldLog
 if($process -and -not $process.HasExited){Stop-Process -Id $process.Id -Force}
}
