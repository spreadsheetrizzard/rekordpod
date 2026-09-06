#!/usr/bin/env python3
"""Convert a rekordbox Device Library export into RBPrep/Rockbox inputs.

The metadata model deliberately mirrors Drag'n'Dunk's ImportedTrackRow and
playlist ordering.  Audio paths in export.pdb are already rooted at the
export volume (normally /Contents/...) and are written unchanged to M3U8.

Requires the MIT-licensed ``rekordbox-pdb`` Python package.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import sqlite3
import struct
import sys
import time
import zlib
from collections import defaultdict
from pathlib import Path

try:
    from rekordbox_pdb import Database
except ImportError as exc:
    raise SystemExit(
        "rekordbox-pdb is required: python3 -m pip install rekordbox-pdb"
    ) from exc

try:
    from pyrekordbox.anlz import AnlzFile
except ImportError:
    AnlzFile = None


SCHEMA_VERSION = 1
RS = "\x1e"
BAD_COMPONENT = re.compile(r"[/:\\\x00-\x1f]")


class Progress:
    """Small dependency-free terminal progress bar for long RBPrep jobs."""

    def __init__(self, phase: str, total: int):
        self.phase = phase
        self.total = max(0, total)
        self.started = time.monotonic()
        self.last_drawn = -1
        self.update(0, force=True)

    def update(self, completed: int, force: bool = False) -> None:
        completed = min(max(0, completed), self.total)
        percent = 100 if self.total == 0 else int(completed * 100 / self.total)
        if not force and completed != self.total and percent == self.last_drawn:
            return
        self.last_drawn = percent
        filled = int(28 * percent / 100)
        bar = "█" * filled + "░" * (28 - filled)
        elapsed = int(time.monotonic() - self.started)
        line = (
            f"\r{self.phase:<12} [{bar}] {percent:3d}%  "
            f"{completed:,}/{self.total:,}  {elapsed // 60:02d}:{elapsed % 60:02d}"
        )
        print(line, end="\n" if completed == self.total else "", flush=True)


def digest(values: list[str]) -> str:
    """Match Drag'n'Dunk's SHA-256 metadata hashing convention."""
    return hashlib.sha256(RS.join(values).encode("utf-8")).hexdigest()


def safe_component(value: str) -> str:
    value = BAD_COMPONENT.sub("_", value).strip().rstrip(".")
    return value or "UNTITLED"


def playlist_paths(nodes) -> dict[int, tuple[str, ...]]:
    by_id = {node.id: node for node in nodes}
    memo: dict[int, tuple[str, ...]] = {}

    def resolve(node_id: int, seen: frozenset[int] = frozenset()) -> tuple[str, ...]:
        if node_id in memo:
            return memo[node_id]
        if node_id in seen:
            raise ValueError(f"playlist tree cycle at node {node_id}")
        node = by_id[node_id]
        parent = () if not node.parent_id else resolve(node.parent_id, seen | {node_id})
        memo[node_id] = parent + (node.name,)
        return memo[node_id]

    for node in nodes:
        resolve(node.id)
    return memo


