# -*- mode: python ; coding: utf-8 -*-


a = Analysis(
    ['kiwi_codegen_ui_app.py'],
    pathex=[],
    binaries=[],
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
    name='kiwi_gui',
    icon='../doc/kiwi.ico',
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
