#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later

"""Shared, cross-platform Rekordpod installer implementation.

The GUI supplies one user-selected volume root.  This module deliberately does
not enumerate disks or search outside that root.
"""

from __future__ import annotations

import contextlib
import hashlib
import importlib
import io
import json
import os
from pathlib import Path
import platform
import re
import shutil
import stat
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass
from types import SimpleNamespace
from typing import Callable, Iterable
import zipfile


APP_NAME = "Rekordpod Installer"
APP_VERSION = "1.0.0-beta.2"
CACHE_FORMAT_VERSION = "rekordpod-installer-cache-1"

TARGET_ASSETS = {
    "ipod6g": "rekordpod-public-beta-2-ipod6g.zip",
}

PERSISTENT_FILES = (
    ".rockbox/rekordpod/rekordpod.cfg",
    ".rockbox/rekordpod/state/macros.a",
    ".rockbox/rekordpod/state/macros.b",
    ".rockbox/rekordpod/tool-macros.rbm",
    ".rockbox/rekordpod/tool-macros.rbm.tmp",
    ".rockbox/rekordpod/tool-macros.rbm.prev",
    ".rekordpod-macros.rbm",
    ".rekordpod-macros.rbm.tmp",
    ".rekordpod-macros.rbm.prev",
    ".rekordpod-workflows.rbl",
    ".rekordpod-workflows.rbl.tmp",
    ".rockbox/rekordpod/playlist-workflows.rbl",
)

ProgressCallback = Callable[[int, str], None]
LogCallback = Callable[[str], None]


class InstallerError(RuntimeError):
    """A plain-language error safe to show in the installer UI."""


@dataclass(frozen=True)
class ValidationResult:
    root: Path
    target: str
    installed_version: str
    database: Path
    analysis_root: Path
    release_zip: Path
    free_bytes: int


@dataclass(frozen=True)
class BuildResult:
    package: Path
    summary: dict[str, int]
    reused_cache: bool


@dataclass(frozen=True)
class InstallResult:
    backup_directory: Path
    ejected: bool


def _noop_progress(_percent: int, _label: str) -> None:
    pass


def _noop_log(_message: str) -> None:
    pass


def bundle_root() -> Path:
    frozen = getattr(sys, "_MEIPASS", None)
    return Path(frozen) if frozen else Path(__file__).resolve().parent


def asset_directories() -> Iterable[Path]:
    override = os.environ.get("REKORDPOD_ASSET_DIR")
    if override:
        yield Path(override).expanduser()
    yield bundle_root() / "rekordpod_assets"
    yield Path(__file__).resolve().parent / "assets"


def find_asset(filename: str) -> Path:
    for directory in asset_directories():
        candidate = directory / filename
        if candidate.is_file():
            return candidate
    raise InstallerError(
        f"The installer is missing {filename}. Download the complete "
        "Rekordpod installer package and try again."
    )


def normalize_selected_root(value: str) -> Path:
    text = value.strip().strip('"').strip()
    if not text:
        raise InstallerError("Choose the mounted iPod before continuing.")

    if os.name == "nt" and re.fullmatch(r"[A-Za-z]:?", text):
        text = text[0].upper() + ":\\"

    root = Path(text).expanduser()
    if not root.is_absolute():
        raise InstallerError(
            "Enter the complete iPod path, such as E:\\ on Windows or "
            "/Volumes/RIZZPOD on macOS."
        )

    if os.name == "nt":
        system_drive = os.environ.get("SystemDrive", "C:").rstrip("\\/").casefold()
        selected_drive = root.drive.rstrip("\\/").casefold()
        if selected_drive and selected_drive == system_drive:
            raise InstallerError("The Windows system drive cannot be selected.")

    return root


def _permission_message(path: Path) -> str:
    if sys.platform == "darwin":
        return (
            "macOS blocked access to the selected iPod. Open System Settings > "
            "Privacy & Security > Files and Folders, allow Removable Volumes for "
            "Rekordpod Installer, then reopen the installer and choose the iPod again."
        )
    return f"Access to the selected iPod was denied: {path}"


def _require_selected_path(
    path: Path,
    *,
    directory: bool,
    missing_message: str,
) -> None:
    """Check a selected-volume path without hiding permission failures.

    ``Path.is_file`` and ``Path.is_dir`` intentionally collapse several OS
    errors into ``False``.  That made a macOS removable-volume denial look like
    a missing Rockbox installation, so validation uses ``stat`` directly.
    """
    try:
        mode = path.stat().st_mode
    except PermissionError as exc:
        raise InstallerError(_permission_message(path)) from exc
    except FileNotFoundError as exc:
        raise InstallerError(missing_message) from exc
    except OSError as exc:
        raise InstallerError(f"The selected iPod could not be read: {exc}") from exc

    correct_kind = stat.S_ISDIR(mode) if directory else stat.S_ISREG(mode)
    if not correct_kind:
        raise InstallerError(missing_message)


