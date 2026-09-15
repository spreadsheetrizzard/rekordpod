#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later

"""Regression tests for Rekordpod's append-only edit and cue formats."""

import importlib.util
import sqlite3
import struct
import sys
import tempfile
import types
import unittest
from pathlib import Path
from types import SimpleNamespace


# The format helpers do not need rekordbox-pdb.  Supply a minimal import stub
# so these release-gate tests remain runnable on a clean Rockbox checkout.
rekordbox_pdb = types.ModuleType("rekordbox_pdb")
rekordbox_pdb_edit = types.ModuleType("rekordbox_pdb.edit")
rekordbox_pdb_edit.PdbEditor = object
sys.modules.setdefault("rekordbox_pdb", rekordbox_pdb)
sys.modules.setdefault("rekordbox_pdb.edit", rekordbox_pdb_edit)
MODULE_PATH = Path(__file__).with_name("apply_device_edits.py")
SPEC = importlib.util.spec_from_file_location("rbprep_apply_device_edits",
                                              MODULE_PATH)
assert SPEC and SPEC.loader
formats = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = formats
SPEC.loader.exec_module(formats)

CACHE_MODULE_PATH = Path(__file__).with_name("build_device_cache.py")
CACHE_SPEC = importlib.util.spec_from_file_location(
    "rekordpod_build_device_cache", CACHE_MODULE_PATH)
assert CACHE_SPEC and CACHE_SPEC.loader
cache_formats = importlib.util.module_from_spec(CACHE_SPEC)
sys.modules[CACHE_SPEC.name] = cache_formats
CACHE_SPEC.loader.exec_module(cache_formats)


def snapshot(version=2, hotcues=None):
    cues = tuple(hotcues if hotcues is not None else [-1] * 16)
    raw = bytearray(formats.EDIT_RECORD_SIZE)
    raw[:4] = formats.EDIT_MAGIC
    struct.pack_into("<HHII", raw, 4, formats.EDIT_RECORD_SIZE, version,
                     1234, 77)
    struct.pack_into("<BBBbHIiii", raw, 16, 4, 8, 1, 0, 2026,
                     12800, 0, 0, -1)
    struct.pack_into("<16i", raw, 40, *cues)
    raw[104:120] = bytes(range(8)) * 2
    raw[120:126] = b"House\0"
    raw[152:157] = b"Test\0"
    raw[192:196] = b"8A\0\0"
    return formats.EditSnapshot(
        track_id=1234, saved_tick=77, rating=4, color_index=8,
        quantize=True, beat_shift=0, year=2026, bpm_x100=12800,
        grid_phase_ms=0, grid_offset_ms=0, deck_cue_ms=-1,
        hotcues_ms=cues, hotcue_colors=tuple(raw[104:120]),
        genre="House", title="Test", raw=bytes(raw),
    )


def analysis_with_one_cue():
    points = [(1, 12345, 3)]
    tags = (formats.build_pcob_hotcues(points) +
            formats.build_pco2_hotcues(points))
    header = bytearray(28)
    header[:4] = b"PMAI"
    struct.pack_into(">II", header, 4, 28, 28 + len(tags))
    return bytes(header) + tags


class EditFormatTests(unittest.TestCase):
    def test_version_two_snapshot_is_readable(self):
        value = snapshot()
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "edits.rbe"
            path.write_bytes(value.raw)
            parsed, count = formats.parse_edit_journal(path)
        self.assertEqual(count, 1)
        self.assertEqual(parsed[1234].title, "Test")
        self.assertEqual(parsed[1234].hotcues_ms, (-1,) * 16)

    def test_deleting_only_cue_produces_valid_empty_banks(self):
        rewritten = formats.rewrite_analysis(analysis_with_one_cue(),
                                             snapshot(), 12800)
        tags = {kind: rewritten[offset:offset + length]
                for offset, kind, _header, length
                in formats.iter_anlz_tags(rewritten)}
        self.assertEqual(formats.parse_pcob_hotcues(tags[b"PCOB"]), {})
        self.assertEqual(formats.parse_pco2_hotcues(tags[b"PCO2"]), {})
        self.assertEqual(struct.unpack_from(">H", tags[b"PCOB"], 18)[0], 0)
        self.assertEqual(struct.unpack_from(">H", tags[b"PCO2"], 16)[0], 0)

    def test_cue_palette_round_trips_rekordbox_green(self):
        tag = formats.build_pco2_hotcues([(1, 12345, 3)])
        self.assertEqual(formats.parse_pco2_hotcues(tag),
                         {1: (12345, (26, 255, 0))})
        self.assertEqual(cache_formats.cue_color("#1AFF00"), 3)

    def test_pre_beta_and_uncoloured_cues_import_as_green(self):
        self.assertEqual(cache_formats.cue_color("#37EB5F"), 3)
        self.assertEqual(cache_formats.cue_color(""), 3)
        self.assertEqual(cache_formats.cue_color("#000000"), 3)

    def test_device_cache_records_source_fingerprint(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "export.pdb"
            source.write_bytes(b"DeviceSQL test bytes\0")
            connection = sqlite3.connect(":memory:")
            try:
                connection.execute(
                    "CREATE TABLE metadata (key TEXT, value TEXT)"
                )
                connection.execute(
                    "INSERT INTO metadata VALUES ('source', ?)",
                    (str(source),),
                )
                state = cache_formats.build_source_state(connection)
                expected_fingerprint = cache_formats.fnv1a_file(source)[1]
            finally:
                connection.close()
        self.assertIsNotNone(state)
        magic, version, size, source_size, fingerprint = struct.unpack(
            "<4sHHII", state
        )
        self.assertEqual((magic, version, size), (b"RLS1", 1, 16))
        self.assertEqual(source_size, len(b"DeviceSQL test bytes\0"))
        self.assertEqual(fingerprint, expected_fingerprint)

    def test_recovery_writer_leaves_descriptive_metadata_read_only(self):
        class FakeEditor:
            source = None

            def __init__(self, _data=None):
                self.calls = []
                self.db = SimpleNamespace(
                    tracks=[SimpleNamespace(
                        id=1234, rating=0, year=1999, tempo=12000,
                        color_id=2, genre_id=7,
                        analyze_path="/PIONEER/USBANLZ/missing.DAT")],
                    playlist_tree=[], playlist_entries=[])

            @classmethod
            def from_file(cls, _path):
                cls.source = cls()
                return cls.source

            def set_track_field(self, track_id, field, value):
                self.calls.append((track_id, field, value))

            def to_bytes(self):
                return b"validated-pdb"

        original = formats.PdbEditor
        formats.PdbEditor = FakeEditor
        try:
            with tempfile.TemporaryDirectory() as directory:
                _pdb, _analysis, changes, _warnings = formats.plan_changes(
                    Path(directory), {1234: snapshot()}, [])
            calls = FakeEditor.source.calls
        finally:
            formats.PdbEditor = original
        self.assertEqual(calls, [
            (1234, "rating", 4),
            (1234, "tempo", 12800),
        ])
        self.assertIn("track 1234: rating 0 -> 4", changes)
        self.assertIn("track 1234: tempo 12000 -> 12800", changes)
        self.assertFalse(any("year" in change or "color" in change or
                             "genre" in change or "key" in change
                             for change in changes))


if __name__ == "__main__":
    unittest.main(verbosity=2)
