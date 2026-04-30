# -*- mode: python ; coding: utf-8 -*-


a = Analysis(
    ['MTGTokenDownloader.py'],
    pathex=[],
    binaries=[],
    datas=[],
    hiddenimports=['aiohttp', 'aiohttp.resolver', 'aiohttp.connector', 'PIL', 'PIL.Image', 'multidict._multidict_py', 'yarl'],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[],
    noarchive=False,
    optimize=0,
)
pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    a.binaries,
    a.datas,
    [],
    name='MTG Token Downloader',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    upx_exclude=[],
    runtime_tmpdir=None,
    console=False,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
)
app = BUNDLE(
    exe,
    name='MTG Token Downloader.app',
    icon=None,
    bundle_identifier=None,
)
