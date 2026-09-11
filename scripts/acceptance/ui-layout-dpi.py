"""Own-process UI geometry/pixel regression; injected DPI is not OS monitor DPI."""
import ctypes as c
import ctypes.wintypes as w
import hashlib
import json
import os
import pathlib
import subprocess
import sys
import time
from PIL import ImageGrab, ImageChops

if '--worker' not in sys.argv:
    child = subprocess.Popen([sys.executable, __file__, '--worker', *sys.argv[1:]])
    try:
        sys.exit(child.wait(timeout=120))
    except subprocess.TimeoutExpired:
        subprocess.run(['taskkill.exe', '/PID', str(child.pid), '/T', '/F'], capture_output=True, timeout=5)
        sys.exit('FAIL: UI layout exceeded 120 seconds')
sys.argv.remove('--worker')
u = c.WinDLL('user32', use_last_error=True)
g = c.WinDLL('gdi32', use_last_error=True)
u.SetProcessDpiAwarenessContext.argtypes = [w.HANDLE]
u.SetProcessDpiAwarenessContext(c.c_void_p(-4))
u.GetDlgItem.argtypes = [w.HWND, c.c_int]
u.GetDlgItem.restype = w.HWND
u.GetWindowRect.argtypes = [w.HWND, c.POINTER(w.RECT)]
u.GetClientRect.argtypes = [w.HWND, c.POINTER(w.RECT)]
u.GetDpiForWindow.argtypes = [w.HWND]
u.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
u.GetClassNameW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
u.IsWindowVisible.argtypes = [w.HWND]
u.ShowWindow.argtypes = [w.HWND, c.c_int]
u.SetWindowPos.argtypes = [w.HWND, w.HWND, c.c_int, c.c_int, c.c_int, c.c_int, w.UINT]
u.PostMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
u.SendMessageTimeoutW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM, w.UINT, w.UINT, c.POINTER(c.c_size_t)]
g.GetObjectW.argtypes = [w.HANDLE, c.c_int, c.c_void_p]
visit_type = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)


def send(hwnd, message, first=0, second=0):
    result = c.c_size_t()
    if not u.SendMessageTimeoutW(hwnd, message, first, second, 2, 1500, c.byref(result)):
        raise RuntimeError('window message timed out')
    return result.value


def windows(class_name, parent=None):
    found = []

    @visit_type
    def visit(hwnd, unused):
        pid = w.DWORD()
        u.GetWindowThreadProcessId(hwnd, c.byref(pid))
        name = c.create_unicode_buffer(128)
        u.GetClassNameW(hwnd, name, 128)
        if pid.value == process.pid and name.value == class_name:
            found.append(hwnd)
        return True

    if parent:
        u.EnumChildWindows(parent, visit, 0)
    else:
        u.EnumWindows(visit, 0)
    return found


def wait_for(action, seconds=5):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        value = action()
        if value:
            return value
        assert process.poll() is None, 'application exited early'
        time.sleep(.025)
    raise RuntimeError('condition timed out')


def rect(hwnd):
    value = w.RECT()
    assert u.GetWindowRect(hwnd, c.byref(value))
    return [value.left, value.top, value.right, value.bottom]


def contains(outer, inner):
    return outer[0] <= inner[0] and outer[1] <= inner[1] and outer[2] >= inner[2] and outer[3] >= inner[3]


def overlaps(a, b):
    return max(a[0], b[0]) < min(a[2], b[2]) and max(a[1], b[1]) < min(a[3], b[3])


def font_height(hwnd):
    data = c.create_string_buffer(92)  # LOGFONTW
    assert g.GetObjectW(send(hwnd, 0x31), len(data), data)
    return abs(c.c_long.from_buffer(data).value)


exe = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else 'out/build/x64-release/veyra.exe').resolve()
out = pathlib.Path('logs/continuation-repair-20260910') / ('ui-layout-' + str(time.time_ns()))
out.mkdir(parents=True)
env = os.environ.copy()
env['VEYRA_LOG_FILE'] = str((out / 'app.log').resolve())
env['PATH'] = r'C:\veyra-deps\installed\x64-windows\bin;' + env['PATH']
result = {'exeHash': hashlib.sha256(exe.read_bytes()).hexdigest(), 'cases': [], 'passed': False}
startup = subprocess.STARTUPINFO()
startup.dwFlags = subprocess.STARTF_USESHOWWINDOW
startup.wShowWindow = 0


def snapshot(name):
    shot = ImageGrab.grab(window=main).convert('RGB')
    shot.save(out / (name + '.png'))
    return shot


def local_box(hwnd):
    outer, child = rect(main), rect(hwnd)
    return (child[0]-outer[0], child[1]-outer[1], child[2]-outer[0], child[3]-outer[1])


