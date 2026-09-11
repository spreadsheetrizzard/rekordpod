#!/usr/bin/env python3
"""Apply RBPrep's on-device edit journals to a rekordbox USB export.

The journal stores full track snapshots.  Only the newest snapshot for each
track is considered, and that desired state is diffed against export.pdb and
the track's ANLZ files.  This makes repeated create/delete/recreate actions
collapse into one net result instead of stacking operations.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from datetime import datetime
import json
import os
from pathlib import Path
import shutil
import struct
import sys
from typing import Iterable

try:
    from rekordbox_pdb.edit import PdbEditor
except ImportError as error:  # pragma: no cover - exercised by the launcher
    raise SystemExit(
        "rekordbox-pdb editing support is unavailable. Run this with the "
        "RBPrep Python environment and its rekordbox-pdb source on PYTHONPATH."
    ) from error


EDIT_RECORD_SIZE = 216
EDIT_MAGIC = b"RBE1"
EDIT_VERSION = 1
EDIT_JOURNAL = Path(".rockbox/rbprep/edits.rbe")
PLAYLIST_JOURNAL = Path(".rockbox/rbprep/playlist-adds.rba")
LOCAL_BURN_STATE = Path(".rockbox/rbprep/local-burn.rbs")
PDB_PATH = Path("PIONEER/rekordbox/export.pdb")
COLOR_RGB = (
    (255, 70, 70), (255, 145, 40), (250, 220, 45), (55, 235, 95),
    (50, 225, 225), (55, 135, 255), (175, 90, 255), (255, 80, 185),
)


@dataclass(frozen=True)
class EditSnapshot:
    track_id: int
    saved_tick: int
    rating: int
    color_index: int
    quantize: bool
    beat_shift: int
    year: int
    bpm_x100: int
    grid_phase_ms: int
    grid_offset_ms: int
    deck_cue_ms: int
    hotcues_ms: tuple[int, ...]
    hotcue_colors: tuple[int, ...]
    genre: str
    title: str
    raw: bytes


@dataclass(frozen=True)
class PlaylistAdd:
    track_id: int
    playlist_id: int
    playlist_name: str


class Progress:
    def __init__(self, total: int) -> None:
        self.total = max(1, total)
        self.done = 0

    def step(self, label: str) -> None:
        self.done += 1
        percent = min(100, self.done * 100 // self.total)
        width = 32
        filled = percent * width // 100
        bar = "#" * filled + " " * (width - filled)
        print(f"\r[{bar}] {percent:3d}%  {label[:48]:48s}",
              end="", flush=True)
        if self.done >= self.total:
            print()


def _cstring(data: bytes) -> str:
    return data.split(b"\0", 1)[0].decode("utf-8", "replace")


def parse_edit_journal(path: Path, start: int = 0) -> tuple[dict[int, EditSnapshot], int]:
    if not path.exists():
        return {}, 0
    data = path.read_bytes()
    if start < 0 or start > len(data) or start % EDIT_RECORD_SIZE:
        start = 0
    data = data[start:]
    latest: dict[int, EditSnapshot] = {}
    valid = 0
    for offset in range(0, len(data) - EDIT_RECORD_SIZE + 1,
                        EDIT_RECORD_SIZE):
        raw = data[offset:offset + EDIT_RECORD_SIZE]
        if (raw[:4] != EDIT_MAGIC or
                struct.unpack_from("<H", raw, 4)[0] != EDIT_RECORD_SIZE or
                struct.unpack_from("<H", raw, 6)[0] != EDIT_VERSION):
            continue
        valid += 1
        track_id, saved_tick = struct.unpack_from("<II", raw, 8)
        rating, color_index, quantize, beat_shift = struct.unpack_from(
            "<BBBb", raw, 16)
        year = struct.unpack_from("<H", raw, 20)[0]
        bpm_x100 = struct.unpack_from("<I", raw, 24)[0]
        grid_phase_ms, grid_offset_ms, deck_cue_ms = struct.unpack_from(
            "<iii", raw, 28)
        snapshot = EditSnapshot(
            track_id=track_id, saved_tick=saved_tick, rating=rating,
            color_index=color_index, quantize=bool(quantize),
            beat_shift=beat_shift, year=year, bpm_x100=bpm_x100,
            grid_phase_ms=grid_phase_ms, grid_offset_ms=grid_offset_ms,
            deck_cue_ms=deck_cue_ms,
            hotcues_ms=struct.unpack_from("<16i", raw, 40),
            hotcue_colors=tuple(raw[104:120]),
            genre=_cstring(raw[120:152]), title=_cstring(raw[152:216]),
            raw=raw,
        )
        latest[track_id] = snapshot
    return latest, valid


def parse_playlist_journal(path: Path, start: int = 0) -> tuple[list[PlaylistAdd], int]:
    if not path.exists():
        return [], 0
    data = path.read_bytes()
    if start < 0 or start > len(data):
        start = 0
    latest: dict[tuple[int, int], PlaylistAdd] = {}
    valid = 0
    for line in data[start:].decode("utf-8", "replace").splitlines():
        parts = line.split("\t", 2)
        if len(parts) != 3:
            continue
        try:
            track_id, playlist_id = int(parts[0]), int(parts[1])
        except ValueError:
            continue
        valid += 1
        value = PlaylistAdd(track_id, playlist_id, parts[2].strip())
        latest[(track_id, playlist_id)] = value
    return list(latest.values()), valid


def iter_anlz_tags(data: bytes) -> Iterable[tuple[int, bytes, int, int]]:
    if len(data) < 28 or data[:4] != b"PMAI":
        raise ValueError("not a rekordbox ANLZ file")
    header_length, file_length = struct.unpack_from(">II", data, 4)
    if file_length != len(data) or not 28 <= header_length <= len(data):
        raise ValueError("invalid ANLZ file length")
    offset = header_length
    while offset < len(data):
        if offset + 12 > len(data):
            raise ValueError("truncated ANLZ tag")
        kind = data[offset:offset + 4]
        len_header, len_tag = struct.unpack_from(">II", data, offset + 4)
        if len_header < 12 or len_tag < len_header or offset + len_tag > len(data):
            raise ValueError(f"invalid {kind!r} ANLZ tag length")
        yield offset, kind, len_header, len_tag
        offset += len_tag
    if offset != len(data):
        raise ValueError("ANLZ tag layout does not cover file")


def parse_pcob_hotcues(tag: bytes) -> dict[int, int]:
    if len(tag) < 24 or struct.unpack_from(">I", tag, 12)[0] != 1:
        return {}
    count = struct.unpack_from(">H", tag, 18)[0]
    result: dict[int, int] = {}
    offset = 24
    for _ in range(count):
        if offset + 56 > len(tag):
            raise ValueError("truncated PCOB cue entry")
        length = struct.unpack_from(">I", tag, offset + 8)[0]
        if length < 56 or offset + length > len(tag):
            raise ValueError("invalid PCOB cue entry")
        slot = struct.unpack_from(">I", tag, offset + 12)[0]
        time_ms = struct.unpack_from(">I", tag, offset + 32)[0]
        if 1 <= slot <= 16:
            result[slot] = time_ms
        offset += length
    return result


def parse_pco2_hotcues(tag: bytes) -> dict[int, tuple[int, tuple[int, int, int]]]:
    if len(tag) < 20 or struct.unpack_from(">I", tag, 12)[0] != 1:
        return {}
    count = struct.unpack_from(">H", tag, 16)[0]
    result: dict[int, tuple[int, tuple[int, int, int]]] = {}
    offset = 20
    for _ in range(count):
        if offset + 48 > len(tag):
            raise ValueError("truncated PCO2 cue entry")
        length = struct.unpack_from(">I", tag, offset + 8)[0]
        comment_length = struct.unpack_from(">I", tag, offset + 40)[0]
        if length < 48 or offset + length > len(tag) or 48 + comment_length > length:
            raise ValueError("invalid PCO2 cue entry")
        slot = struct.unpack_from(">I", tag, offset + 12)[0]
        time_ms = struct.unpack_from(">I", tag, offset + 20)[0]
        color_at = offset + 44 + comment_length
        rgb = tuple(tag[color_at + 1:color_at + 4])
        if 1 <= slot <= 16:
            result[slot] = (time_ms, rgb)  # type: ignore[arg-type]
        offset += length
    return result


def build_pcob_hotcues(points: list[tuple[int, int, int]]) -> bytes:
    entries = bytearray()
    for index, (slot, time_ms, _color) in enumerate(points):
        previous = 0xFFFF if index == 0 else index - 1
        following = 0xFFFF if index + 1 == len(points) else index + 1
        entries.extend(struct.pack(
            ">4sIIIIIHHBxHII16x", b"PCPT", 28, 56, slot, 4, 0x10000,
            previous, following, 1, 1000, time_ms, 0xFFFFFFFF,
        ))
    length = 24 + len(entries)
    return (struct.pack(">4sIIIHHi", b"PCOB", 24, length, 1, 0,
                        len(points), -1) + entries)


def build_pco2_hotcues(points: list[tuple[int, int, int]]) -> bytes:
    entries = bytearray()
    for slot, time_ms, color in points:
        red, green, blue = COLOR_RGB[color & 7]
        entries.extend(struct.pack(
            ">4sIIIB3xIIB7xHHIBBBB", b"PCP2", 16, 48, slot, 1,
            time_ms, 0xFFFFFFFF, (color & 7) + 1, 0, 0, 0,
            (color & 7) + 1, red, green, blue,
        ))
    length = 20 + len(entries)
    return struct.pack(">4sIIIHH", b"PCO2", 20, length, 1,
                       len(points), 0) + entries


def _div_trunc(numerator: int, denominator: int) -> int:
    sign = -1 if numerator < 0 else 1
    return sign * (abs(numerator) // denominator)


def rewrite_pqtz(tag: bytes, snapshot: EditSnapshot,
                 source_bpm_x100: int) -> bytes:
    if len(tag) < 24:
        raise ValueError("truncated PQTZ tag")
    count = struct.unpack_from(">I", tag, 20)[0]
    if len(tag) != 24 + count * 8 or snapshot.bpm_x100 <= 0:
        raise ValueError("invalid PQTZ beat grid")
    if source_bpm_x100 <= 0 and count:
        source_bpm_x100 = struct.unpack_from(">H", tag, 26)[0]
    source_bpm_x100 = max(1, source_bpm_x100)
    entries = bytearray()
    for index in range(count):
        beat, _tempo, time_ms = struct.unpack_from(">HHI", tag, 24 + index * 8)
        delta = time_ms - snapshot.grid_phase_ms
        target_time = (snapshot.grid_phase_ms + snapshot.grid_offset_ms +
                       _div_trunc(delta * source_bpm_x100,
                                  snapshot.bpm_x100))
        target_beat = ((beat - 1 + snapshot.beat_shift) & 3) + 1
        entries.extend(struct.pack(">HHI", target_beat,
                                   min(65535, snapshot.bpm_x100),
                                   max(0, target_time)))
    return (struct.pack(">4sII4xII", b"PQTZ", 24, 24 + len(entries),
                        0x80000, count) + entries)


def rewrite_analysis(data: bytes, snapshot: EditSnapshot,
                     source_bpm_x100: int) -> bytes:
    desired = [
        (slot + 1, time_ms, snapshot.hotcue_colors[slot] & 7)
        for slot, time_ms in enumerate(snapshot.hotcues_ms) if time_ms >= 0
    ]
    desired_times = {slot: time_ms for slot, time_ms, _ in desired}
    desired_extended = {
        slot: (time_ms, COLOR_RGB[color]) for slot, time_ms, color in desired
    }
    header_length = struct.unpack_from(">I", data, 4)[0]
    output = bytearray(data[:header_length])
    for offset, kind, _len_header, len_tag in iter_anlz_tags(data):
        original = data[offset:offset + len_tag]
        replacement = original
        if kind == b"PQTZ":
            candidate = rewrite_pqtz(original, snapshot, source_bpm_x100)
            if candidate != original:
                replacement = candidate
        elif kind == b"PCOB" and len(original) >= 24:
            if struct.unpack_from(">I", original, 12)[0] == 1:
                if parse_pcob_hotcues(original) != desired_times:
                    replacement = build_pcob_hotcues(desired)
        elif kind == b"PCO2" and len(original) >= 20:
            if struct.unpack_from(">I", original, 12)[0] == 1:
                if parse_pco2_hotcues(original) != desired_extended:
                    replacement = build_pco2_hotcues(desired)
        output.extend(replacement)
    struct.pack_into(">I", output, 8, len(output))
    result = bytes(output)
    list(iter_anlz_tags(result))
    return result


def resolve_analysis_files(volume: Path, analyze_path: str) -> list[Path]:
    relative = analyze_path.lstrip("/")
    dat = volume / relative
    stem = dat.with_suffix("")
    return [path for path in (dat, stem.with_suffix(".EXT")) if path.exists()]


def current_color_index(color_id: int) -> int:
    return color_id - 1 if 1 <= color_id <= 8 else 8


def source_bpm_from_cache(volume: Path, track_id: int, fallback: int) -> int:
    path = volume / f".rockbox/rbprep/tracks/{track_id:06d}.rbw"
    try:
        header = path.read_bytes()[:28]
    except OSError:
        return fallback
    if len(header) == 28 and header[:4] == b"RBW3":
        return max(1, struct.unpack_from("<H", header, 24)[0])
    return fallback


def plan_changes(volume: Path, snapshots: dict[int, EditSnapshot],
                 playlist_adds: list[PlaylistAdd]) -> tuple[
                     bytes, dict[Path, bytes], list[str], list[str]]:
    pdb_path = volume / PDB_PATH
    editor = PdbEditor.from_file(pdb_path)
    tracks = {track.id: track for track in editor.db.tracks}
    genre_names = {genre.id: genre.name for genre in editor.db.genres}
    changes: list[str] = []
    warnings: list[str] = []
    analysis_outputs: dict[Path, bytes] = {}

    for track_id, snapshot in snapshots.items():
        track = tracks.get(track_id)
        if track is None:
            warnings.append(f"track {track_id}: not present in export.pdb")
            continue
        fields = (
            ("rating", track.rating, min(5, snapshot.rating)),
            ("year", track.year, snapshot.year),
            ("tempo", track.tempo, snapshot.bpm_x100),
        )
        for field, current, desired in fields:
            if desired >= 0 and current != desired:
                editor.set_track_field(track_id, field, desired)
                changes.append(f"track {track_id}: {field} {current} -> {desired}")
        desired_color = (snapshot.color_index + 1
                         if snapshot.color_index < 8 else 0)
        if current_color_index(track.color_id) != snapshot.color_index:
            editor.set_track_field(track_id, "color_id", desired_color)
            changes.append(
                f"track {track_id}: color {track.color_id} -> {desired_color}")
        current_genre = genre_names.get(track.genre_id, "")
        if snapshot.genre and current_genre.casefold() != snapshot.genre.casefold():
            genre_id = editor.get_or_create_genre(snapshot.genre)
            editor.set_track_field(track_id, "genre_id", genre_id)
            genre_names[genre_id] = snapshot.genre
            changes.append(
                f"track {track_id}: genre {current_genre!r} -> {snapshot.genre!r}")

        analysis_files = resolve_analysis_files(volume, track.analyze_path)
        if not analysis_files:
            warnings.append(
                f"track {track_id}: analysis files missing ({track.analyze_path})")
            continue
        for path in analysis_files:
            original = path.read_bytes()
            baseline_path = path.with_name(path.name + ".rbprep-bak")
            baseline = baseline_path.read_bytes() if baseline_path.exists() else original
            source_bpm = source_bpm_from_cache(volume, track_id, track.tempo)
            rewritten = rewrite_analysis(baseline, snapshot, source_bpm)
            if rewritten != original:
                analysis_outputs[path] = rewritten
                changes.append(f"track {track_id}: analysis {path.suffix[1:]}")

    playlist_nodes = {node.id: node for node in editor.db.playlist_tree}
    names: dict[str, list[int]] = {}
    for node in playlist_nodes.values():
        if not node.is_folder:
            names.setdefault(node.name.casefold(), []).append(node.id)
    present = {(entry.track_id, entry.playlist_id)
               for entry in editor.db.playlist_entries}
    for addition in playlist_adds:
        playlist_id = addition.playlist_id
        node = playlist_nodes.get(playlist_id)
        if node is None or node.is_folder:
            matches = names.get(addition.playlist_name.casefold(), [])
            if len(matches) == 1:
                playlist_id = matches[0]
            else:
                warnings.append(
                    f"track {addition.track_id}: playlist "
                    f"{addition.playlist_name!r} could not be resolved")
                continue
        key = (addition.track_id, playlist_id)
        if key in present:
            continue
        if addition.track_id not in tracks:
            warnings.append(
                f"track {addition.track_id}: playlist add track is missing")
            continue
        editor.add_to_playlist(playlist_id, addition.track_id)
        present.add(key)
        changes.append(
            f"track {addition.track_id}: add to playlist {playlist_id}")

    pdb_bytes = editor.to_bytes()
    validation = PdbEditor(pdb_bytes)
    if len(validation.db.tracks) != len(tracks):
        raise ValueError("export.pdb validation changed the track count")
    for data in analysis_outputs.values():
        list(iter_anlz_tags(data))
    return pdb_bytes, analysis_outputs, changes, warnings


def backup_files(volume: Path, backup_root: Path, paths: Iterable[Path]) -> Path:
    destination = backup_root / datetime.now().strftime("%Y%m%d-%H%M%S")
    for source in paths:
        relative = source.relative_to(volume)
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
    return destination


def atomic_replace(path: Path, data: bytes) -> None:
    temporary = path.with_name(f".{path.name}.rbprep-new-{os.getpid()}")
    try:
        with temporary.open("wb") as handle:
            handle.write(data)
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary, path)
    finally:
        if temporary.exists():
            temporary.unlink()


def read_local_burn_offsets(path: Path) -> tuple[int, int]:
    if not path.exists():
        return 0, 0
    data = path.read_bytes()
    if len(data) != 12 or data[:4] != b"RBL1":
        return 0, 0
    return struct.unpack_from("<II", data, 4)


def local_burn_state(edit_size: int, playlist_size: int) -> bytes:
    return struct.pack("<4sII", b"RBL1", edit_size, playlist_size)


def compact_edit_journal(snapshots: dict[int, EditSnapshot]) -> bytes:
    return b"".join(snapshot.raw for snapshot in snapshots.values())


def compact_playlist_journal(additions: list[PlaylistAdd]) -> bytes:
    return "".join(
        f"{item.track_id}\t{item.playlist_id}\t{item.playlist_name}\n"
        for item in additions
    ).encode("utf-8")


def run(args: argparse.Namespace) -> int:
    volume = args.volume.expanduser().resolve()
    pdb_path = volume / PDB_PATH
    edit_path = volume / EDIT_JOURNAL
    playlist_path = volume / PLAYLIST_JOURNAL
    burn_state_path = volume / LOCAL_BURN_STATE
    if not volume.is_dir() or not pdb_path.is_file():
        raise SystemExit(f"No rekordbox export found at {pdb_path}")

    edit_start, playlist_start = read_local_burn_offsets(burn_state_path)
    snapshots, edit_records = parse_edit_journal(edit_path, edit_start)
    playlist_adds, playlist_records = parse_playlist_journal(
        playlist_path, playlist_start)
    print(
        f"Journal: {edit_records} edit snapshots -> {len(snapshots)} tracks; "
        f"{playlist_records} playlist requests -> {len(playlist_adds)} unique."
    )
    pdb_bytes, analysis, changes, warnings = plan_changes(
        volume, snapshots, playlist_adds)
    plan = {
        "apply": bool(args.apply), "snapshot_records": edit_records,
        "unique_tracks": len(snapshots), "playlist_records": playlist_records,
        "unique_playlist_adds": len(playlist_adds),
        "local_burn_offsets": [edit_start, playlist_start],
        "net_changes": changes, "warnings": warnings,
        "analysis_files": [str(path.relative_to(volume)) for path in analysis],
    }
    if args.plan_json:
        args.plan_json.parent.mkdir(parents=True, exist_ok=True)
        args.plan_json.write_text(json.dumps(plan, indent=2) + "\n", "utf-8")
    for warning in warnings:
        print(f"WARNING: {warning}", file=sys.stderr)
    if not args.apply:
        print(f"Dry run: {len(changes)} net change(s); nothing was written.")
        return 0
    edit_size = edit_path.stat().st_size if edit_path.exists() else 0
    playlist_size = playlist_path.stat().st_size if playlist_path.exists() else 0
    if not changes:
        if warnings:
            print("No writable changes were applied; unresolved records remain "
                  "pending.", file=sys.stderr)
            return 1
        atomic_replace(burn_state_path,
                       local_burn_state(edit_size, playlist_size))
        print("The Rekordbox export already matches the newest RBPrep state.")
        return 0

    replacements: dict[Path, bytes] = {pdb_path: pdb_bytes, **analysis}
    all_snapshots, _ = parse_edit_journal(edit_path)
    all_playlist_adds, _ = parse_playlist_journal(playlist_path)
    replacements[edit_path] = compact_edit_journal(all_snapshots)
    replacements[playlist_path] = compact_playlist_journal(all_playlist_adds)
    replacements[burn_state_path] = local_burn_state(
        len(replacements[edit_path]), len(replacements[playlist_path]))
    original = {path: path.read_bytes() if path.exists() else None
                for path in replacements}
    backup_sources = [path for path, data in original.items() if data is not None]
    destination = backup_files(volume, args.backup_root.expanduser().resolve(),
                               backup_sources)
    progress = Progress(len(replacements))
    committed: list[Path] = []
    try:
        for path, data in replacements.items():
            path.parent.mkdir(parents=True, exist_ok=True)
            atomic_replace(path, data)
            committed.append(path)
            progress.step(str(path.relative_to(volume)))
        PdbEditor.from_file(pdb_path).db.tracks
        for path in analysis:
            list(iter_anlz_tags(path.read_bytes()))
    except Exception:
        for path in reversed(committed):
            prior = original[path]
            if prior is None:
                path.unlink(missing_ok=True)
            else:
                atomic_replace(path, prior)
        raise
    print(f"Applied {len(changes)} net change(s). Backup: {destination}")
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--volume", type=Path, default=Path("/Volumes/RIZZPOD"))
    parser.add_argument("--backup-root", type=Path, required=True)
    parser.add_argument("--plan-json", type=Path)
    parser.add_argument("--apply", action="store_true")
    return parser.parse_args()


if __name__ == "__main__":
    raise SystemExit(run(parse_args()))
