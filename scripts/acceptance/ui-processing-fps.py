"""Own-process regression: visible, bounded live GPU-completion FPS text."""
import ctypes as c
import ctypes.wintypes as w
import os
from pathlib import Path
import subprocess
import time
import uuid

root = Path.cwd()
user = c.WinDLL("user32")
user.SetProcessDPIAware()
user.GetAncestor.argtypes = [w.HWND, w.UINT]
user.GetAncestor.restype = w.HWND
callback = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
user.EnumWindows.argtypes = [callback, w.LPARAM]
user.EnumChildWindows.argtypes = [w.HWND, callback, w.LPARAM]
user.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
user.GetWindowTextW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
user.IsWindowVisible.argtypes = [w.HWND]
user.GetWindowRect.argtypes = [w.HWND, c.POINTER(w.RECT)]
user.GetClientRect.argtypes = [w.HWND, c.POINTER(w.RECT)]
user.ClientToScreen.argtypes = [w.HWND, c.POINTER(w.POINT)]
user.SendMessageTimeoutW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM,
                                     w.UINT, w.UINT, c.POINTER(c.c_size_t)]

for mode in ("daily", "professional", "small"):
    env = os.environ.copy()
    env["VEYRA_LOG_FILE"] = str(root / "logs" / ("fps-ui-" + uuid.uuid4().hex + ".log"))
    process = subprocess.Popen([str(root / "out/build/x64-release/veyra.exe"),
        str(root / "loop/local/fixed_clips/test_av_1080p.mp4"),
        "--no-nr", "--no-fg", "--no-sr", "--smoke-seconds", "8", "--smoke-view", mode], env=env)
    try:
        observed = {}

        @callback
        def child(handle, unused):
            text = c.create_unicode_buffer(512)
            user.GetWindowTextW(handle, text, len(text))
            if text.value.startswith("处理 ") and text.value.endswith(" fps"):
                observed["label"] = handle
                observed["text"] = text.value
            return True

        @callback
        def top(handle, unused):
            pid = w.DWORD()
            user.GetWindowThreadProcessId(handle, c.byref(pid))
            if pid.value == process.pid:
                observed["window"] = handle
                user.EnumChildWindows(handle, child, 0)
            return True

        deadline = time.monotonic() + 6
        while time.monotonic() < deadline:
            user.EnumWindows(top, 0)
            if "text" in observed and float(observed["text"].split()[1]) > 0:
                break
            time.sleep(.1)
        assert "label" in observed and float(observed["text"].split()[1]) > 0, observed
        label = observed["label"]
        observed["window"] = user.GetAncestor(label, 2)
        assert user.IsWindowVisible(label), (mode, "FPS hidden")
        rect, client, origin = w.RECT(), w.RECT(), w.POINT()
        user.GetWindowRect(label, c.byref(rect))
        user.GetClientRect(observed["window"], c.byref(client))
        user.ClientToScreen(observed["window"], c.byref(origin))
        assert origin.x <= rect.left < rect.right <= origin.x + client.right, (mode, rect.left, rect.right, origin.x, client.right)
        assert origin.y <= rect.top < rect.bottom <= origin.y + client.bottom, (mode, rect.top, rect.bottom, origin.y, client.bottom)
        result = c.c_size_t()
        assert user.SendMessageTimeoutW(observed["window"], 0x111, 102, 0, 2, 1000, c.byref(result))
        time.sleep(.3)
        user.EnumWindows(top, 0)
        assert observed["text"] == "处理 0.0 fps", observed
        assert process.wait(timeout=12) == 0
        print("PASS", mode, "visible bounded FPS, live value, pause zero", flush=True)
    finally:
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=5)