def parse_rockbox_info(path: Path) -> dict[str, str]:
    try:
        body = path.read_text(encoding="utf-8", errors="replace")
    except PermissionError as exc:
        raise InstallerError(_permission_message(path)) from exc
    except OSError as exc:
        raise InstallerError(f"Rockbox information could not be read: {exc}") from exc
    values: dict[str, str] = {}
    for line in body.splitlines():
        key, separator, value = line.partition(":")
        if separator:
            values[key.strip()] = value.strip()
    return values


def _zip_target(path: Path) -> str:
    try:
        with zipfile.ZipFile(path) as archive:
            bad = archive.testzip()
            if bad:
                raise InstallerError(f"The release archive is damaged at {bad}.")
            try:
                info = archive.read(".rockbox/rockbox-info.txt").decode(
                    "utf-8", "replace"
                )
            except KeyError as exc:
                raise InstallerError(
                    "The release archive does not contain Rockbox build information."
                ) from exc
    except (OSError, zipfile.BadZipFile) as exc:
        raise InstallerError(f"The release archive could not be opened: {exc}") from exc
    for line in info.splitlines():
        if line.startswith("Target:"):
            return line.partition(":")[2].strip().lower()
    raise InstallerError("The release archive does not identify its Rockbox target.")


def validate_selected_volume(value: str) -> ValidationResult:
    """Validate only ``value``; never enumerate or search for other volumes."""
    root = normalize_selected_root(value)
    _require_selected_path(
        root,
        directory=True,
        missing_message=f"The selected iPod path does not exist: {root}",
    )

    database = root / "PIONEER" / "rekordbox" / "export.pdb"
    analysis_root = root / "PIONEER" / "USBANLZ"
    rockbox_info = root / ".rockbox" / "rockbox-info.txt"

    _require_selected_path(
        database,
        directory=False,
        missing_message=(
            "The selected drive does not contain PIONEER/rekordbox/export.pdb."
        ),
    )
    _require_selected_path(
        analysis_root,
        directory=True,
        missing_message=(
            "The selected drive does not contain the PIONEER/USBANLZ analysis folder."
        ),
    )
    _require_selected_path(
        rockbox_info,
        directory=False,
        missing_message=(
            "The selected drive does not contain .rockbox/rockbox-info.txt. "
            "Install Rockbox first."
        ),
    )

    info = parse_rockbox_info(rockbox_info)
    target = info.get("Target", "").lower()
    if target not in TARGET_ASSETS:
        shown = target or "unknown"
        raise InstallerError(
            f"This beta does not support the selected Rockbox target ({shown})."
        )

    release_zip = find_asset(TARGET_ASSETS[target])
    archive_target = _zip_target(release_zip)
    if archive_target != target:
        raise InstallerError(
            f"The bundled {archive_target} release does not match the selected {target} iPod."
        )

    try:
        free_bytes = shutil.disk_usage(root).free
    except OSError as exc:
        raise InstallerError(f"Free space on the selected iPod could not be read: {exc}") from exc

    return ValidationResult(
        root=root,
        target=target,
        installed_version=info.get("Version", "unknown"),
        database=database,
        analysis_root=analysis_root,
        release_zip=release_zip,
        free_bytes=free_bytes,
    )


def default_cache_root() -> Path:
    if os.name == "nt":
        base = Path(os.environ.get("LOCALAPPDATA", Path.home() / "AppData" / "Local"))
        return base / "Rekordpod" / "Cache"
    if sys.platform == "darwin":
        return Path.home() / "Library" / "Caches" / "Rekordpod"
    return Path(os.environ.get("XDG_CACHE_HOME", Path.home() / ".cache")) / "rekordpod"


def default_backup_root() -> Path:
    return Path.home() / "Documents" / "Rekordpod Backups"


def _hash_file(path: Path, digest: "hashlib._Hash") -> None:
    with path.open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            digest.update(chunk)


