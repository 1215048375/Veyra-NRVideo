# Regression: live per-field settings, wheel safety, discrete buttons, reset. Own app; 30-second app deadline.
import ctypes as c,ctypes.wintypes as w,subprocess,os,time,pathlib,json,uuid
r=pathlib.Path.cwd();u=c.WinDLL('user32');u.GetWindowThreadProcessId.argtypes=[w.HWND,c.POINTER(w.DWORD)];u.GetDlgCtrlID.argtypes=[w.HWND];u.GetParent.argtypes=[w.HWND];u.GetParent.restype=w.HWND
u.SendMessageTimeoutW.argtypes=[w.HWND,w.UINT,w.WPARAM,w.LPARAM,w.UINT,w.UINT,c.POINTER(c.c_size_t)];u.SendMessageTimeoutW.restype=w.LPARAM
cb=c.WINFUNCTYPE(w.BOOL,w.HWND,w.LPARAM);u.EnumWindows.argtypes=[cb,w.LPARAM];u.EnumChildWindows.argtypes=[w.HWND,cb,w.LPARAM]
def send(h,m,a=0,b=0):
 result=c.c_size_t();assert u.SendMessageTimeoutW(h,m,a,b,2,1500,c.byref(result));return result.value
log=r/('logs/settings-page-scope-'+uuid.uuid4().hex+'.log');env=os.environ.copy();env['PATH']=r'C:\veyra-deps\installed\x64-windows\bin;'+env['PATH'];env['VEYRA_LOG_FILE']=str(log)
p=subprocess.Popen([str(r/'out/build/x64-release/veyra.exe'),'--smoke-seconds','22','--native','--video-sr','1','--nr','--no-fg',str(r/'logs/ui-repair-v3/glass-video.mp4')],env=env)
try:
 start=time.monotonic();time.sleep(4);controls={}
 @cb
 def child(h,l):
  ident=u.GetDlgCtrlID(h)
  if ident in [100,101,202,203,207,210,211,212,600,601,603,607,700,702,711,721]:controls[ident]=h
  return True
 @cb
 def top(h,l):
  pid=w.DWORD();u.GetWindowThreadProcessId(h,c.byref(pid))
  if pid.value==p.pid:u.EnumChildWindows(h,child,0)
  return True
 u.EnumWindows(top,0);assert len(controls)==14,controls
 def select(ident,index):
  h=controls[ident];send(h,0x14e,index);send(u.GetParent(h),0x111,ident|(1<<16),h)
 def click(ident):
  h=controls[ident];send(u.GetParent(h),0x111,ident,h)
 def textOf(ident):
  buf=c.create_unicode_buffer(100);send(controls[ident],13,100,c.addressof(buf));return buf.value
 # Live edits and choices commit individually; no Apply controls remain.
 assert 210 not in controls and 212 not in controls
 def edit(ident,value):
  buf=c.create_unicode_buffer(value);send(controls[ident],12,0,c.addressof(buf))
 def waitFor(token):
  deadline=time.monotonic()+4
  while time.monotonic()<deadline:
   if token in log.read_text(encoding='utf-8',errors='replace'):return
   time.sleep(.05)
  raise AssertionError('missing actual GPU log: '+token)
 edit(100,'0.314159');waitFor('intensity=0.314159')
 select(202,1);waitFor('multiplier=2')
 before=log.read_text();assert before.count('[resolution]')==1,'FG changed native NR size'
 # Wheel over any parameter control scrolls the page without changing values.
 for ident in [600,601,603,607,202,203,207,100,700]:
  h=controls[ident];old=send(h,0x400) if ident>=600 and ident<612 else send(h,0x147) if ident in [202,203,207] else textOf(ident)
  for delta in [-120,120,-120]:send(h,0x20a,(delta&0xffff)<<16,0)
  new=send(h,0x400) if ident>=600 and ident<612 else send(h,0x147) if ident in [202,203,207] else textOf(ident)
  assert old==new,('wheel changed',ident,old,new)
 edit(100,'-');select(203,0);time.sleep(1)
 # Invalid numeric input must not block a valid change on another control.
 assert 'nr=1920x1080' in log.read_text()
 edit(100,'0.42');waitFor('intensity=0.42')
 click(702);waitFor('style=2');assert send(controls[202],0x147)==1
 click(711);waitFor('autoMask=1');click(721);waitFor('UI=1')
 # Drag-equivalent HSCROLL, without wheel, still applies the slider value.
 send(controls[601],0x405,1,37);send(u.GetParent(controls[601]),0x114,5,controls[601]);waitFor('tone=0.37')
 click(211);time.sleep(1)
 assert textOf(100)=='1' and textOf(101)=='1' and send(controls[202],0x147)==0 and send(controls[203],0x147)==0
 code=p.wait(timeout=max(1,30-(time.monotonic()-start)));data=log.read_text()
 ok=code==0 and 'failed=false' in data
 print(json.dumps({'exit':code,'live_numeric_fg_discrete_reset':True,'wheel_does_not_adjust':True,'fg_keeps_nr_size':True,'pass':ok,'log':str(log)}));assert ok

finally:
 if p.poll() is None:p.kill();p.wait()
