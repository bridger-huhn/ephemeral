#!/usr/bin/env python3
"""
build.py
Packages MTGTokenDownloader.py into a standalone app using PyInstaller.

Mac:   produces  dist/MTG Token Downloader.app   (double-click to run)
Win:   produces  dist/MTG Token Downloader.exe

Usage:
    python3 build.py

Requirements (install once):
    pip install pyinstaller aiohttp Pillow
"""

import sys
import subprocess
import platform

APP_NAME   = "MTG Token Downloader"
ENTRY      = "MTGTokenDownloader.py"

def run(cmd):
    print(f"\n$ {' '.join(cmd)}\n")
    result = subprocess.run(cmd)
    if result.returncode != 0:
        print(f"\nBuild failed (exit {result.returncode})")
        sys.exit(result.returncode)

def main():
    is_mac = platform.system() == "Darwin"
    is_win = platform.system() == "Windows"

    print("=" * 60)
    print(f"  Building: {APP_NAME}")
    print(f"  Platform: {platform.system()} {platform.machine()}")
    print("=" * 60)

    # Check PyInstaller is available
    try:
        import PyInstaller
    except ImportError:
        print("\nPyInstaller not found. Installing...")
        run([sys.executable, "-m", "pip", "install", "pyinstaller"])

    cmd = [
        sys.executable, "-m", "PyInstaller",
        "--name", APP_NAME,
        "--onefile",               # single file / single .app bundle
        "--clean",                 # start fresh each time
        "--noconfirm",             # overwrite dist/ without asking
        # Hidden imports that PyInstaller often misses with aiohttp
        "--hidden-import", "aiohttp",
        "--hidden-import", "aiohttp.resolver",
        "--hidden-import", "aiohttp.connector",
        "--hidden-import", "PIL",
        "--hidden-import", "PIL.Image",
        "--hidden-import", "multidict._multidict_py",
        "--hidden-import", "yarl",
    ]

    if is_mac:
        cmd += [
            "--windowed",          # no terminal window on Mac
        ]
    elif is_win:
        cmd += [
            "--windowed",          # no console window on Windows
        ]

    cmd.append(ENTRY)
    run(cmd)

    print("\n" + "=" * 60)
    print(f"  Build complete!")
    if is_mac:
        print(f"  → dist/{APP_NAME}.app")
        print(f"    Drag to Applications or double-click to run.")
    elif is_win:
        print(f"  → dist\\{APP_NAME}.exe")
        print(f"    Share the .exe — no Python needed on the target machine.")
    else:
        print(f"  → dist/{APP_NAME}")
    print("=" * 60)

if __name__ == "__main__":
    main()
