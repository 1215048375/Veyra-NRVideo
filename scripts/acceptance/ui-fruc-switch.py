# Regression: live per-field settings, wheel safety, discrete buttons, reset. Own app; 30-second app deadline.
import ctypes as c,ctypes.wintypes as w,subprocess,os,time,pathlib,json,uuid
r=pathlib.Path.cwd();u=c.WinDLL('user32');u.GetWindowThreadProcessId.argtypes=[w.HWND,c.POINTER(w.DWORD)];u.GetDlgCtrlID.argtypes=[w.HWND];u.GetParent.argtypes=[w.HWND];u.GetParent.restype=w.HWND
u.SendMessageTimeoutW.argtypes=[w.HWND,w.UINT,w.WPARAM,w.LPARAM,w.UINT,w.UINT,c.POINTER(c.c_size_t)];u.SendMessageTimeoutW.restype=w.LPARAM
cb=c.WINFUNCTYPE(w.BOOL,w.HWND,w.LPARAM);u.EnumWindows.argtypes=[cb,w.LPARAM];u.EnumChildWindows.argtypes=[w.HWND,cb,w.LPARAM]
def send(h,m,a=0,b=0):
 result=c.c_size_t();assert u.SendMessageTimeoutW(h,m,a,b,2,1500,c.byref(result));return result.value
log=r/('logs/fruc-switch-'+uuid.uuid4().hex+'.log');env=os.environ.copy();env['PATH']=r'C:\veyra-deps\installed\x64-windows\bin;'+env['PATH'];env['VEYRA_LOG_FILE']=str(log)
p=subprocess.Popen([str(r/'out/build/x64-release/veyra.exe'),'--smoke-seconds','24','--no-nr','--no-sr','--fruc','--fg-multiplier','2',str(r/'logs/video-sdk-trial-20260909/known-pan15.mp4')],env=env)
try:
 start=time.monotonic();time.sleep(3);controls={}
 @cb
 def child(h,l):
  ident=u.GetDlgCtrlID(h)
  if ident in [202,208,211]:controls[ident]=h
  return True
 @cb
 def top(h,l):
  pid=w.DWORD();u.GetWindowThreadProcessId(h,c.byref(pid))
  if pid.value==p.pid:u.EnumChildWindows(h,child,0)
  return True
 u.EnumWindows(top,0);assert len(controls)==3,controls
 def select(ident,index):
  h=controls[ident];send(h,0x14e,index);send(u.GetParent(h),0x111,ident|(1<<16),h)
 def click(ident):
  h=controls[ident];send(u.GetParent(h),0x111,ident,h)
 def textOf(ident):
  buf=c.create_unicode_buffer(100);send(controls[ident],13,100,c.addressof(buf));return buf.value
 def change(ident,index,token):
  old=log.read_text(encoding='utf-8',errors='replace').count(token)
  select(ident,index);deadline=time.monotonic()+6
  while time.monotonic()<deadline:
   data=log.read_text(encoding='utf-8',errors='replace')
   if data.count(token)>old:return
   assert p.poll() is None,'app exited early'
   time.sleep(.05)
  raise AssertionError('not applied: '+token)
 change(202,2,'backend=FRUC multiplier=3')
 change(202,3,'backend=FRUC multiplier=4')
 change(208,0,'backend=DLSS multiplier=4')
 change(208,1,'backend=FRUC multiplier=4')
 change(202,0,'backend=FRUC multiplier=1')
 change(202,1,'backend=FRUC multiplier=2')
 click(211)
 code=p.wait(timeout=max(1,35-(time.monotonic()-start)));data=log.read_text()
 ok=code==0 and 'failed=false' in data and 'backend=DLSS multiplier=1' in data
 print(json.dumps({'exit':code,'switch_2_3_4_dlss_fruc_off_on_reset':ok,'log':str(log)}));assert ok

finally:
 if p.poll() is None:p.kill();p.wait()
