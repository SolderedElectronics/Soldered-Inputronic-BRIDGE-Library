# -*- mode: python ; coding: utf-8 -*-
from PyInstaller.utils.hooks import collect_data_files

a = Analysis(
    ['firmware_flasher.py'],
    pathex=[],
    binaries=[],
    datas=[
        ('../firmware_binaries/*.bin', 'firmware_binaries'),
        *collect_data_files('esptool'),
    ],
    hiddenimports=[
        'esptool',
        'esptool.targets',
        'esptool.loader',
        'serial',
        'serial.tools',
        'serial.tools.list_ports',
        'customtkinter',
        'tkinter',
        'tkinter.messagebox',
    ],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[],
    noarchive=False,
)

pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    a.binaries,
    a.datas,
    [],
    name='firmware_flasher',
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
