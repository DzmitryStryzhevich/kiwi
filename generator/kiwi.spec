# -*- mode: python ; coding: utf-8 -*-

import shutil


clang_format = shutil.which("clang-format")
if clang_format is None:
    raise RuntimeError("clang-format was not found. Install generator requirements first.")


a = Analysis(
    ['kiwi_codegen_cli_app.py'],
    pathex=[],
    binaries=[(clang_format, '.')],
    datas=[
        ('../osal', 'osal'),
        ('../doc/kiwi.png', 'doc'),
        ('../doc/kiwi_header.png', 'doc'),
        ('../doc/kiwi_window.png', 'doc'),
        ('../doc/kiwi.ico', 'doc'),
    ],
    hiddenimports=[],
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
    name='kiwi',
    icon='../doc/kiwi.ico',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    upx_exclude=[],
    runtime_tmpdir=None,
    console=True,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
)
