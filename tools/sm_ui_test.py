#!/usr/bin/env python3
"""Tiny Windows UI harness for driving the local SuperMet test window.

Examples:
  python tools/sm_ui_test.py launch
  python tools/sm_ui_test.py keys enter down down enter
  python tools/sm_ui_test.py key esc
  python tools/sm_ui_test.py screenshot --out debug_screenshots/agent.bmp
"""

from __future__ import annotations

import argparse
import ctypes
from ctypes import wintypes
import os
from pathlib import Path
import struct
import subprocess
import sys
import time


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_EXE = ROOT / "build" / "bin-x64-Debug" / "sm.exe"
DEFAULT_TITLE = "SuperMet"

user32 = ctypes.WinDLL("user32", use_last_error=True)
gdi32 = ctypes.WinDLL("gdi32", use_last_error=True)
ULONG_PTR = wintypes.WPARAM


class RECT(ctypes.Structure):
    _fields_ = [
        ("left", ctypes.c_long),
        ("top", ctypes.c_long),
        ("right", ctypes.c_long),
        ("bottom", ctypes.c_long),
    ]


class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [
        ("biSize", wintypes.DWORD),
        ("biWidth", ctypes.c_long),
        ("biHeight", ctypes.c_long),
        ("biPlanes", wintypes.WORD),
        ("biBitCount", wintypes.WORD),
        ("biCompression", wintypes.DWORD),
        ("biSizeImage", wintypes.DWORD),
        ("biXPelsPerMeter", ctypes.c_long),
        ("biYPelsPerMeter", ctypes.c_long),
        ("biClrUsed", wintypes.DWORD),
        ("biClrImportant", wintypes.DWORD),
    ]


class BITMAPINFO(ctypes.Structure):
    _fields_ = [("bmiHeader", BITMAPINFOHEADER), ("bmiColors", wintypes.DWORD * 3)]


class KEYBDINPUT(ctypes.Structure):
    _fields_ = [
        ("wVk", wintypes.WORD),
        ("wScan", wintypes.WORD),
        ("dwFlags", wintypes.DWORD),
        ("time", wintypes.DWORD),
        ("dwExtraInfo", ULONG_PTR),
    ]


class MOUSEINPUT(ctypes.Structure):
    _fields_ = [
        ("dx", ctypes.c_long),
        ("dy", ctypes.c_long),
        ("mouseData", wintypes.DWORD),
        ("dwFlags", wintypes.DWORD),
        ("time", wintypes.DWORD),
        ("dwExtraInfo", ULONG_PTR),
    ]


class HARDWAREINPUT(ctypes.Structure):
    _fields_ = [
        ("uMsg", wintypes.DWORD),
        ("wParamL", wintypes.WORD),
        ("wParamH", wintypes.WORD),
    ]


class INPUT_UNION(ctypes.Union):
    _fields_ = [("mi", MOUSEINPUT), ("ki", KEYBDINPUT), ("hi", HARDWAREINPUT)]


class INPUT(ctypes.Structure):
    _fields_ = [("type", wintypes.DWORD), ("union", INPUT_UNION)]


EnumWindowsProc = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

user32.EnumWindows.argtypes = [EnumWindowsProc, wintypes.LPARAM]
user32.GetWindowTextLengthW.argtypes = [wintypes.HWND]
user32.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
user32.IsWindowVisible.argtypes = [wintypes.HWND]
user32.GetClientRect.argtypes = [wintypes.HWND, ctypes.POINTER(RECT)]
user32.GetDC.argtypes = [wintypes.HWND]
user32.ReleaseDC.argtypes = [wintypes.HWND, wintypes.HDC]
user32.ShowWindow.argtypes = [wintypes.HWND, ctypes.c_int]
user32.SetForegroundWindow.argtypes = [wintypes.HWND]
user32.BringWindowToTop.argtypes = [wintypes.HWND]
user32.SendInput.argtypes = [wintypes.UINT, ctypes.POINTER(INPUT), ctypes.c_int]
user32.MapVirtualKeyW.argtypes = [wintypes.UINT, wintypes.UINT]
gdi32.CreateCompatibleDC.argtypes = [wintypes.HDC]
gdi32.CreateCompatibleBitmap.argtypes = [wintypes.HDC, ctypes.c_int, ctypes.c_int]
gdi32.SelectObject.argtypes = [wintypes.HDC, wintypes.HGDIOBJ]
gdi32.BitBlt.argtypes = [
    wintypes.HDC,
    ctypes.c_int,
    ctypes.c_int,
    ctypes.c_int,
    ctypes.c_int,
    wintypes.HDC,
    ctypes.c_int,
    ctypes.c_int,
    wintypes.DWORD,
]
gdi32.GetDIBits.argtypes = [
    wintypes.HDC,
    wintypes.HBITMAP,
    wintypes.UINT,
    wintypes.UINT,
    wintypes.LPVOID,
    ctypes.POINTER(BITMAPINFO),
    wintypes.UINT,
]
gdi32.DeleteObject.argtypes = [wintypes.HGDIOBJ]
gdi32.DeleteDC.argtypes = [wintypes.HDC]

