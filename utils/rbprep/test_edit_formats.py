#!/usr/bin/env python3
"""Regression tests for Rekordpod's append-only edit and cue formats."""

import importlib.util
import struct
import sys
import tempfile
import types
import unittest
from pathlib import Path


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


if __name__ == "__main__":
    unittest.main(verbosity=2)
