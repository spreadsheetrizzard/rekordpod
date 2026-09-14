# -*- mode: python ; coding: utf-8 -*-

import os
from pathlib import Path
import sys

from PyInstaller.utils.hooks import copy_metadata


installer_dir = Path(SPECPATH)
rekordpod_dir = installer_dir.parent
rekordbox_pdb_src = installer_dir / "vendor" / "rekordbox-pdb" / "src"
pyrekordbox_src = installer_dir / "vendor" / "pyrekordbox"
asset_dir = Path(os.environ.get("REKORDPOD_ASSET_DIR", installer_dir / "assets"))
ipod6g = asset_dir / "rekordpod-public-beta-1-ipod6g.zip"

for required in (ipod6g,):
    if not required.is_file():
        raise SystemExit(f"Missing release asset: {required}")

hiddenimports = [
    "import_rekordbox",
    "build_device_cache",
    "rekordbox_pdb",
    "rekordbox_pdb.pdb",
    "pyrekordbox",
    "pyrekordbox.anlz",
    "pyrekordbox.anlz.file",
    "pyrekordbox.anlz.structs",
    "pyrekordbox.anlz.tags",
]

datas = [
    (str(ipod6g), "rekordpod_assets"),
    (str(installer_dir / "THIRD_PARTY_NOTICES.md"), "."),
    (str(installer_dir / "vendor" / "rekordbox-pdb" / "LICENSE"),
     "licenses/rekordbox-pdb"),
    (str(installer_dir / "vendor" / "pyrekordbox" / "LICENSE"),
     "licenses/pyrekordbox"),
]
datas += copy_metadata("numpy", recursive=True)
datas += copy_metadata("construct", recursive=True)

a = Analysis(
    [str(installer_dir / "installer_gui.py")],
    pathex=[
        str(installer_dir),
        str(rekordpod_dir),
        str(rekordbox_pdb_src),
        str(pyrekordbox_src),
    ],
    binaries=[],
    datas=datas,
    hiddenimports=hiddenimports,
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[],
    noarchive=False,
    optimize=1,
)
pyz = PYZ(a.pure)

if sys.platform == "darwin":
    exe = EXE(
        pyz,
        a.scripts,
        [],
        exclude_binaries=True,
        name="Rekordpod Installer",
        debug=False,
        bootloader_ignore_signals=False,
        strip=False,
        upx=False,
        console=False,
        argv_emulation=False,
        target_arch=None,
        codesign_identity=os.environ.get("REKORDPOD_CODESIGN_IDENTITY") or None,
        entitlements_file=None,
    )
    collected = COLLECT(
        exe,
        a.binaries,
        a.datas,
        strip=False,
        upx=False,
        name="Rekordpod Installer",
    )
    app = BUNDLE(
        collected,
        name="Rekordpod Installer.app",
        icon=None,
        bundle_identifier="org.rockbox.rekordpod.installer",
        version="1.0.0b1",
        info_plist={
            "CFBundleDisplayName": "Rekordpod Installer",
            "CFBundleShortVersionString": "1.0.0-beta.1",
            "NSHighResolutionCapable": True,
            "NSRemovableVolumesUsageDescription": (
                "Rekordpod reads the iPod you select and installs its firmware "
                "and prepared library cache."
            ),
            "NSDocumentsFolderUsageDescription": (
                "Rekordpod saves a safety backup before installation."
            ),
        },
    )
else:
    exe = EXE(
        pyz,
        a.scripts,
        a.binaries,
        a.datas,
        [],
        name="Rekordpod Installer",
        debug=False,
        bootloader_ignore_signals=False,
        strip=False,
        upx=False,
        console=False,
        disable_windowed_traceback=False,
        argv_emulation=False,
    )
