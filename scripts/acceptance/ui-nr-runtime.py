"""Exercise NR runtime changes, rollback, and the actual professional selector."""
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
        sys.exit('NR switch test exceeded 100 seconds')
sys.argv.remove('--worker')
exe = Path(sys.argv[1]).resolve()
community = Path(sys.argv[2]).resolve()
hidden = community.with_suffix('.dll.test-disabled')
assert community.is_file() and not hidden.exists()
out = Path('logs/nr-runtime-switch') / str(time.time_ns())
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
startup = subprocess.STARTUPINFO(); startup.dwFlags = 1; startup.wShowWindow = 0
with (out / 'stdout.log').open('w') as stdout, (out / 'stderr.log').open('w') as stderr:
    process = subprocess.Popen([str(exe), str(Path('loop/local/fixed_clips/test_av_1080p.mp4').resolve()),
        '--nr-original', '--nr', '--no-sr', '--no-fg', '--smoke-view', 'professional', '--smoke-seconds', '90'],
        env=env, stdout=stdout, stderr=stderr, startupinfo=startup)
    try:
        main = wait(lambda: windows('VeyraApp'))[0]
        u.ShowWindow(main, 4)
        panel = wait(lambda: windows('VeyraInspector', main))[0]
        body = windows('VeyraInspectorBody', panel)[0]
        wait(lambda: 'professional=true' in log())
        selector = u.GetDlgItem(body, 218)
        label = u.GetDlgItem(panel, 400)
        assert selector and send(selector, 0x146) == 2
        def reveal():
            for unused in range(20):
                r, v = rect(selector), rect(body)
                if v[1] <= r[1] and r[3] <= v[3]: return
                delta = 120 if r[1] < v[1] else -120
                send(body, 0x20A, (delta & 0xffff) << 16)
            raise RuntimeError('selector cannot be scrolled into view')
        for width, height in [(1280, 900), (1040, 540)]:
            u.SetWindowPos(main, None, 30, 30, width, height, 0x14); time.sleep(.25)
            reveal()
            r, v = rect(selector), rect(body)
            assert v[1] <= r[1] < r[3] <= v[3], 'selector clipped'
            assert rect(u.GetDlgItem(body, 1117))[3] <= r[1], 'label overlap'
            result['layouts'].append({'selector': r, 'body': v})
        previous_index = send(selector, 0x147)
        send(selector, 0x20A, ((-120) & 0xffff) << 16)
        assert send(selector, 0x147) == previous_index, 'wheel changed runtime'
        send(body, 0x20A, 120 << 16)

        def choose(index, rejected=False):
            reveal()
            previous = text(label)
            start = len(log())
            u.PostMessageW(selector, 0x201, 1, 12 | (12 << 16))
            popup = wait(lambda: windows('VeyraGlassSelector'))[0]
            listing = u.GetDlgItem(popup, 1)
            send(listing, 0x186, index); u.PostMessageW(listing, 0x100, 0x0D, 0)
            wait(lambda: not windows('VeyraGlassSelector'))
            if rejected:
                wait(lambda: send(selector, 0x147) == 0 and 'NR' in text(u.GetDlgItem(panel, 401)))
                wait(lambda: 'staged DLL missing' in log()[start:])
                result['switches'].append('missing community rejected; original restored')
                return
            def applied():
                current = text(label); values = re.findall(r'\d+', current.splitlines()[0])
                return current != previous and len(values) >= 2 and values[0] == values[1]
            wait(applied)
            expected = 'community-RTX40-RTX50' if index else 'NVIDIA-original'
            wait(lambda: f'selected={expected}' in log()[start:] and 'Applied revision=' in log()[start:])
            segment = log()[start:]
            assert f'selected={expected}' in segment and 'Applied revision=' in segment
            assert 'CreateFeature id=18 result=0x1' in segment
            time.sleep(.5)
            r = rect(u.GetDlgItem(main, 1000))
            image = ImageGrab.grab(bbox=tuple(r))
            assert max(ImageStat.Stat(image.convert('RGB')).stddev) > 10, 'blank video'
            image.save(out / f'{len(result["switches"])}-{index}.png')
            result['switches'].append(expected)

        choose(1); choose(0)
        community.rename(hidden)
        try: choose(1, rejected=True)
        finally: hidden.rename(community)
        choose(1); choose(0)
        result['passed'] = True
    except Exception as error:
        result.update(passed=False, error=str(error))
    finally:
        for hwnd in windows('VeyraApp'): u.PostMessageW(hwnd, 0x10, 0, 0)
        try: process.wait(timeout=10)
        except subprocess.TimeoutExpired: process.kill(); process.wait()
        if hidden.exists(): hidden.rename(community)
        result['exit'] = process.returncode
        result['passed'] = result.get('passed', False) and process.returncode == 0
        (out / 'result.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
        print(out / 'result.json'); print(json.dumps(result))
sys.exit(0 if result['passed'] else 1)
