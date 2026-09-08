#!/usr/bin/env python3
"""Build the indexed, per-track RBPrep cache used by the Rockbox plugin."""

from __future__ import annotations

import argparse
import math
import os
from pathlib import Path
import sqlite3
import struct
import sys
import tempfile
import zipfile
import zlib


WAVEFORM_HZ = 150
MAX_DEVICE_POINTS = 131_072
INDEX_HEADER_SIZE = 40
TRACK_RECORD_SIZE = 24
NODE_RECORD_SIZE = 20
ROOT_NODE = 0xFFFFFFFF

COLOR_LABELS = {
    "sample": 0,
    "opener": 1,
    "builder": 2,
    "pivoter": 3,
    "maintainer": 4,
    "peak": 5,
    "reset": 6,
    "tool": 7,
}

CUE_COLORS = {
    "#FF0000": 0,
    "#FF5E00": 1,
    "#FF8C00": 1,
    "#FFE800": 2,
    "#1AFF00": 3,
    "#00E0FF": 4,
    "#0000FF": 5,
    "#4D00FF": 6,
    "#FF00A1": 7,
}


class Progress:
    def __init__(self, total: int) -> None:
        self.total = max(1, total)
        self.last = -1

    def update(self, current: int, label: str) -> None:
        percent = min(100, current * 100 // self.total)
        if percent == self.last and current != self.total:
            return
        self.last = percent
        width = 32
        filled = percent * width // 100
        bar = "#" * filled + " " * (width - filled)
        print(f"\r[{bar}] {percent:3d}%  {label[:42]:42s}",
              end="", file=sys.stderr, flush=True)
        if current >= self.total:
            print(file=sys.stderr)


class StringTable:
    def __init__(self) -> None:
        self.data = bytearray(b"\0")
        self.offsets = {"": 0}

    def add(self, value: object) -> int:
        text = str(value or "").replace("\0", "")
        if text in self.offsets:
            return self.offsets[text]
        offset = len(self.data)
        self.data.extend(text.encode("utf-8", "replace") + b"\0")
        self.offsets[text] = offset
        return offset


def peak_resample(samples: bytes, source_points: int,
                  output_points: int) -> bytes:
    if source_points <= output_points:
        return samples
    output = bytearray(output_points * 4)
    for target in range(output_points):
        start = target * source_points // output_points
        end = max(start + 1, (target + 1) * source_points // output_points)
        peak = start
        peak_height = samples[start * 4]
        for source in range(start + 1, end):
            height = samples[source * 4]
            if height > peak_height:
                peak = source
                peak_height = height
        output[target * 4:target * 4 + 4] = samples[peak * 4:peak * 4 + 4]
    return bytes(output)


def build_index(connection: sqlite3.Connection) -> bytes:
    strings = StringTable()
    track_rows = connection.execute(
        "SELECT stable_key, rekordbox_track_id, location, title, artist, "
        "genre, bpm, rating, color FROM tracks "
        "ORDER BY LTRIM(title) COLLATE NOCASE, artist COLLATE NOCASE, "
        "CAST(rekordbox_track_id AS INTEGER)"
    ).fetchall()
    track_lookup: dict[str, int] = {}
    track_records = bytearray()
    for index, row in enumerate(track_rows):
        stable_key, track_id, path, title, artist, genre, bpm, rating, color = row
        track_lookup[stable_key] = index
        track_records.extend(struct.pack(
            "<IIIIIHBB",
            int(track_id), strings.add(str(path or "").strip()),
            strings.add(str(title or "").strip()),
            strings.add(str(artist or "").strip()),
            strings.add(str(genre or "").strip()),
            max(0, min(65535, round(float(bpm or 0) * 100))),
            max(0, min(5, int(rating or 0))),
            COLOR_LABELS.get(str(color or "").casefold(), 0),
        ))

    nodes: list[dict[str, int | str]] = []
    folder_lookup: dict[tuple[int, str], int] = {}
    memberships: list[int] = []
    playlists = connection.execute(
        "SELECT rekordbox_playlist_id, path FROM playlists ORDER BY path"
    ).fetchall()
    for playlist_id, path in playlists:
        parts = [part.strip() for part in str(path).split(" / ") if part.strip()]
        if not parts:
            continue
        parent = ROOT_NODE
        for part in parts[:-1]:
            key = (parent, part)
            node_index = folder_lookup.get(key)
            if node_index is None:
                node_index = len(nodes)
                folder_lookup[key] = node_index
                nodes.append({
                    "parent": parent, "name": part, "first": 0,
                    "count": 0, "kind": 0,
                })
            parent = node_index

        first_member = len(memberships)
        for (stable_key,) in connection.execute(
            "SELECT stable_key FROM playlist_tracks "
            "WHERE rekordbox_playlist_id=? ORDER BY ordinal", (playlist_id,)
        ):
            track_index = track_lookup.get(stable_key)
            if track_index is not None:
                memberships.append(track_index)
        nodes.append({
            "parent": parent, "name": parts[-1], "first": first_member,
            "count": len(memberships) - first_member, "kind": 1,
        })

    node_records = bytearray()
    for node in nodes:
        node_records.extend(struct.pack(
            "<IIIIB3x", int(node["parent"]), strings.add(node["name"]),
            int(node["first"]), int(node["count"]), int(node["kind"]),
        ))
    member_records = b"".join(struct.pack("<I", value) for value in memberships)

    track_offset = INDEX_HEADER_SIZE
    node_offset = track_offset + len(track_records)
    member_offset = node_offset + len(node_records)
    string_offset = member_offset + len(member_records)
    header = struct.pack(
        "<4sHHIIIIIIII", b"RBI1", 1, INDEX_HEADER_SIZE,
        len(track_rows), len(nodes), len(memberships), track_offset,
        node_offset, member_offset, string_offset, len(strings.data),
    )
    assert len(header) == INDEX_HEADER_SIZE
    assert len(track_records) == len(track_rows) * TRACK_RECORD_SIZE
    assert len(node_records) == len(nodes) * NODE_RECORD_SIZE
    return header + track_records + node_records + member_records + strings.data


def cue_color(value: object) -> int:
    return CUE_COLORS.get(str(value or "").upper(), 0)


def build_track_cache(connection: sqlite3.Connection, stable_key: str,
                      track_id: int, bpm: float, rating: int,
                      color: str) -> bytes | None:
    detail = connection.execute(
        "SELECT point_count, encoding, samples FROM detail_waveforms "
        "WHERE stable_key=?", (stable_key,)
    ).fetchone()
    if detail is None:
        return None
    original_points, encoding, packed = detail
    samples = zlib.decompress(packed) if str(encoding).startswith("zlib-") else packed
    if len(samples) != original_points * 4:
        raise ValueError(f"{stable_key}: corrupt detail waveform")
    stored_points = min(original_points, MAX_DEVICE_POINTS)
    samples = peak_resample(samples, original_points, stored_points)
    waveform_duration_ms = math.ceil(original_points * 1000 / WAVEFORM_HZ)

    cues = connection.execute(
        "SELECT hot_cue, time_seconds, color_rgb FROM cue_points "
        "WHERE stable_key=? AND hot_cue BETWEEN 1 AND 16 "
        "ORDER BY hot_cue, ordinal", (stable_key,)
    ).fetchall()
    beats = connection.execute(
        "SELECT beat, time_seconds, bpm FROM beat_grid_points "
        "WHERE stable_key=? ORDER BY ordinal", (stable_key,)
    ).fetchall()
    phase_ms = round(beats[0][1] * 1000) if beats else 0
    header = struct.pack(
        "<4sHHIHHIIHHIBBHI", b"RBW3", 40, 0, stored_points,
        len(cues), min(len(beats), 65535), waveform_duration_ms,
        waveform_duration_ms, max(0, min(65535, round(bpm * 100))),
        WAVEFORM_HZ, phase_ms, max(0, min(5, rating)),
        COLOR_LABELS.get(str(color or "").casefold(), 0), 0,
        original_points,
    )
    assert len(header) == 40
    cue_data = b"".join(
        struct.pack("<IBBH", round(seconds * 1000), cue_color(rgb), slot, 0)
        for slot, seconds, rgb in cues
    )
    beat_data = b"".join(
        struct.pack(
            "<IBBH", round(seconds * 1000), max(1, min(4, beat)), 0,
            max(1, min(65535, round(point_bpm * 100))),
        )
        for beat, seconds, point_bpm in beats[:65535]
    )
    return header + samples + cue_data + beat_data


def zip_info(name: str) -> zipfile.ZipInfo:
    info = zipfile.ZipInfo(name, (2026, 9, 7, 21, 0, 0))
    info.compress_type = zipfile.ZIP_DEFLATED
    info.external_attr = 0o100644 << 16
    return info


def build(args: argparse.Namespace) -> None:
    connection = sqlite3.connect(args.database)
    tracks = connection.execute(
        "SELECT stable_key, rekordbox_track_id, COALESCE(bpm, 0), "
        "COALESCE(rating, 0), COALESCE(color, '') FROM tracks "
        "ORDER BY CAST(rekordbox_track_id AS INTEGER)"
    ).fetchall()
    index = build_index(connection)

    output = Path(args.output).expanduser().resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    handle, temporary_name = tempfile.mkstemp(
        prefix=".rbprep-device-", suffix=".zip", dir=output.parent
    )
    os.close(handle)
    temporary = Path(temporary_name)
    progress = Progress(len(tracks))
    analyzed = 0
    try:
        with zipfile.ZipFile(args.rockbox_zip) as source, zipfile.ZipFile(
            temporary, "w", allowZip64=True
        ) as target:
            for item in source.infolist():
                if not item.filename.startswith(".rockbox/rbprep/"):
                    target.writestr(item, source.read(item.filename))
            target.writestr(zip_info(".rockbox/rbprep/library.rbi"), index)
            for current, (stable_key, track_id, bpm, rating, color) in enumerate(
                tracks, 1
            ):
                payload = build_track_cache(
                    connection, stable_key, int(track_id), float(bpm),
                    int(rating), str(color),
                )
                if payload is not None:
                    target.writestr(
                        zip_info(f".rockbox/rbprep/tracks/{int(track_id):06d}.rbw"),
                        payload,
                    )
                    analyzed += 1
                progress.update(current, f"{current}/{len(tracks)} {stable_key}")
        os.replace(temporary, output)
    finally:
        connection.close()
        if temporary.exists():
            temporary.unlink()
    print(
        f"Built {output} with {len(tracks)} tracks and "
        f"{analyzed} per-track analysis files."
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--database", required=True, type=Path)
    parser.add_argument("--rockbox-zip", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    return parser.parse_args()


if __name__ == "__main__":
    build(parse_args())