with (out / 'stdout.log').open('w') as stdout, (out / 'stderr.log').open('w') as stderr:
    process = subprocess.Popen([str(exe), '--smoke-empty', '--smoke-seconds', '110', '--no-nr', '--no-sr', '--no-fg'],
                               stdout=stdout, stderr=stderr, startupinfo=startup, env=env)
    try:
        main = wait_for(lambda: windows('VeyraApp'))[0]
        u.ShowWindow(main, 4)
        video = u.GetDlgItem(main, 1000)
        panel = windows('VeyraInspector', main)[0]
        body = windows('VeyraInspectorBody', panel)[0]
        status = windows('VeyraLiveStatus', main)[0]
        result['windowsDpi'] = u.GetDpiForWindow(main)
        result['dpiBoundary'] = 'Real current-monitor DPI recorded; 96/144/192 are application layout injections, not monitor transitions.'
        # Native monitor scale first; ordinary startup uses no injection.
        time.sleep(.3)
        result['startupFontHeight'] = font_height(u.GetDlgItem(main, 220))
        for dpi in [96, 144, 192]:
            send(main, 0x8000+90, dpi)
            scale = dpi/96
            u.SetWindowPos(main, None, 10, 10, round(720*scale), round(540*scale), 0x14)
            time.sleep(.25)
            assert font_height(u.GetDlgItem(main, 220)) == round(14*scale), 'font size changed at DPI transition'
            assert not u.IsWindowVisible(panel), 'daily mode exposes inspector'
            send(main, 0x111, 220)
            frames = []
            for unused in range(8):
                time.sleep(.045)
                frames.append(rect(video))
            assert len({tuple(r) for r in frames}) >= 2, 'mode transition jumped without intermediate layout'
            if not u.IsWindowVisible(panel):
                send(main, 0x111, 229)
            send(main, 0x111, 231)
            time.sleep(.2)
            assert u.IsWindowVisible(panel) and u.IsWindowVisible(status)
            assert (rect(body)[3]-rect(body)[1])/scale >= 80, 'settings viewport too short'
            assert (rect(status)[3]-rect(status)[1])/scale >= 151, 'status has no complete detail row'
            assert not overlaps(rect(panel), rect(status)), 'settings and status overlap'
            minimum_body = rect(body)
            header = [u.GetDlgItem(main, i) for i in [220, 221, 229, 243, 244, 245]]
            for i, a in enumerate(header):
                assert contains(rect(main), rect(a)), 'header outside window'
                for b in header[i+1:]:
                    assert not overlaps(rect(a), rect(b)), 'header controls overlap'
            send(status, 0x20A, (120*100)<<16)
            time.sleep(.3)
            top = snapshot(f'{dpi}-minimum-top')
            vx = local_box(video)
            center = ((vx[0]+vx[2])//2, (vx[1]+vx[3])//2)
            assert max(top.getpixel(center)) <= 2, 'idle video is not opaque black'
            send(status, 0x20A, ((-120*100)&0xffff)<<16)
            time.sleep(.3)
            bottom = snapshot(f'{dpi}-minimum-bottom')
            box = local_box(status)
            assert ImageChops.difference(top.crop(box), bottom.crop(box)).getbbox(), 'status detail cannot scroll'
            # Scroll settings until the backend is genuinely inside its viewport.
            backend = u.GetDlgItem(body, 208)
            for unused in range(20):
                if contains(rect(body), rect(backend)):
                    break
                send(body, 0x20A, ((-120)&0xffff)<<16)
            assert contains(rect(body), rect(backend)), 'FG selector is unreachable at minimum height'
            u.PostMessageW(backend, 0x201, 1, 10|(10<<16))
            popup = wait_for(lambda: windows('VeyraGlassSelector'))[0]
            listing = u.GetDlgItem(popup, 1)
            assert send(listing, 0x18B) == 2
            assert font_height(listing) == int(13*scale+.5), 'popup font has different scale'
            time.sleep(.15)
            ImageGrab.grab(window=popup).save(out / f'{dpi}-selector.png')
            u.PostMessageW(listing, 0x100, 0x1B, 0)
            wait_for(lambda: not windows('VeyraGlassSelector'))
            send(main, 0x111, 230)
            slider = u.GetDlgItem(body, 600)
            slider_value = send(slider, 0x400)
            send(slider, 0x20A, ((-120)&0xffff)<<16)
            assert send(slider, 0x400) == slider_value, 'scroll wheel changed enhancement strength'
            send(main, 0x111, 231)
            # Exercise resize notifications and multiple widths inside one drag session.
            send(main, 0x231)
            for width, height in [(960, 600), (1180, 760), (1280, 900), (1040, 800)]:
                physical_width = min(round(width*scale), u.GetSystemMetrics(0)-40)
                physical_height = min(round(height*scale), u.GetSystemMetrics(1)-40)
                u.SetWindowPos(main, None, 10, 10, physical_width, physical_height, 0x14)
                time.sleep(.07)
            send(main, 0x232)
            time.sleep(.2)
            snapshot(f'{dpi}-professional')
            result['cases'].append({'dpi': dpi, 'minimumBody': minimum_body, 'finalWindow': rect(main), 'transitionRects': frames, 'font': font_height(backend)})
            send(main, 0x111, 220)
            time.sleep(.35)
        # Restore actual DPI before full monitor / maximized tests.
        send(main, 0x8000+90, 0)
        u.ShowWindow(main, 3)
        time.sleep(.3)
        snapshot('native-maximized')
        u.ShowWindow(main, 9)
        before = rect(main)
        send(main, 0x111, 120)
        time.sleep(.3)
        assert rect(video) == rect(main), 'fullscreen video is not full client'
        assert not u.IsWindowVisible(panel) and not u.IsWindowVisible(status)
        time.sleep(1.8)
        assert not u.IsWindowVisible(u.GetDlgItem(main, 120)), 'fullscreen transport did not auto-hide'
        snapshot('native-fullscreen')
        send(main, 0x100, 0x1B)
        assert rect(main) == before, 'fullscreen did not restore window geometry'
        result['passed'] = True
    except Exception as error:
        result['error'] = str(error)
    finally:
        roots = windows('VeyraApp')
        if roots:
            u.PostMessageW(roots[0], 0x10, 0, 0)
        try:
            process.wait(timeout=8)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
        result['exit'] = process.returncode
        result['passed'] &= process.returncode == 0
        (out / 'result.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
        print(out / 'result.json')
        print(json.dumps(result))
sys.exit(0 if result['passed'] else 1)