INPUT_KEYBOARD = 1
KEYEVENTF_KEYUP = 0x0002
KEYEVENTF_EXTENDEDKEY = 0x0001
KEYEVENTF_SCANCODE = 0x0008
SW_RESTORE = 9
SRCCOPY = 0x00CC0020
DIB_RGB_COLORS = 0
BI_RGB = 0
MAPVK_VK_TO_VSC = 0


VK = {
    "backspace": 0x08,
    "tab": 0x09,
    "enter": 0x0D,
    "return": 0x0D,
    "shift": 0x10,
    "ctrl": 0x11,
    "control": 0x11,
    "alt": 0x12,
    "esc": 0x1B,
    "escape": 0x1B,
    "space": 0x20,
    "pageup": 0x21,
    "pagedown": 0x22,
    "end": 0x23,
    "home": 0x24,
    "left": 0x25,
    "up": 0x26,
    "right": 0x27,
    "down": 0x28,
    "insert": 0x2D,
    "delete": 0x2E,
    "f1": 0x70,
    "f2": 0x71,
    "f3": 0x72,
    "f4": 0x73,
    "f5": 0x74,
    "f6": 0x75,
    "f7": 0x76,
    "f8": 0x77,
    "f9": 0x78,
    "f10": 0x79,
    "f11": 0x7A,
    "f12": 0x7B,
}

for c in "abcdefghijklmnopqrstuvwxyz":
    VK[c] = ord(c.upper())
for c in "0123456789":
    VK[c] = ord(c)


def win_error(prefix: str) -> RuntimeError:
    return RuntimeError(f"{prefix}: Windows error {ctypes.get_last_error()}")


def window_text(hwnd: int) -> str:
    length = user32.GetWindowTextLengthW(hwnd)
    if length <= 0:
        return ""
    buf = ctypes.create_unicode_buffer(length + 1)
    user32.GetWindowTextW(hwnd, buf, length + 1)
    return buf.value


def find_window(title: str = DEFAULT_TITLE) -> int | None:
    needle = title.lower()
    found: list[int] = []

    @EnumWindowsProc
    def callback(hwnd: int, _lparam: int) -> bool:
        if not user32.IsWindowVisible(hwnd):
            return True
        text = window_text(hwnd)
        if needle in text.lower():
            found.append(hwnd)
            return False
        return True

    user32.EnumWindows(callback, 0)
    return found[0] if found else None


def wait_for_window(title: str, timeout: float) -> int:
    deadline = time.time() + timeout
    while time.time() < deadline:
        hwnd = find_window(title)
        if hwnd:
            return hwnd
        time.sleep(0.1)
    raise RuntimeError(f"Timed out waiting for a visible window containing title {title!r}")


def focus_window(hwnd: int) -> None:
    user32.ShowWindow(hwnd, SW_RESTORE)
    user32.BringWindowToTop(hwnd)
    if not user32.SetForegroundWindow(hwnd):
        # Windows sometimes declines focus changes from background processes.
        # BringWindowToTop still makes SendInput land correctly in normal local test runs.
        pass
    time.sleep(0.08)


def launch(args: argparse.Namespace) -> None:
    exe = Path(args.exe).resolve()
    if not exe.exists():
        raise FileNotFoundError(exe)
    subprocess.Popen([str(exe)], cwd=str(ROOT))
    hwnd = wait_for_window(args.title, args.timeout)
    focus_window(hwnd)
    print(f"launched hwnd=0x{hwnd:x} title={window_text(hwnd)!r}")


def resolve_window(args: argparse.Namespace) -> int:
    hwnd = find_window(args.title)
    if not hwnd:
        raise RuntimeError(f"No visible window found containing title {args.title!r}")
    focus_window(hwnd)
    return hwnd


