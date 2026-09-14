"""Editing support for rekordbox export.pdb files.

Follows rekordbox's own write behaviour, as reverse engineered from
byte-diffs of consecutive exports of the same library and a census of
four real exports (see FORMAT.md "Writing"):

* Edits are surgical: rekordbox never rewrites the file; it patches
  fields in place, appends rows to page heaps, and clears presence bits
  to delete. Deleted slots are never reclaimed.
* A row appended into slot ``k`` gets heap offset = current
  ``used_size``; per-table allocation formulas (with 4-byte alignment
  and, for track/artist/album rows, a 4-byte tail) determine the next
  ``used_size``.
* Page bookkeeping on append: slot count (``nrs`` = n & 0xFF plus an
  overflow bit), present-count field, previous-count field (``nrl``),
  the presence bitmask, the written-this-save bitmask at group_base-2
  with its popcount at 0x20, ``free_size``/``used_size``, a per-table
  write-generation stamp at 0x10, and the file-header sequence number.
* New pages come from the table's empty candidate, which the last
  page's next_page already points at.
"""

from __future__ import annotations

import datetime
import struct
from pathlib import Path

from .pdb import Database, TableType

PAGE_HEADER_SIZE = 0x28

# Fixed-width track fields editable in place: name -> (row offset, size).
TRACK_FIXED_FIELDS = {
    "sample_rate": (0x08, 4),
    "file_size": (0x10, 4),
    "artwork_id": (0x1C, 4),
    "key_id": (0x20, 4),
    "original_artist_id": (0x24, 4),
    "label_id": (0x28, 4),
    "remixer_id": (0x2C, 4),
    "bitrate": (0x30, 4),
    "track_number": (0x34, 4),
    "tempo": (0x38, 4),
    "genre_id": (0x3C, 4),
    "album_id": (0x40, 4),
    "artist_id": (0x44, 4),
    "disc_number": (0x4C, 2),
    "play_count": (0x4E, 2),
    "year": (0x50, 2),
    "sample_depth": (0x52, 2),
    "duration": (0x54, 2),
    "color_id": (0x58, 1),
    "rating": (0x59, 1),
}

# File-type code at track row offset 0x5a, keyed by extension.
FILE_TYPE_CODES = {".mp3": 0x01, ".m4a": 0x04, ".wav": 0x0B, ".aiff": 0x0C, ".aif": 0x0C}


def _align4(n: int) -> int:
    return (n + 3) & ~3


def _check_range(name: str, value: int, size: int) -> None:
    if not 0 <= value < 1 << (8 * size):
        raise ValueError(f"{name}={value} does not fit in {size} byte(s)")


def encode_string(text: str) -> bytes:
    """Encode a DeviceSQL string the way rekordbox does."""
    try:
        ascii_bytes = text.encode("ascii")
    except UnicodeEncodeError:
        payload = text.encode("utf-16-le")
        return struct.pack("<BHB", 0x90, len(payload) + 4, 0) + payload
    if len(ascii_bytes) <= 126:
        return bytes([(len(ascii_bytes) + 1) * 2 + 1]) + ascii_bytes
    if len(ascii_bytes) > 32763:
        raise ValueError("string too long for DeviceSQL encoding")
    return struct.pack("<BHB", 0x40, len(ascii_bytes) + 4, 0) + ascii_bytes