def library_fingerprint(
    selected: ValidationResult,
    progress: ProgressCallback = _noop_progress,
) -> tuple[str, int]:
    """Fingerprint the selected library without reading or probing other drives."""
    progress(1, "Checking the selected Rekordbox library")
    digest = hashlib.sha256()
    digest.update(CACHE_FORMAT_VERSION.encode("ascii"))
    digest.update(selected.target.encode("ascii"))
    _hash_file(selected.release_zip, digest)
    _hash_file(selected.database, digest)

    records: list[tuple[str, int, int]] = []
    for directory, directories, filenames in os.walk(selected.analysis_root):
        directories.sort()
        for filename in sorted(filenames):
            suffix = Path(filename).suffix.upper()
            if not filename.upper().startswith("ANLZ") or suffix not in {".DAT", ".EXT"}:
                continue
            path = Path(directory) / filename
            try:
                stat = path.stat()
            except OSError as exc:
                raise InstallerError(f"An analysis file could not be read: {path}: {exc}") from exc
            relative = path.relative_to(selected.analysis_root).as_posix()
            records.append((relative, stat.st_size, stat.st_mtime_ns))

    total = max(1, len(records))
    for index, (relative, size, modified) in enumerate(records, 1):
        digest.update(relative.encode("utf-8", "surrogateescape"))
        digest.update(b"\0")
        digest.update(str(size).encode("ascii"))
        digest.update(b":")
        digest.update(str(modified).encode("ascii"))
        if index == total or index % 128 == 0:
            progress(1 + index * 4 // total, f"Checking analysis files ({index:,}/{total:,})")
    return digest.hexdigest(), len(records)


class ProgressCapture(io.TextIOBase):
    """Turn the existing command-line progress output into GUI events."""

    def __init__(self, progress: ProgressCallback, log: LogCallback):
        self.progress = progress
        self.log = log
        self.buffer = ""
        self.last_log = ""

    def writable(self) -> bool:
        return True

    def write(self, text: str) -> int:
        self.buffer += text
        while True:
            newline = min(
                (position for position in (self.buffer.find("\r"), self.buffer.find("\n"))
                 if position >= 0),
                default=-1,
            )
            if newline < 0:
                break
            line = self.buffer[:newline].strip()
            self.buffer = self.buffer[newline + 1:]
            if line:
                self._handle(line)
        return len(text)

    def flush(self) -> None:
        line = self.buffer.strip()
        if line:
            self._handle(line)
        self.buffer = ""

    def _handle(self, line: str) -> None:
        match = re.search(r"(\d{1,3})%", line)
        percent = min(100, int(match.group(1))) if match else None
        if line.startswith("ANLZ files") and percent is not None:
            self.progress(5 + percent * 35 // 100, "Reading Rekordbox analysis")
        elif line.startswith("Tracks") and percent is not None:
            self.progress(40 + percent * 14 // 100, "Indexing tracks")
        elif line.startswith("Playlists") and percent is not None:
            self.progress(54 + percent * 6 // 100, "Indexing playlists")
        elif line.startswith("[") and percent is not None:
            self.progress(60 + percent * 27 // 100, "Packing the device cache")
        elif line != self.last_log:
            self.log(line)
            self.last_log = line


def _load_builder_modules():
    source_root = Path(__file__).resolve().parent.parent
    installer_root = Path(__file__).resolve().parent
    search_roots = (
        installer_root / "vendor" / "rekordbox-pdb" / "src",
        installer_root / "vendor" / "pyrekordbox",
        source_root,
    )
    for search_root in reversed(search_roots):
        if str(search_root) not in sys.path:
            sys.path.insert(0, str(search_root))
    try:
        importer = importlib.import_module("import_rekordbox")
        packager = importlib.import_module("build_device_cache")
    except Exception as exc:
        raise InstallerError(f"The Rekordpod cache builder could not start: {exc}") from exc
    return importer, packager


def validate_device_package(path: Path, target: str) -> None:
    try:
        with zipfile.ZipFile(path) as archive:
            bad = archive.testzip()
            if bad:
                raise InstallerError(f"The completed package is damaged at {bad}.")
            names = set(archive.namelist())
    except (OSError, zipfile.BadZipFile) as exc:
        raise InstallerError(f"The completed package could not be opened: {exc}") from exc
    required = {
        ".rockbox/rockbox.ipod",
        ".rockbox/rockbox-info.txt",
        ".rockbox/rocks/apps/rekordpod.rock",
        ".rockbox/rekordpod/library.rbi",
    }
    missing = sorted(required - names)
    if missing:
        raise InstallerError("The completed package is missing " + ", ".join(missing) + ".")
    if _zip_target(path) != target:
        raise InstallerError("The completed package does not match the selected iPod target.")


def build_or_reuse_package(
    selected: ValidationResult,
    *,
    cache_root: Path | None = None,
    force_rebuild: bool = False,
    progress: ProgressCallback = _noop_progress,
    log: LogCallback = _noop_log,
) -> BuildResult:
    cache_root = cache_root or default_cache_root()
    fingerprint, analysis_file_count = library_fingerprint(selected, progress)
    cache_directory = cache_root / selected.target / fingerprint
    package = cache_directory / "rekordpod-device.zip"
    summary_path = cache_directory / "summary.json"

    if not force_rebuild and package.is_file() and summary_path.is_file():
        try:
            validate_device_package(package, selected.target)
            summary = json.loads(summary_path.read_text(encoding="utf-8"))
        except (InstallerError, OSError, json.JSONDecodeError) as exc:
            log(f"The saved cache was invalid and will be rebuilt: {exc}")
            package.unlink(missing_ok=True)
            summary_path.unlink(missing_ok=True)
        else:
            progress(88, "Using the unchanged Rekordpod cache")
            return BuildResult(package=package, summary=summary, reused_cache=True)

    cache_directory.mkdir(parents=True, exist_ok=True)
    work_directory = Path(tempfile.mkdtemp(prefix="rekordpod-build-", dir=cache_directory))
    importer, packager = _load_builder_modules()
    capture = ProgressCapture(progress, log)
    try:
        imported = work_directory / "imported"
        progress(5, f"Reading {analysis_file_count:,} selected analysis files")
        with contextlib.redirect_stdout(capture), contextlib.redirect_stderr(capture):
            summary = importer.convert(
                selected.database, imported, selected.analysis_root
            )
            temporary_package = work_directory / "rekordpod-device.zip"
            packager.build(
                SimpleNamespace(
                    database=imported / "rekordpod-library.sqlite",
                    rockbox_zip=selected.release_zip,
                    output=temporary_package,
                )
            )
        capture.flush()
        validate_device_package(temporary_package, selected.target)
        temporary_summary = work_directory / "summary.json"
        temporary_summary.write_text(
            json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        os.replace(temporary_package, package)
        os.replace(temporary_summary, summary_path)
    except InstallerError:
        raise
    except Exception as exc:
        raise InstallerError(f"The Rekordpod cache could not be built: {exc}") from exc
    finally:
        shutil.rmtree(work_directory, ignore_errors=True)

    progress(88, "Rekordpod cache ready")
    return BuildResult(package=package, summary=summary, reused_cache=False)


def _safe_archive_name(name: str) -> Path:
    normalized = name.replace("\\", "/")
    relative = Path(normalized)
    if relative.is_absolute() or ".." in relative.parts:
        raise InstallerError(f"The release archive contains an unsafe path: {name}")
    if not relative.parts or relative.parts[0] != ".rockbox":
        raise InstallerError(f"The release archive contains an unexpected path: {name}")
    return relative


def _atomic_copy_stream(source, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    handle, temporary_name = tempfile.mkstemp(
        prefix=".rekordpod-install-", suffix=".tmp", dir=destination.parent
    )
    try:
        with os.fdopen(handle, "wb") as target:
            shutil.copyfileobj(source, target, length=1024 * 1024)
            target.flush()
            os.fsync(target.fileno())
        os.replace(temporary_name, destination)
    finally:
        if os.path.exists(temporary_name):
            os.unlink(temporary_name)


def _atomic_copy_file(source: Path, destination: Path) -> None:
    with source.open("rb") as stream:
        _atomic_copy_stream(stream, destination)


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    _hash_file(path, digest)
    return digest.hexdigest()


def create_backup(
    selected: ValidationResult,
    *,
    backup_root: Path | None = None,
) -> Path:
    backup_root = backup_root or default_backup_root()
    stamp = time.strftime("%Y-%m-%d_%H%M%S")
    label = re.sub(r"[^A-Za-z0-9._-]+", "-", selected.root.name or "IPOD")
    destination = backup_root / f"{stamp}-{label}"
    suffix = 1
    while destination.exists():
        destination = backup_root / f"{stamp}-{label}-{suffix}"
        suffix += 1
    destination.mkdir(parents=True)

    try:
        shutil.copy2(selected.database, destination / "export.pdb")
        persistent_root = destination / "persistent"
        preserved: list[str] = []
        for relative in PERSISTENT_FILES:
            source = selected.root / relative
            if not source.is_file():
                continue
            target = persistent_root / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
            preserved.append(relative)
        manifest = {
            "installer_version": APP_VERSION,
            "source": str(selected.root),
            "target": selected.target,
            "installed_rockbox_version": selected.installed_version,
            "export_pdb_sha256": _sha256(destination / "export.pdb"),
            "persistent_files": preserved,
        }
        (destination / "backup.json").write_text(
            json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
    except Exception as exc:
        shutil.rmtree(destination, ignore_errors=True)
        raise InstallerError(f"The safety backup could not be created: {exc}") from exc
    return destination


def _restore_persistent(selected: ValidationResult, backup: Path) -> None:
    persistent_root = backup / "persistent"
    if not persistent_root.is_dir():
        return
    for relative in PERSISTENT_FILES:
        source = persistent_root / relative
        if source.is_file():
            _atomic_copy_file(source, selected.root / relative)


def _flush_selected_volume(root: Path, log: LogCallback) -> None:
    if sys.platform == "darwin" and Path("/bin/sync").exists():
        result = subprocess.run(["/bin/sync"], check=False, capture_output=True, text=True)
        if result.returncode:
            log("macOS could not complete its final sync; eject the iPod in Finder.")
    elif os.name == "nt":
        # Every installed file has already been individually flushed with fsync.
        log("Installed files were flushed. Windows will now request safe removal.")


def eject_selected_volume(root: Path, log: LogCallback = _noop_log) -> bool:
    """Attempt to eject only the user-selected volume."""
    try:
        if sys.platform == "darwin":
            result = subprocess.run(
                ["/usr/sbin/diskutil", "eject", str(root)],
                check=False,
                capture_output=True,
                text=True,
            )
            if result.returncode:
                log(result.stderr.strip() or "macOS did not eject the selected iPod.")
            return result.returncode == 0
        if os.name == "nt":
            drive = root.drive or str(root)
            script = (
                "$item=(New-Object -ComObject Shell.Application).Namespace(17)"
                ".ParseName($env:REKORDPOD_SELECTED_DRIVE);"
                "if($null -eq $item){exit 2};$item.InvokeVerb('Eject')"
            )
            environment = os.environ.copy()
            environment["REKORDPOD_SELECTED_DRIVE"] = drive
            result = subprocess.run(
                ["powershell.exe", "-NoProfile", "-NonInteractive", "-Command", script],
                env=environment,
                check=False,
                capture_output=True,
                text=True,
            )
            if result.returncode:
                log(result.stderr.strip() or "Windows did not eject the selected iPod.")
            return result.returncode == 0
    except OSError as exc:
        log(f"Automatic eject was unavailable: {exc}")
    return False


def install_package(
    selected: ValidationResult,
    package: Path,
    *,
    backup_root: Path | None = None,
    eject: bool = True,
    progress: ProgressCallback = _noop_progress,
    log: LogCallback = _noop_log,
) -> InstallResult:
    validate_device_package(package, selected.target)
    backup: Path | None = None
    try:
        with zipfile.ZipFile(package) as archive:
            members = [item for item in archive.infolist() if not item.is_dir()]
            safe_members = [(item, _safe_archive_name(item.filename)) for item in members]
            uncompressed = sum(item.file_size for item in members)
            if selected.free_bytes < uncompressed + 8 * 1024 * 1024:
                raise InstallerError("The selected iPod does not have enough free space.")

            progress(89, "Creating a safety backup")
            backup = create_backup(selected, backup_root=backup_root)
            total = max(1, len(safe_members))
            for index, (item, relative) in enumerate(safe_members, 1):
                with archive.open(item) as source:
                    _atomic_copy_stream(source, selected.root / relative)
                if index == total or index % 8 == 0:
                    progress(
                        90 + index * 8 // total,
                        f"Installing Rekordpod ({index:,}/{total:,})",
                    )

        _restore_persistent(selected, backup)
        name_file = selected.root / ".rockbox" / "rekordpod" / "device-name.txt"
        _atomic_copy_stream(
            io.BytesIO(((selected.root.name or "REKORDPOD") + "\n").encode("utf-8")),
            name_file,
        )
        progress(99, "Flushing installed files")
        _flush_selected_volume(selected.root, log)
    except InstallerError:
        raise
    except Exception as exc:
        recovery = (
            f" The safety backup is at {backup}."
            if backup is not None
            else " The iPod was not changed."
        )
        raise InstallerError(
            f"Installation stopped. Keep the iPod connected.{recovery} Error: {exc}"
        ) from exc

    ejected = eject_selected_volume(selected.root, log) if eject else False
    progress(100, "Rekordpod installed")
    return InstallResult(backup_directory=backup, ejected=ejected)
