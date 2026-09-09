# Windows native regression: move/resize while the professional selector pumps nested messages.
# Usage: python scripts/acceptance/ui-window-resize.py [exe] [media]
import re
import ctypes as c, ctypes.wintypes as w, subprocess,time,sys,json,pathlib,os
# A separate process owns the deadline: native synchronous window messages can
# block the worker, so its own p.wait timeout is not a sufficient watchdog.
if '--worker' not in sys.argv:
 supervisor=subprocess.Popen([sys.executable,__file__,'--worker',*sys.argv[1:]])
 try:
  sys.exit(supervisor.wait(timeout=25))
 except subprocess.TimeoutExpired:
  subprocess.run(['taskkill.exe','/PID',str(supervisor.pid),'/T','/F'],timeout=5,capture_output=True)
  print('FAIL: UI regression exceeded external 25-second deadline')
  sys.exit(1)
sys.argv.remove('--worker')
u=c.WinDLL('user32',use_last_error=True)
u.SetWindowPos.argtypes=[w.HWND,w.HWND,c.c_int,c.c_int,c.c_int,c.c_int,w.UINT]
u.GetDlgItem.argtypes=[w.HWND,c.c_int];u.GetDlgItem.restype=w.HWND
u.SendMessageTimeoutW.argtypes=[w.HWND,w.UINT,w.WPARAM,w.LPARAM,w.UINT,w.UINT,c.POINTER(c.c_size_t)]
u.PostMessageW.argtypes=[w.HWND,w.UINT,w.WPARAM,w.LPARAM]
CB=c.WINFUNCTYPE(w.BOOL,w.HWND,w.LPARAM)
def find(pid):
 found=[]
 @CB
 def visit(h,l):
  p=w.DWORD();u.GetWindowThreadProcessId(h,c.byref(p));buf=c.create_unicode_buffer(128);u.GetClassNameW(h,buf,128)
  if p.value==pid and buf.value=='VeyraApp':found.append(h)
  return True
 u.EnumWindows(visit,0);return found[0] if found else None
def send(h,m,a=0,b=0):
 result=c.c_size_t()
 if not u.SendMessageTimeoutW(h,m,a,b,2,1500,c.byref(result)):raise RuntimeError('message timeout/dead window')
 return result.value
root=pathlib.Path.cwd();exe=pathlib.Path(sys.argv[1] if len(sys.argv)>1 else 'out/build/x64-release/veyra.exe');out=root/'logs/optimization-goal-20260908'/('resize-'+str(time.time_ns()));out.mkdir()
env=os.environ.copy();env['PATH']=r'C:\veyra-deps\installed\x64-windows\bin;'+env['PATH'];env['VEYRA_LOG_FILE']=str(out/'app.log')
si=subprocess.STARTUPINFO();si.dwFlags=1;si.wShowWindow=0
start=time.monotonic();result={'exe':str(exe),'moves':0,'sizes':0,'error':''}
with (out/'stdout.log').open('w') as stdout,(out/'stderr.log').open('w') as stderr:
 args=[str(exe),'--smoke-view','professional','--smoke-seconds','18']
 if len(sys.argv)>2:args += [sys.argv[2],'--realtime','--no-fg']
 else:args += ['--smoke-empty']
 p=subprocess.Popen(args,stdout=stdout,stderr=stderr,env=env,startupinfo=si)
 try:
  h=None
  while time.monotonic()-start<5 and p.poll() is None:
   h=find(p.pid)
   if h:break
   time.sleep(.05)
  if not h:raise RuntimeError('main window missing')
  u.ShowWindow(h,4)
  time.sleep(.7)
  u.PostMessageW(h,0x112,0xF012,0)
  time.sleep(.2)
  u.PostMessageW(h,0x100,0x27,0)
  u.PostMessageW(h,0x100,0x1B,0)
  time.sleep(.1)
  for i in range(24):
   if i%6==0:
    combo=u.GetDlgItem(h,119)
    u.PostMessageW(combo,0x14F,1,0)
    time.sleep(.12)
   if p.poll() is not None:raise RuntimeError('application crashed')
   if not u.SetWindowPos(h,None,40+i*2,50+i,1280,800,0x15):raise RuntimeError('move failed')
   result['moves']+=1
   send(h,0x231)
   if not u.SetWindowPos(h,None,0,0,800+(i%6)*110,600+(i%4)*60,0x16):raise RuntimeError('resize failed')
   result['sizes']+=1
   send(h,0x232);send(h,0)
   if i%6==0:
    # Moving an anchor does not necessarily dismiss it before the next request.
    # Close explicitly and acknowledge modal-loop unwind before reopening.
    u.PostMessageW(combo,0x14F,0,0)
    closeDeadline=time.monotonic()+1
    while send(combo,0x157):
     if time.monotonic()>=closeDeadline:raise RuntimeError('selector did not close')
     time.sleep(.02)
   time.sleep(.05)
  p.wait(timeout=max(1,25-(time.monotonic()-start)))
 except Exception as e:result['error']=str(e)
 finally:
  if p.poll() is None:p.kill();p.wait()
 log=(out/'app.log').read_text(encoding='utf-8',errors='replace') if (out/'app.log').exists() else ''
 result['professional']='professional=true' in log
 result['selectorOpens']=log.count('[ui-selector] open items=')
 smoke=re.search(r'\[app\] smoke frames=(\d+)[^\r\n]*failed=false',log)
 result['cleanSmoke']=bool(smoke) and (len(sys.argv)<=2 or int(smoke[1])>0)
 result['exit']=p.returncode;result['seconds']=time.monotonic()-start;result['passed']=p.returncode==0 and not result['error'] and result['sizes']==24 and result['professional'] and result['selectorOpens']>=4 and result['cleanSmoke']
 (out/'result.json').write_text(json.dumps(result,indent=2));print(out/'result.json');print(json.dumps(result))


sys.exit(0 if result['passed'] else 1)
