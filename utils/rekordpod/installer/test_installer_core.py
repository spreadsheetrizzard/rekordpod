#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later

from __future__ import annotations

import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest import mock
import zipfile

import installer_core as core


def write_zip(path: Path, target: str, *, device_cache: bool = False,
              malicious: bool = False) -> None:
    with zipfile.ZipFile(path, "w") as archive:
        archive.writestr(
            ".rockbox/rockbox-info.txt",
            f"Target: {target}\nVersion: test-build\n",
        )
        archive.writestr(".rockbox/rockbox.ipod", b"firmware")
        archive.writestr(".rockbox/rocks/apps/rekordpod.rock", b"plugin")
        if device_cache:
            archive.writestr(".rockbox/rekordpod/library.rbi", b"RBI3")
        if malicious:
            archive.writestr("../outside.txt", b"unsafe")


class InstallerCoreTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.assets = self.root / "assets"
        self.assets.mkdir()
        write_zip(
            self.assets / core.TARGET_ASSETS["ipod6g"],
            "ipod6g",
        )
        self.volume = self.root / "RIZZPOD"
        (self.volume / ".rockbox").mkdir(parents=True)
        (self.volume / ".rockbox" / "rockbox-info.txt").write_text(
            "Target: ipod6g\nVersion: daily-test\n", encoding="utf-8"
        )
        database = self.volume / "PIONEER" / "rekordbox" / "export.pdb"
        database.parent.mkdir(parents=True)
        database.write_bytes(b"database")
        analysis = self.volume / "PIONEER" / "USBANLZ" / "P000" / "00000000"
        analysis.mkdir(parents=True)
        (analysis / "ANLZ0000.DAT").write_bytes(b"dat")
        (analysis / "ANLZ0000.EXT").write_bytes(b"ext")
        (analysis / "ANLZ0000.2EX").write_bytes(b"ignored")
        self.environment = mock.patch.dict(
            os.environ, {"REKORDPOD_ASSET_DIR": str(self.assets)}
        )
        self.environment.start()

    def tearDown(self) -> None:
        self.environment.stop()
        self.temporary.cleanup()

    def test_validates_only_explicit_volume(self) -> None:
        selected = core.validate_selected_volume(str(self.volume))
        self.assertEqual(selected.root, self.volume)
        self.assertEqual(selected.target, "ipod6g")
        self.assertEqual(selected.installed_version, "daily-test")
        self.assertEqual(selected.release_zip, self.assets / core.TARGET_ASSETS["ipod6g"])

    def test_rejects_a_path_without_rekordbox_database(self) -> None:
        unrelated = self.root / "not-an-ipod"
        unrelated.mkdir()
        with self.assertRaisesRegex(core.InstallerError, "export.pdb"):
            core.validate_selected_volume(str(unrelated))

    def test_rejects_an_unpublished_target(self) -> None:
        (self.volume / ".rockbox" / "rockbox-info.txt").write_text(
            "Target: unsupported-test-target\nVersion: daily-test\n",
            encoding="utf-8",
        )
        with self.assertRaisesRegex(core.InstallerError, "does not support"):
            core.validate_selected_volume(str(self.volume))

    def test_reports_macos_removable_volume_permission_denial(self) -> None:
        protected = mock.Mock()
        protected.stat.side_effect = PermissionError("blocked")
        with mock.patch.object(core.sys, "platform", "darwin"):
            with self.assertRaisesRegex(
                core.InstallerError, "allow Removable Volumes"
            ):
                core._require_selected_path(
                    protected,
                    directory=True,
                    missing_message="missing",
                )

    def test_fingerprint_uses_dat_and_ext_but_not_2ex(self) -> None:
        selected = core.validate_selected_volume(str(self.volume))
        first, count = core.library_fingerprint(selected)
        self.assertEqual(count, 2)
        analysis = self.volume / "PIONEER" / "USBANLZ" / "P000" / "00000000"
        (analysis / "ANLZ0000.2EX").write_bytes(b"changed but still ignored")
        second, second_count = core.library_fingerprint(selected)
        self.assertEqual(second_count, 2)
        self.assertEqual(first, second)

    def test_reuses_a_valid_fingerprinted_package(self) -> None:
        selected = core.validate_selected_volume(str(self.volume))
        fingerprint, _ = core.library_fingerprint(selected)
        cache = self.root / "cache"
        destination = cache / "ipod6g" / fingerprint
        destination.mkdir(parents=True)
        package = destination / "rekordpod-device.zip"
        write_zip(package, "ipod6g", device_cache=True)
        summary = {"tracks": 12, "playlists": 3, "waveform_tracks": 11}
        (destination / "summary.json").write_text(json.dumps(summary), encoding="utf-8")

        result = core.build_or_reuse_package(selected, cache_root=cache)
        self.assertTrue(result.reused_cache)
        self.assertEqual(result.summary, summary)

    def test_install_preserves_settings_and_creates_backup(self) -> None:
        selected = core.validate_selected_volume(str(self.volume))
        settings = self.volume / ".rockbox" / "rekordpod" / "rekordpod.cfg"
        settings.parent.mkdir(parents=True)
        settings.write_text("accent=green\n", encoding="utf-8")
        package = self.root / "device.zip"
        write_zip(package, "ipod6g", device_cache=True)
        with zipfile.ZipFile(package, "a") as archive:
            archive.writestr(".rockbox/rekordpod/rekordpod.cfg", "accent=default\n")

        result = core.install_package(
            selected,
            package,
            backup_root=self.root / "backups",
            eject=False,
        )

        self.assertEqual(settings.read_text(encoding="utf-8"), "accent=green\n")
        self.assertTrue((self.volume / ".rockbox" / "rocks" / "apps" / "rekordpod.rock").is_file())
        self.assertEqual(
            (self.volume / ".rockbox" / "rekordpod" / "device-name.txt").read_text(),
            "RIZZPOD\n",
        )
        self.assertEqual(
            (result.backup_directory / "export.pdb").read_bytes(), b"database"
        )
        self.assertEqual(
            (result.backup_directory / "persistent" / core.PERSISTENT_FILES[0]).read_text(),
            "accent=green\n",
        )

    def test_rejects_unsafe_archive_before_writing(self) -> None:
        selected = core.validate_selected_volume(str(self.volume))
        package = self.root / "malicious.zip"
        write_zip(package, "ipod6g", device_cache=True, malicious=True)
        plugin = self.volume / ".rockbox" / "rocks" / "apps" / "rekordpod.rock"

        with self.assertRaisesRegex(core.InstallerError, "unsafe path"):
            core.install_package(
                selected,
                package,
                backup_root=self.root / "backups",
                eject=False,
            )
        self.assertFalse(plugin.exists())


if __name__ == "__main__":
    unittest.main()