class PdbEditor:
    """Edits an export.pdb in memory; write the result with save()."""

    def __init__(self, data: bytes):
        self._buf = bytearray(data)
        self._db: Database | None = None
        # Per-page append bookkeeping (page_index -> value).
        self._orig_slots: dict[int, int] = {}   # slot count before this session
        self._appends: dict[int, int] = {}      # rows appended this session
        # Tables whose pages must get a new write-generation stamp.
        self._new_gen: dict[int, int] = {}      # table type -> stamp value
        self._touched_pages: dict[int, set[int]] = {}  # table type -> pages
        self._structural = False
        self.page_size = struct.unpack_from("<I", self._buf, 4)[0]

    # -- construction / output ---------------------------------------------

    @classmethod
    def from_file(cls, path: str | Path) -> "PdbEditor":
        return cls(Path(path).read_bytes())

    def to_bytes(self) -> bytes:
        self._finalize()
        return bytes(self._buf)

    def save(self, path: str | Path) -> None:
        Path(path).write_bytes(self.to_bytes())

    @property
    def db(self) -> Database:
        """The current state, parsed. Reparsed after each mutation."""
        if self._db is None:
            self._db = Database(bytes(self._buf))
        return self._db

    # -- low-level helpers ---------------------------------------------------

    def _u16(self, off: int) -> int:
        return struct.unpack_from("<H", self._buf, off)[0]

    def _u32(self, off: int) -> int:
        return struct.unpack_from("<I", self._buf, off)[0]

    def _put(self, off: int, fmt: str, *values: int) -> None:
        struct.pack_into(fmt, self._buf, off, *values)

    def _table_entry_offset(self, table_type: int) -> int:
        num_tables = self._u32(8)
        for i in range(num_tables):
            off = 0x1C + i * 16
            if self._u32(off) == table_type:
                return off
        raise LookupError(f"no table of type {table_type}")

    # -- in-place edits -------------------------------------------------------

    def set_track_field(self, track_id: int, field: str, value: int) -> None:
        """Patch a fixed-width numeric track field in place.

        Changes exactly the bytes of that field; bookkeeping fields are
        left alone (readers don't consult them for in-place patches).
        """
        offset, size = TRACK_FIXED_FIELDS[field]  # KeyError for non-fixed fields
        _check_range(field, value, size)
        for track, location in zip(
            self.db.tracks, self.db.row_locations(TableType.TRACKS)
        ):
            if track.id == track_id:
                fmt = {1: "<B", 2: "<H", 4: "<I"}[size]
                self._put(location + offset, fmt, value)
                self._db = None
                return
        raise LookupError(f"no track with id {track_id}")

    # -- appending rows -------------------------------------------------------

    def _slot_count(self, page_off: int) -> int:
        return self._buf[page_off + 0x18] + 0x100 * (self._buf[page_off + 0x19] & 1)

    @staticmethod
    def _dir_bytes(n: int) -> int:
        return 2 * n + 4 * ((n + 15) // 16) if n else 0

    def _mark_table_touched(self, table_type: int, page_index: int) -> None:
        if table_type not in self._new_gen:
            entry = self._table_entry_offset(table_type)
            first, last = self._u32(entry + 8), self._u32(entry + 12)
            gen = 0
            page = first
            while True:
                gen = max(gen, self._u32(page * self.page_size + 0x10))
                if page == last:
                    break
                page = self._u32(page * self.page_size + 0x0C)
            self._new_gen[table_type] = gen + 1
        self._touched_pages.setdefault(table_type, set()).add(page_index)
        self._structural = True

    def _append_row(self, table_type: int, row: bytes, alloc: int,
                    index_shift_at: int | None = None) -> None:
        """Append one row to the end of a table, page bookkeeping included."""
        max_alloc = self.page_size - PAGE_HEADER_SIZE - self._dir_bytes(1)
        if alloc > max_alloc:
            raise ValueError(
                f"row allocation {alloc} exceeds page capacity {max_alloc}")
        entry = self._table_entry_offset(table_type)
        page_index = self._u32(entry + 12)  # last_page
        page_off = page_index * self.page_size

        if self._buf[page_off + 0x1B] & 0x40:
            # Empty table: its only page is the index page. Allocate.
            page_index = self._allocate_page(table_type)
            page_off = page_index * self.page_size

        n = self._slot_count(page_off)
        used = self._u16(page_off + 0x1E)
        if PAGE_HEADER_SIZE + used + alloc + self._dir_bytes(n + 1) > self.page_size:
            page_index = self._allocate_page(table_type)
            page_off = page_index * self.page_size
            n, used = 0, 0

        if page_index not in self._orig_slots:
            # First structural touch of this page in this session: reset the
            # written-this-save masks, exactly as a new rekordbox save does.
            self._orig_slots[page_index] = n
            self._appends[page_index] = 0
            for group in range((n + 15) // 16):
                self._put(page_off + self.page_size - group * 0x24 - 2, "<H", 0)
        self._mark_table_touched(table_type, page_index)

        slot = n
        group, bit = divmod(slot, 16)
        group_base = page_off + self.page_size - group * 0x24
        if bit == 0:
            # New row group: initialize its two flag words over heap garbage.
            self._put(group_base - 4, "<HH", 0, 0)

        # Row bytes (zero-padded to the allocated size) into the heap.
        row_start = page_off + PAGE_HEADER_SIZE + used
        padded = row + bytes(alloc - len(row))
        self._buf[row_start : row_start + alloc] = padded
        if index_shift_at is not None:
            self._put(row_start + index_shift_at, "<H", 0x20 * slot)

        # Row directory: offset, presence bit, written-this-save bit.
        self._put(group_base - 6 - 2 * bit, "<H", used)
        self._put(group_base - 4, "<H", self._u16(group_base - 4) | (1 << bit))
        self._put(group_base - 2, "<H", self._u16(group_base - 2) | (1 << bit))
        self._appends[page_index] += 1

        # Page header bookkeeping.
        new_n = n + 1
        if new_n > 511:
            raise ValueError("slot count encoding caps at 511 per page")
        present = self._present_count(page_off, new_n)
        self._buf[page_off + 0x18] = new_n & 0xFF
        self._put(page_off + 0x19, "<H",
                  0x20 * present | (1 if new_n > 255 else 0))
        used += alloc
        self._put(page_off + 0x1E, "<H", used)
        self._put(page_off + 0x1C, "<H",
                  self.page_size - PAGE_HEADER_SIZE - used - self._dir_bytes(new_n))
        self._put(page_off + 0x20, "<H", self._appends[page_index])
        self._put(page_off + 0x22, "<H", self._orig_slots[page_index])
        self._db = None

    def _present_count(self, page_off: int, n: int) -> int:
        count = 0
        for group in range((n + 15) // 16):
            base = page_off + self.page_size - group * 0x24
            in_group = min(16, n - group * 16)
            flags = self._u16(base - 4) & ((1 << in_group) - 1)
            count += bin(flags).count("1")
        return count

    def _allocate_page(self, table_type: int) -> int:
        """Turn the table's empty-candidate page into its new last page."""
        entry = self._table_entry_offset(table_type)
        new_page = self._u32(entry + 4)  # empty_candidate
        next_unused = self._u32(12)

        needed = (new_page + 1) * self.page_size
        if len(self._buf) < needed:
            self._buf.extend(bytes(needed - len(self._buf)))

        old_last = self._u32(entry + 12)
        page_off = new_page * self.page_size
        self._buf[page_off : page_off + self.page_size] = bytes(self.page_size)
        self._put(page_off + 0x04, "<I", new_page)
        self._put(page_off + 0x08, "<I", table_type)
        self._put(page_off + 0x0C, "<I", next_unused)  # -> new empty candidate
        self._buf[page_off + 0x1B] = 0x24  # data page, no deletions
        self._put(page_off + 0x1C, "<H", self.page_size - PAGE_HEADER_SIZE)

        self._put(entry + 12, "<I", new_page)      # table.last_page
        self._put(entry + 4, "<I", next_unused)    # table.empty_candidate
        self._put(12, "<I", next_unused + 1)       # header next_unused_page

        if self._buf[old_last * self.page_size + 0x1B] & 0x40:
            # First data page of a previously-empty table: point the index
            # page's "first data page" heap field at it.
            heap = old_last * self.page_size + PAGE_HEADER_SIZE
            if self._u32(heap + 4) == 0x03FFFFFF:
                self._put(heap + 4, "<I", new_page)

        self._orig_slots[new_page] = 0
        self._appends[new_page] = 0
        self._mark_table_touched(table_type, new_page)
        return new_page

    def _finalize(self) -> None:
        for table_type, pages in self._touched_pages.items():
            for page in pages:
                self._put(page * self.page_size + 0x10, "<I",
                          self._new_gen[table_type])
        if self._structural:
            # One editing session = one save transaction.
            if not hasattr(self, "_final_sequence"):
                self._final_sequence = self._u32(0x14) + 1
            self._put(0x14, "<I", self._final_sequence)

    # -- lookup-table helpers -------------------------------------------------

    def _get_or_create(self, table_type: int, name: str,
                       build) -> int:
        for row in self.db.rows(table_type):
            if row.name == name:
                return row.id
        new_id = max((r.id for r in self.db.rows(table_type)), default=0) + 1
        row_bytes, alloc, shift_at = build(new_id, name)
        self._append_row(table_type, row_bytes, alloc, index_shift_at=shift_at)
        return new_id

    @staticmethod
    def _build_named_row(row_id: int, name: str):
        """Genre / label / history-playlist shape: u32 id + name."""
        s = encode_string(name)
        return struct.pack("<I", row_id) + s, _align4(4 + len(s)), None

    @staticmethod
    def _build_artist_row(row_id: int, name: str):
        s = encode_string(name)
        row = struct.pack("<HHIBB", 0x60, 0, row_id, 0x03, 0x0A) + s
        return row, _align4(10 + _align4(len(s))) + 4, 2

    @staticmethod
    def _build_album_row(row_id: int, name: str, artist_id: int = 0):
        s = encode_string(name)
        row = struct.pack("<HHIIIIBB", 0x80, 0, 0, artist_id, row_id, 0,
                          0x03, 0x16) + s
        return row, _align4(22 + _align4(len(s))) + 4, 2

    @staticmethod
    def _build_key_row(row_id: int, name: str):
        s = encode_string(name)
        return struct.pack("<II", row_id, row_id) + s, _align4(8 + len(s)), None

    def get_or_create_artist(self, name: str) -> int:
        return self._get_or_create(TableType.ARTISTS, name, self._build_artist_row)

    def get_or_create_album(self, name: str, artist_id: int = 0) -> int:
        def build(row_id, album_name):
            return self._build_album_row(row_id, album_name, artist_id)
        return self._get_or_create(TableType.ALBUMS, name, build)

    def get_or_create_genre(self, name: str) -> int:
        return self._get_or_create(TableType.GENRES, name, self._build_named_row)

    def get_or_create_label(self, name: str) -> int:
        return self._get_or_create(TableType.LABELS, name, self._build_named_row)

    def get_or_create_key(self, name: str) -> int:
        return self._get_or_create(TableType.KEYS, name, self._build_key_row)

    # -- tracks -----------------------------------------------------------------

    def add_track(
        self,
        title: str,
        file_path: str,
        *,
        filename: str | None = None,
        artist: str | None = None,
        album: str | None = None,
        genre: str | None = None,
        key: str | None = None,
        label: str | None = None,
        comment: str = "",
        mix_name: str = "",
        release_date: str = "",
        analyze_path: str = "",
        date_added: str | None = None,
        tempo: int = 0,
        duration: int = 0,
        year: int = 0,
        bitrate: int = 0,
        sample_rate: int = 44100,
        sample_depth: int = 16,
        file_size: int = 0,
        track_number: int = 0,
        disc_number: int = 0,
        play_count: int = 0,
        rating: int = 0,
        color_id: int = 0,
        artwork_id: int = 0,
    ) -> int:
        """Add a track row; returns its id.

        Creates (or reuses) artist/album/genre/key/label rows by name.
        Note: for the track to actually play on a CDJ, the audio file
        must exist at file_path on the stick (and players expect ANLZ
        analysis files at analyze_path).
        """
        if filename is None:
            filename = file_path.rsplit("/", 1)[-1]
        if date_added is None:
            date_added = datetime.date.today().isoformat()

        # Validate everything that can fail BEFORE creating lookup rows,
        # so a bad argument can't leave orphan artist/album/... rows.
        for name, value, size in (
            ("sample_rate", sample_rate, 4), ("file_size", file_size, 4),
            ("bitrate", bitrate, 4), ("track_number", track_number, 4),
            ("tempo", tempo, 4), ("disc_number", disc_number, 2),
            ("play_count", play_count, 2), ("year", year, 2),
            ("sample_depth", sample_depth, 2), ("duration", duration, 2),
            ("artwork_id", artwork_id, 4), ("color_id", color_id, 1),
            ("rating", rating, 1),
        ):
            _check_range(name, value, size)

        strings = [
            "", "", "2", "2", "",         # 0-4 (0 = ISRC, left empty)
            "", "ON", "ON", "", "",       # 5-9 (kuvo_public, autoload_hotcues)
            date_added, release_date, mix_name, "",
            analyze_path, date_added,     # 14-15 (analyze_date)
            comment, title, "", filename, file_path,
        ]
        encoded = [encode_string(s) for s in strings]
        alloc = 0x88 + sum(_align4(len(e)) for e in encoded) + 4
        if alloc > self.page_size - PAGE_HEADER_SIZE - self._dir_bytes(1):
            raise ValueError("track row too large for one page")

        artist_id = self.get_or_create_artist(artist) if artist else 0
        album_id = self.get_or_create_album(album, artist_id) if album else 0
        genre_id = self.get_or_create_genre(genre) if genre else 0
        key_id = self.get_or_create_key(key) if key else 0
        label_id = self.get_or_create_label(label) if label else 0

        track_id = max((t.id for t in self.db.tracks), default=0) + 1
        # Unique per-track value at 0x14 (purpose unknown; unique in
        # every real export, so keep it unique here too).
        seen = {
            self._u32(loc + 0x14)
            for loc in self.db.row_locations(TableType.TRACKS)
        }
        unique = max(seen, default=0x00100000) + 1

        ext = "." + file_path.rsplit(".", 1)[-1].lower() if "." in file_path else ""
        file_type = FILE_TYPE_CODES.get(ext, 0x01)

        fixed = struct.pack(
            "<HHIIIIIHH12IHHHHHHBBHH",
            0x0024, 0,                    # magic, index_shift (patched on append)
            0x000C0700,                   # bitmask (constant in every export)
            sample_rate, 0,               # composer_id
            file_size, unique,
            0xAE49, 0x03DD,               # format constants
            artwork_id, key_id, 0, label_id, 0,
            bitrate, track_number, tempo, genre_id, album_id, artist_id,
            track_id,
            disc_number, play_count, year, sample_depth, duration,
            0x0029, color_id, rating, file_type, 0x0003,
        )
        assert len(fixed) == 0x5E

        offsets = []
        blob = bytearray()
        pos = 0x88
        for enc in encoded:
            if enc[0] & 1 == 0 and pos % 4:  # long strings start 4-aligned
                pad = 4 - pos % 4
                blob.extend(bytes(pad))
                pos += pad
            offsets.append(pos)
            blob.extend(enc)
            pos += len(enc)

        row = fixed + struct.pack("<21H", *offsets) + bytes(blob)
        self._append_row(TableType.TRACKS, row, alloc, index_shift_at=2)
        return track_id

    # -- playlists ----------------------------------------------------------------

    def create_playlist(self, name: str, parent_id: int = 0,
                        is_folder: bool = False) -> int:
        nodes = self.db.playlist_tree
        node_id = max((n.id for n in nodes), default=0) + 1
        sort_order = max(
            (n.sort_order for n in nodes if n.parent_id == parent_id),
            default=-1) + 1
        s = encode_string(name)
        row = struct.pack("<IIIII", parent_id, 0, sort_order, node_id,
                          1 if is_folder else 0) + s
        self._append_row(TableType.PLAYLIST_TREE, row, _align4(20 + len(s)))
        return node_id

    def add_to_playlist(self, playlist_id: int, track_id: int,
                        entry_index: int | None = None) -> None:
        nodes = {n.id: n for n in self.db.playlist_tree}
        if playlist_id not in nodes or nodes[playlist_id].is_folder:
            raise LookupError(f"no playlist with id {playlist_id}")
        if entry_index is None:
            entry_index = max(
                (e.entry_index for e in self.db.playlist_entries
                 if e.playlist_id == playlist_id),
                default=0) + 1
        row = struct.pack("<III", entry_index, track_id, playlist_id)
        self._append_row(TableType.PLAYLIST_ENTRIES, row, 12)