def create_schema(connection: sqlite3.Connection) -> None:
    connection.executescript(
        """
        PRAGMA foreign_keys = ON;
        CREATE TABLE metadata (key TEXT PRIMARY KEY, value TEXT NOT NULL);
        CREATE TABLE tracks (
            stable_key TEXT PRIMARY KEY,
            rekordbox_track_id TEXT NOT NULL UNIQUE,
            location TEXT,
            title TEXT NOT NULL,
            artist TEXT NOT NULL DEFAULT '',
            album TEXT NOT NULL DEFAULT '',
            genre TEXT NOT NULL DEFAULT 'UNCLASSIFIED',
            bpm REAL,
            musical_key TEXT NOT NULL DEFAULT '',
            year INTEGER,
            rating INTEGER,
            play_count INTEGER,
            comments TEXT NOT NULL DEFAULT '',
            date_added TEXT NOT NULL DEFAULT '',
            color TEXT NOT NULL DEFAULT '',
            cue_count INTEGER NOT NULL DEFAULT 0,
            beat_grid_count INTEGER NOT NULL DEFAULT 0,
            metadata_hash TEXT NOT NULL
        );
        CREATE INDEX tracks_location ON tracks(location);
        CREATE INDEX tracks_artist ON tracks(artist);
        CREATE INDEX tracks_genre ON tracks(genre);
        CREATE TABLE playlists (
            rekordbox_playlist_id INTEGER PRIMARY KEY,
            path TEXT NOT NULL UNIQUE,
            name TEXT NOT NULL,
            membership_hash TEXT NOT NULL
        );
        CREATE TABLE playlist_tracks (
            rekordbox_playlist_id INTEGER NOT NULL,
            ordinal INTEGER NOT NULL,
            stable_key TEXT NOT NULL,
            PRIMARY KEY(rekordbox_playlist_id, ordinal),
            FOREIGN KEY(rekordbox_playlist_id) REFERENCES playlists(rekordbox_playlist_id),
            FOREIGN KEY(stable_key) REFERENCES tracks(stable_key)
        );
        CREATE INDEX playlist_tracks_track ON playlist_tracks(stable_key);
        CREATE TABLE beat_grid_points (
            stable_key TEXT NOT NULL,
            ordinal INTEGER NOT NULL,
            beat INTEGER NOT NULL,
            bpm REAL NOT NULL,
            time_seconds REAL NOT NULL,
            PRIMARY KEY(stable_key, ordinal),
            FOREIGN KEY(stable_key) REFERENCES tracks(stable_key)
        );
        CREATE TABLE cue_points (
            stable_key TEXT NOT NULL,
            ordinal INTEGER NOT NULL,
            cue_kind TEXT NOT NULL,
            hot_cue INTEGER NOT NULL,
            time_seconds REAL NOT NULL,
            loop_time_seconds REAL,
            comment TEXT NOT NULL DEFAULT '',
            color_rgb TEXT NOT NULL DEFAULT '',
            PRIMARY KEY(stable_key, ordinal),
            FOREIGN KEY(stable_key) REFERENCES tracks(stable_key)
        );
        CREATE TABLE waveforms (
            stable_key TEXT PRIMARY KEY,
            source_format TEXT NOT NULL,
            point_count INTEGER NOT NULL,
            channels TEXT NOT NULL,
            encoding TEXT NOT NULL,
            samples BLOB NOT NULL,
            FOREIGN KEY(stable_key) REFERENCES tracks(stable_key)
        );
        CREATE TABLE detail_waveforms (
            stable_key TEXT PRIMARY KEY,
            source_format TEXT NOT NULL,
            point_count INTEGER NOT NULL,
            channels TEXT NOT NULL,
            encoding TEXT NOT NULL,
            samples BLOB NOT NULL,
            FOREIGN KEY(stable_key) REFERENCES tracks(stable_key)
        );
        """
    )


