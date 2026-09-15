# Optional Rekordpod desktop recovery tool

This packaging path is retained for maintainers, recovery experiments, and
cross-checking the on-device cache format. It is not distributed with the
public beta and is not required on Windows or macOS. Ordinary users install the
same Rockbox overlay ZIP on every desktop platform, then let Rekordpod build its
index and analysis bridge on the iPod.

This directory can build the same optional tool for Windows and macOS. The
GUI, validation, cache generation, safety backup, copy order, messages, and
progress stages are shared Python code. Only packaging and the operating
system's final eject request differ.

The installer never enumerates drives. The user must type a drive root or pick
the mounted iPod folder. Validation reads only that selected location and
requires all of the following before installation is offered:

- `PIONEER/rekordbox/export.pdb`
- `PIONEER/USBANLZ`
- `.rockbox/rockbox-info.txt`
- the supported `ipod6g` target
- the matching, integrity-checked Rekordpod release archive

On Windows the system drive is rejected even if entered explicitly.

## Release inputs

Place these exact files together in an asset directory:

- `rekordpod-public-beta-2-ipod6g.zip`

The build scripts default to `installer/assets`, but accept another asset
directory as their first argument. Release archives are deliberately not kept
in Git.

The iPod Classic ZIP is embedded in the finished installer. The selected iPod's
`.rockbox/rockbox-info.txt` must identify the supported `ipod6g` target. The
tool must not mix a Rekordpod plugin or codecs with an unrelated Rockbox daily
build. Other targets are rejected by the diagnostic tool.

## Windows EXE

Install 64-bit Python 3.12, open PowerShell in this directory, and run:

```powershell
.\build_windows.ps1 C:\path\to\release-assets C:\path\to\output
```

This creates one windowed `Rekordpod Installer.exe` plus a SHA-256 file.
The portable Windows builder bundle also includes `Build Rekordpod
Installer.cmd`; double-click it to run the same build with the bundled release
assets and place the result in `dist-release`.
PyInstaller is not a cross-compiler: the Windows executable must be produced
on Windows or a Windows CI runner.

An unsigned test executable can trigger Microsoft SmartScreen. Do not publish
these maintainer builds as public-beta installation assets.

## macOS app

Run:

```sh
./build_macos.sh /path/to/release-assets /path/to/output
```

This creates `Rekordpod Installer.app` and a ZIP that preserves the app bundle.
The app replaces the `.command` user experience; the old command remains a
transparent fallback for testing.

An unsigned development build must be opened with Control-click, Open. Set
`REKORDPOD_CODESIGN_IDENTITY` while building to pass a signing identity to
PyInstaller for controlled internal distribution.

## Cache and safety behavior

The first run reads the selected Rekordbox Device Library and its DAT/EXT ANLZ
files, then builds the complete device package on the computer. The iPod is not
changed until that package passes validation.

The host cache key includes:

- the complete `export.pdb` contents;
- the matching Rekordpod firmware archive;
- relative path, size, and modification time for every selected DAT/EXT file;
- the installer cache-format version.

An unchanged library therefore reuses its completed package. The user can
force a rebuild from the GUI. Cache locations are `%LOCALAPPDATA%/Rekordpod`
on Windows and `~/Library/Caches/Rekordpod` on macOS.

Immediately before copying, the installer saves `export.pdb` and existing
Rekordpod settings/workflows under `Documents/Rekordpod Backups`. Every target
file is written to a temporary neighbor, flushed, and replaced. Other music and
Rekordbox analysis files are never erased.

## Tests

The core safety tests do not need Rekordbox parser dependencies:

```sh
python3 -m unittest -v test_installer_core.py
```

Before using a frozen recovery tool, test it with a disposable FAT32 fixture
and an independently backed-up iPod. Confirm that the user-selected-path
guarantee, target refusal, cache reuse, backup, overlay install, failure
handling, and eject behavior match on that operating system.
