"""Exercise the broadcast presentation switch and its fixed professional control."""
import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time
from PIL import ImageGrab, ImageStat

if '--worker' not in sys.argv:
    child = subprocess.Popen([sys.executable, __file__, '--worker', *sys.argv[1:]])
    try:
        sys.exit(child.wait(timeout=100))
    except subprocess.TimeoutExpired:
        subprocess.run(['taskkill', '/PID', str(child.pid), '/T', '/F'], capture_output=True, timeout=5)
        sys.exit('Broadcast test exceeded 100 seconds')
sys.argv.remove('--worker')
exe = Path(sys.argv[1]).resolve()
out = Path('logs/broadcast-mode') / str(time.time_ns())
out.mkdir(parents=True)
env = os.environ.copy()
env['VEYRA_LOG_FILE'] = str((out / 'app.log').resolve())
u = c.WinDLL('user32', use_last_error=True)
u.SetProcessDpiAwarenessContext.argtypes = [w.HANDLE]
u.SetProcessDpiAwarenessContext(c.c_void_p(-4))
u.GetDlgItem.argtypes = [w.HWND, c.c_int]; u.GetDlgItem.restype = w.HWND
u.PostMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
u.GetWindowRect.argtypes = [w.HWND, c.POINTER(w.RECT)]
u.GetWindowTextW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
u.SetWindowPos.argtypes = [w.HWND, w.HWND, c.c_int, c.c_int, c.c_int, c.c_int, w.UINT]
u.ShowWindow.argtypes = [w.HWND, c.c_int]
u.SendMessageTimeoutW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM, w.UINT, w.UINT, c.POINTER(c.c_size_t)]
visit_type = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)

def send(hwnd, message, first=0, second=0):
    result = c.c_size_t()
    if not u.SendMessageTimeoutW(hwnd, message, first, second, 2, 1500, c.byref(result)):
        raise RuntimeError('window message timeout')
    return result.value

def windows(class_name, parent=None):
    result = []
    @visit_type
    def visit(hwnd, unused):
        owner = w.DWORD(); name = c.create_unicode_buffer(128)
        u.GetWindowThreadProcessId(hwnd, c.byref(owner)); u.GetClassNameW(hwnd, name, 128)
        if owner.value == process.pid and name.value == class_name: result.append(hwnd)
        return True
    if parent: u.EnumChildWindows(parent, visit, 0)
    else: u.EnumWindows(visit, 0)
    return result

def wait(action, seconds=15):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        result = action()
        if result: return result
        if process.poll() is not None: raise RuntimeError('application exited')
        time.sleep(.05)
    raise RuntimeError('condition timeout')

def rect(hwnd):
    r = w.RECT(); assert u.GetWindowRect(hwnd, c.byref(r))
    return [r.left, r.top, r.right, r.bottom]

def text(hwnd):
    b = c.create_unicode_buffer(1024); u.GetWindowTextW(hwnd, b, len(b)); return b.value

def log():
    path = out / 'app.log'
    return path.read_text(encoding='utf-8', errors='replace') if path.exists() else ''


result = {'exeHash': hashlib.sha256(exe.read_bytes()).hexdigest(), 'switches': [], 'layouts': []}
with (out / 'stdout.log').open('w') as stdout, (out / 'stderr.log').open('w') as stderr:
    process = subprocess.Popen([str(exe), str(Path('loop/local/fixed_clips/test_av_1080p.mp4').resolve()),
        '--nr', '--no-sr', '--fg', '--smoke-view', 'professional', '--smoke-seconds', '90'],
        env=env, stdout=stdout, stderr=stderr)
    try:
        main = wait(lambda: windows('VeyraApp'))[0]
        u.ShowWindow(main, 4)
        panel = wait(lambda: windows('VeyraInspector', main))[0]
        body = windows('VeyraInspectorBody', panel)[0]
        toggle = u.GetDlgItem(panel, 219)
        label = u.GetDlgItem(panel, 400)
        assert toggle
        wait(lambda: 'swapEffect=flip-discard' in log() and 'player-timing' in log())
        if '--xess' in sys.argv:
            start = len(log())
            backend = u.GetDlgItem(body, 208)
            send(backend, 0x14E, 1)
            send(panel, 0x111, 208 | (1 << 16), backend)
            wait(lambda: 'Applied revision=' in log()[start:] and 'fgBackend=XeSS' in log()[start:])
            result['backend'] = 'XeSS'
        for width, height in [(1280, 900), (1040, 540)]:
            u.SetWindowPos(main, None, 30, 30, width, height, 0x14)
            time.sleep(.3)
            a, b, v = rect(u.GetDlgItem(panel, 211)), rect(toggle), rect(panel)
            assert a[2] <= b[0] and v[0] <= b[0] < b[2] <= v[2]
            assert b[3] <= rect(body)[1], 'toggle must stay above scroll body'
            send(body, 0x20A, ((-120) & 0xffff) << 16)
            assert rect(toggle) == b, 'toggle moved during scroll'
            result['layouts'].append(b)
        def choose(enabled):
            start = len(log())
            before = text(label)
            assert send(toggle, 0xF0) != int(enabled)
            send(toggle, 0xF5)
            effect = 'flip-sequential' if enabled else 'flip-discard'
            wait(lambda: f'swapEffect={effect}' in log()[start:])
            def applied():
                current = text(label)
                values = re.findall(r'\d+', current.splitlines()[0])
                return current != before and len(values) >= 2 and values[0] == values[1]
            wait(applied)
            assert send(toggle, 0xF0) == int(enabled)
            time.sleep(.4)
            shot = ImageGrab.grab(bbox=tuple(rect(u.GetDlgItem(main, 1000))))
            assert max(ImageStat.Stat(shot.convert('RGB')).stddev) > 10, 'blank video'
            shot.save(out / f'video-{len(result["switches"])}.png')
            result['switches'].append(effect)
        choose(True)
        send(u.GetDlgItem(main, 102), 0xF5)
        choose(False)
        send(u.GetDlgItem(main, 102), 0xF5)
        before_master = text(label)
        send(u.GetDlgItem(main, 221), 0xF5)
        def bypass_applied():
            current = text(label)
            values = re.findall(r'\d+', current.splitlines()[0])
            return current != before_master and len(values) >= 2 and values[0] == values[1]
        wait(bypass_applied)
        time.sleep(.4)
        choose(True)
        send(u.GetDlgItem(main, 120), 0xF5)
        time.sleep(.4)
        send(u.GetDlgItem(main, 120), 0xF5)
        choose(False)
        u.SetWindowPos(main, None, 30, 30, 1280, 900, 0x14)
        time.sleep(.5)
        ImageGrab.grab(bbox=tuple(rect(main))).save(out / 'professional.png')
        result['passed'] = True
    except Exception as error:
        result.update(passed=False, error=str(error), status=text(label), warning=text(u.GetDlgItem(panel, 401)))
    finally:
        for hwnd in windows('VeyraApp'): u.PostMessageW(hwnd, 0x10, 0, 0)
        try: process.wait(timeout=10)
        except subprocess.TimeoutExpired: process.kill(); process.wait()
        result['exit'] = process.returncode
        result['passed'] = result.get('passed', False) and process.returncode == 0
        (out / 'result.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
        print(out / 'result.json'); print(json.dumps(result))
sys.exit(0 if result['passed'] else 1)