def read_analysis(root: Path | None) -> dict[str, dict]:
    if root is None:
        return {}
    if AnlzFile is None:
        raise SystemExit(
            "pyrekordbox is required for --analysis-root: "
            "python3 -m pip install pyrekordbox"
        )

    result: dict[str, dict] = {}
    dat_paths = sorted(root.rglob("ANLZ*.DAT"))
    progress = Progress("ANLZ files", len(dat_paths))
    for file_index, dat_path in enumerate(dat_paths, 1):
        try:
            dat = AnlzFile.parse_file(dat_path)
            audio_path = dat.get("PPTH")
        except Exception as exc:
            print(f"warning: could not parse {dat_path}: {exc}", file=sys.stderr)
            progress.update(file_index)
            continue

        analysis = {"beats": [], "cues": [], "waveform": None, "detail": None}
        if "PQTZ" in dat:
            beats, bpms, times = dat.get("PQTZ")
            analysis["beats"] = [
                (int(beat), float(bpm), float(time))
                for beat, bpm, time in zip(beats, bpms, times)
            ]

        files = [dat]
        ext_path = dat_path.with_suffix(".EXT")
        if ext_path.exists():
            try:
                files.append(AnlzFile.parse_file(ext_path))
            except Exception as exc:
                print(f"warning: could not parse {ext_path}: {exc}", file=sys.stderr)

        for anlz in reversed(files):
            if "PWV4" not in anlz:
                continue
            heights, colors, _ = anlz.get("PWV4")
            samples = bytearray()
            for height, color in zip(heights, colors):
                samples.extend(
                    struct.pack(
                        "BBBB", int(max(height)),
                        *(max(0, min(255, int(value))) for value in color[1]),
                    )
                )
            analysis["waveform"] = (
                "PWV4", len(heights), "amplitude,r,g,b", "zlib-u8x4",
                zlib.compress(bytes(samples), level=9),
            )
            break
        for anlz in reversed(files):
            if "PWV5" not in anlz:
                continue
            heights, colors = anlz.get("PWV5")
            samples = bytearray()
            for height, color in zip(heights, colors):
                samples.extend(struct.pack(
                    "BBBB", max(0, min(255, round(float(height) * 255))),
                    *(max(0, min(255, int(value) * 36)) for value in color)))
            analysis["detail"] = (
                "PWV5", len(heights), "amplitude,r,g,b", "zlib-u8x4",
                zlib.compress(bytes(samples), level=9),
            )
            break

        extended = []
        legacy = []
        for anlz in files:
            for tag_name, target in (("PCO2", extended), ("PCOB", legacy)):
                if tag_name not in anlz:
                    continue
                for cue_list in anlz.getall(tag_name):
                    list_kind = str(cue_list.get("type", cue_list.get("cue_type", "memory")))
                    for entry in cue_list.entries:
                        time_ms = int(entry.time)
                        loop_ms = int(entry.loop_time)
                        color = ""
                        if all(key in entry for key in ("color_red", "color_green", "color_blue")):
                            color = "#{:02X}{:02X}{:02X}".format(
                                entry.color_red, entry.color_green, entry.color_blue
                            )
                        target.append(
                            (list_kind, int(entry.hot_cue), time_ms / 1000,
                             None if loop_ms < 0 else loop_ms / 1000,
                             str(entry.get("comment", "")), color)
                        )
        analysis["cues"] = list(dict.fromkeys(extended or legacy))
        result[audio_path] = analysis
        progress.update(file_index)
    return result