def vk_for_key(name: str) -> int:
    key = name.strip().lower()
    if key not in VK:
        raise ValueError(f"Unknown key {name!r}; use arrows/enter/esc/f12/a-z/0-9/etc.")
    return VK[key]


EXTENDED_KEYS = {
    VK["left"],
    VK["up"],
    VK["right"],
    VK["down"],
    VK["insert"],
    VK["delete"],
    VK["home"],
    VK["end"],
    VK["pageup"],
    VK["pagedown"],
}


def send_vk(vk: int, down: bool) -> None:
    scan = user32.MapVirtualKeyW(vk, MAPVK_VK_TO_VSC)
    flags = KEYEVENTF_SCANCODE
    if vk in EXTENDED_KEYS:
        flags |= KEYEVENTF_EXTENDEDKEY
    if not down:
        flags |= KEYEVENTF_KEYUP
    event = INPUT(
        type=INPUT_KEYBOARD,
        union=INPUT_UNION(
            ki=KEYBDINPUT(
                wVk=0,
                wScan=scan,
                dwFlags=flags,
                time=0,
                dwExtraInfo=0,
            )
        ),
    )
    sent = user32.SendInput(1, ctypes.byref(event), ctypes.sizeof(INPUT))
    if sent != 1:
        raise win_error("SendInput failed")


def tap_key(name: str, hold_ms: int) -> None:
    vk = vk_for_key(name)
    send_vk(vk, True)
    time.sleep(max(0, hold_ms) / 1000.0)
    send_vk(vk, False)


def command_key(args: argparse.Namespace) -> None:
    resolve_window(args)
    tap_key(args.key, args.hold_ms)
    time.sleep(args.after_ms / 1000.0)
    print(f"sent {args.key}")


def command_keys(args: argparse.Namespace) -> None:
    resolve_window(args)
    for key in args.keys:
        tap_key(key, args.hold_ms)
        time.sleep(args.delay_ms / 1000.0)
    print("sent " + " ".join(args.keys))


def client_size(hwnd: int) -> tuple[int, int]:
    rect = RECT()
    if not user32.GetClientRect(hwnd, ctypes.byref(rect)):
        raise win_error("GetClientRect failed")
    return rect.right - rect.left, rect.bottom - rect.top


def capture_window_bmp(hwnd: int, out_path: Path) -> tuple[int, int]:
    width, height = client_size(hwnd)
    if width <= 0 or height <= 0:
        raise RuntimeError(f"Invalid client size {width}x{height}")

    hdc_window = user32.GetDC(hwnd)
    if not hdc_window:
        raise win_error("GetDC failed")
    hdc_mem = gdi32.CreateCompatibleDC(hdc_window)
    if not hdc_mem:
        user32.ReleaseDC(hwnd, hdc_window)
        raise win_error("CreateCompatibleDC failed")
    hbmp = gdi32.CreateCompatibleBitmap(hdc_window, width, height)
    if not hbmp:
        gdi32.DeleteDC(hdc_mem)
        user32.ReleaseDC(hwnd, hdc_window)
        raise win_error("CreateCompatibleBitmap failed")

    try:
        old = gdi32.SelectObject(hdc_mem, hbmp)
        if not gdi32.BitBlt(hdc_mem, 0, 0, width, height, hdc_window, 0, 0, SRCCOPY):
            raise win_error("BitBlt failed")

        bmi = BITMAPINFO()
        bmi.bmiHeader.biSize = ctypes.sizeof(BITMAPINFOHEADER)
        bmi.bmiHeader.biWidth = width
        bmi.bmiHeader.biHeight = -height
        bmi.bmiHeader.biPlanes = 1
        bmi.bmiHeader.biBitCount = 32
        bmi.bmiHeader.biCompression = BI_RGB
        bmi.bmiHeader.biSizeImage = width * height * 4
        pixel_data = (ctypes.c_ubyte * (width * height * 4))()
        lines = gdi32.GetDIBits(hdc_mem, hbmp, 0, height, pixel_data, ctypes.byref(bmi), DIB_RGB_COLORS)
        if lines != height:
            raise win_error("GetDIBits failed")

        out_path.parent.mkdir(parents=True, exist_ok=True)
        file_header_size = 14
        info_header_size = 40
        pixel_offset = file_header_size + info_header_size
        file_size = pixel_offset + len(pixel_data)
        with out_path.open("wb") as f:
            f.write(b"BM")
            f.write(struct.pack("<IHHI", file_size, 0, 0, pixel_offset))
            f.write(
                struct.pack(
                    "<IiiHHIIiiII",
                    info_header_size,
                    width,
                    -height,
                    1,
                    32,
                    BI_RGB,
                    len(pixel_data),
                    0,
                    0,
                    0,
                    0,
                )
            )
            f.write(bytes(pixel_data))

        if old:
            gdi32.SelectObject(hdc_mem, old)
    finally:
        gdi32.DeleteObject(hbmp)
        gdi32.DeleteDC(hdc_mem)
        user32.ReleaseDC(hwnd, hdc_window)

    return width, height