def convert(source: Path, destination: Path, analysis_root: Path | None = None) -> dict[str, int]:
    print("Reading Rekordbox Device Library…", flush=True)
    db = Database.from_file(source)
    analyses = read_analysis(analysis_root)
    destination.mkdir(parents=True, exist_ok=True)
    playlist_root = destination / "Playlists"
    playlist_root.mkdir(exist_ok=True)
    cache_path = destination / "rbprep-library.sqlite"
    temporary_cache = destination / ".rbprep-library.sqlite.tmp"
    temporary_cache.unlink(missing_ok=True)

    artists = {row.id: row.name for row in db.artists}
    albums = {row.id: row.name for row in db.albums}
    genres = {row.id: row.name for row in db.genres}
    keys = {row.id: row.name for row in db.keys}
    colors = {row.id: row.name for row in db.colors}
    tracks = {row.id: row for row in db.tracks}
    paths = playlist_paths(db.playlist_tree)
    entries: dict[int, list] = defaultdict(list)
    for entry in db.playlist_entries:
        entries[entry.playlist_id].append(entry)
    for rows in entries.values():
        rows.sort(key=lambda row: row.entry_index)

    connection = sqlite3.connect(temporary_cache)
    try:
        create_schema(connection)
        connection.executemany(
            "INSERT INTO metadata(key, value) VALUES (?, ?)",
            [("schema_version", str(SCHEMA_VERSION)), ("source", str(source))],
        )
        track_progress = Progress("Tracks", len(tracks))
        for track_index, track in enumerate(tracks.values(), 1):
            artist = artists.get(track.artist_id, "")
            album = albums.get(track.album_id, "")
            genre = genres.get(track.genre_id, "").strip() or "UNCLASSIFIED"
            musical_key = keys.get(track.key_id, "")
            color = colors.get(track.color_id, "")
            track_id = str(track.id)
            stable_key = f"rb:{track_id}"
            bpm = track.tempo / 100 if track.tempo else None
            analysis = analyses.get(
                track.file_path, {"beats": [], "cues": [], "waveform": None, "detail": None}
            )
            cue_count = len(analysis["cues"])
            beat_grid_count = len(analysis["beats"])
            values = [
                track_id, track.file_path, track.title, artist, album, genre,
                "" if bpm is None else str(bpm), musical_key,
                str(track.year or ""), str(track.rating or ""),
                str(track.play_count or ""), track.comment, track.date_added,
                color, str(cue_count), str(beat_grid_count),
            ]
            connection.execute(
                "INSERT INTO tracks VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
                (stable_key, track_id, track.file_path, track.title, artist, album,
                 genre, bpm, musical_key, track.year or None, track.rating or None,
                 track.play_count or None, track.comment, track.date_added, color,
                 cue_count, beat_grid_count, digest(values)),
            )
            connection.executemany(
                "INSERT INTO beat_grid_points VALUES (?,?,?,?,?)",
                ((stable_key, ordinal, beat, grid_bpm, time_seconds)
                 for ordinal, (beat, grid_bpm, time_seconds)
                 in enumerate(analysis["beats"])),
            )
            connection.executemany(
                "INSERT INTO cue_points VALUES (?,?,?,?,?,?,?,?)",
                ((stable_key, ordinal, *cue)
                 for ordinal, cue in enumerate(analysis["cues"])),
            )
            if analysis["waveform"] is not None:
                connection.execute(
                    "INSERT INTO waveforms VALUES (?,?,?,?,?,?)",
                    (stable_key, *analysis["waveform"]),
                )
            if analysis["detail"] is not None:
                connection.execute(
                    "INSERT INTO detail_waveforms VALUES (?,?,?,?,?,?)",
                    (stable_key, *analysis["detail"]),
                )
            track_progress.update(track_index)

        playlist_count = 0
        playlist_entry_count = 0
        playlist_nodes = [node for node in db.playlist_tree if not node.is_folder]
        playlist_progress = Progress("Playlists", len(playlist_nodes))
        for node in sorted(playlist_nodes, key=lambda row: (paths[row.id][:-1], row.sort_order)):
            components = tuple(safe_component(value) for value in paths[node.id])
            relative = Path(*components).with_suffix(".m3u8")
            target = playlist_root / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            ordered = [row for row in entries.get(node.id, []) if row.track_id in tracks]
            track_ids = [str(row.track_id) for row in ordered]
            playlist_path = " / ".join(paths[node.id])
            connection.execute(
                "INSERT INTO playlists VALUES (?,?,?,?)",
                (node.id, playlist_path, node.name, digest(track_ids)),
            )
            connection.executemany(
                "INSERT INTO playlist_tracks VALUES (?,?,?)",
                ((node.id, ordinal, f"rb:{row.track_id}") for ordinal, row in enumerate(ordered)),
            )
            body = "#EXTM3U\n" + "".join(f"{tracks[row.track_id].file_path}\n" for row in ordered)
            target.write_text(body, encoding="utf-8", newline="\n")
            playlist_count += 1
            playlist_entry_count += len(ordered)
            playlist_progress.update(playlist_count)

        connection.commit()
    finally:
        connection.close()
    os.replace(temporary_cache, cache_path)

    summary = {
        "tracks": len(tracks),
        "playlists": playlist_count,
        "playlist_entries": playlist_entry_count,
        "folders": sum(node.is_folder for node in db.playlist_tree),
        "analyzed_tracks": sum(bool(value["beats"] or value["cues"]) for value in analyses.values()),
        "beat_grid_points": sum(len(value["beats"]) for value in analyses.values()),
        "cue_points": sum(len(value["cues"]) for value in analyses.values()),
        "waveform_tracks": sum(value["waveform"] is not None for value in analyses.values()),
        "waveform_points": sum(
            0 if value["waveform"] is None else value["waveform"][1]
            for value in analyses.values()
        ),
        "detail_waveform_tracks": sum(value["detail"] is not None for value in analyses.values()),
        "detail_waveform_points": sum(
            0 if value["detail"] is None else value["detail"][1]
            for value in analyses.values()
        ),
    }
    (destination / "rbprep-summary.json").write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    return summary


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("export_pdb", type=Path)
    parser.add_argument("output_directory", type=Path)
    parser.add_argument("--analysis-root", type=Path,
                        help="directory containing Rekordbox USBANLZ folders")
    args = parser.parse_args()
    summary = convert(args.export_pdb, args.output_directory, args.analysis_root)
    print(
        f"Imported {summary['tracks']} tracks and {summary['playlist_entries']} "
        f"ordered entries across {summary['playlists']} playlists; "
        f"{summary['beat_grid_points']} beat-grid points and "
        f"{summary['cue_points']} cues; {summary['waveform_tracks']} RGB waveforms."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