def command_screenshot(args: argparse.Namespace) -> None:
    hwnd = resolve_window(args)
    out_path = Path(args.out)
    if not out_path.is_absolute():
        out_path = ROOT / out_path
    width, height = capture_window_bmp(hwnd, out_path)
    print(f"screenshot {width}x{height} {out_path}")


def command_focus(args: argparse.Namespace) -> None:
    hwnd = resolve_window(args)
    print(f"focused hwnd=0x{hwnd:x} title={window_text(hwnd)!r}")


def command_smoke_menu(args: argparse.Namespace) -> None:
    hwnd = find_window(args.title)
    if not hwnd:
        launch(args)
    else:
        focus_window(hwnd)

    time.sleep(args.initial_wait_ms / 1000.0)
    if args.reveal:
        tap_key("enter", args.hold_ms)
        time.sleep(args.delay_ms / 1000.0)
    for key in ("down", "down", "enter"):
        tap_key(key, args.hold_ms)
        time.sleep(args.delay_ms / 1000.0)
    capture_window_bmp(resolve_window(args), ROOT / "debug_screenshots" / "agent_level_editor.bmp")
    tap_key("esc", args.hold_ms)
    time.sleep(args.delay_ms / 1000.0)
    capture_window_bmp(resolve_window(args), ROOT / "debug_screenshots" / "agent_back_to_main.bmp")
    print("wrote debug_screenshots/agent_level_editor.bmp and agent_back_to_main.bmp")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Drive and screenshot the local SuperMet window.")
    parser.add_argument("--title", default=DEFAULT_TITLE, help="Window-title substring to target.")
    sub = parser.add_subparsers(dest="command", required=True)

    launch_p = sub.add_parser("launch", help="Start build/bin-x64-Debug/sm.exe and focus it.")
    launch_p.add_argument("--exe", default=str(DEFAULT_EXE))
    launch_p.add_argument("--timeout", type=float, default=10.0)
    launch_p.set_defaults(func=launch)

    focus_p = sub.add_parser("focus", help="Focus the existing SuperMet window.")
    focus_p.set_defaults(func=command_focus)

    key_p = sub.add_parser("key", help="Tap one key in the SuperMet window.")
    key_p.add_argument("key")
    key_p.add_argument("--hold-ms", type=int, default=40)
    key_p.add_argument("--after-ms", type=int, default=80)
    key_p.set_defaults(func=command_key)

    keys_p = sub.add_parser("keys", help="Tap a sequence of keys in the SuperMet window.")
    keys_p.add_argument("keys", nargs="+")
    keys_p.add_argument("--hold-ms", type=int, default=40)
    keys_p.add_argument("--delay-ms", type=int, default=120)
    keys_p.set_defaults(func=command_keys)

    shot_p = sub.add_parser("screenshot", help="Capture the SuperMet client area as a BMP.")
    shot_p.add_argument("--out", default=str(ROOT / "debug_screenshots" / "agent.bmp"))
    shot_p.set_defaults(func=command_screenshot)

    smoke_p = sub.add_parser("smoke-menu", help="From main menu, open Level Editor, capture it, press Esc, capture main menu.")
    smoke_p.add_argument("--exe", default=str(DEFAULT_EXE))
    smoke_p.add_argument("--timeout", type=float, default=10.0)
    smoke_p.add_argument("--hold-ms", type=int, default=40)
    smoke_p.add_argument("--delay-ms", type=int, default=220)
    smoke_p.add_argument("--initial-wait-ms", type=int, default=500)
    smoke_p.add_argument("--reveal", action="store_true", help="Press Enter first to reveal the menu from intro playback.")
    smoke_p.set_defaults(func=command_smoke_menu)

    return parser


def main(argv: list[str]) -> int:
    if os.name != "nt":
        print("sm_ui_test.py currently targets Windows only.", file=sys.stderr)
        return 2
    args = build_parser().parse_args(argv)
    args.func(args)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
