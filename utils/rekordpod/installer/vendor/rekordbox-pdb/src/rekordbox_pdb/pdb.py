"""Parser for rekordbox `export.pdb` — the DeviceSQL database rekordbox
writes to `/PIONEER/rekordbox/` on exported USB media.

Layout (verified byte-by-byte against real exports; see FORMAT.md):

* The file is an array of fixed-size pages (4096 bytes in every export seen).
* The file header (page 0) holds the page size and a table directory:
  one (type, empty_candidate, first_page, last_page) entry per table.
* Each table is a linked list of pages, `first_page` .. `last_page`,
  chained by the `next_page` field of each page header.
* Pages whose flags have bit 0x40 set hold no rows; data pages keep row
  data in a heap growing up from offset 0x28, and row offsets in groups
  of up to 16 growing down from the page end (0x24 bytes per group:
  16 u16 offsets, a u16 presence bitmask at group_base - 4).
* Strings ("DeviceSQL strings") are either short ASCII — one odd length
  byte `b`, text length `(b >> 1) - 1` — or long: a kind byte (0x40
  ASCII / 0x90 UTF-16LE), a u16 total length that includes the 4-byte
  header, one zero byte, then the payload.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass
from enum import IntEnum
from pathlib import Path
from typing import Callable

__all__ = [
    "Album",
    "Artist",
    "Artwork",
    "Color",
    "Column",
    "Database",
    "ExtDatabase",
    "ExtTableType",
    "Genre",
    "HistoryEntry",
    "HistoryPlaylist",
    "Key",
    "Label",
    "PlaylistEntry",
    "PlaylistTreeNode",
    "TableType",
    "Tag",
    "Track",
]

PAGE_HEADER_SIZE = 0x28
ROW_GROUP_SIZE = 0x24  # 16 u16 row offsets + u16 presence flags + u16 pad


class ExtTableType(IntEnum):
    """Table types in exportExt.pdb (a different set from export.pdb)."""

    UNKNOWN_0 = 0
    UNKNOWN_1 = 1
    UNKNOWN_2 = 2
    TAGS = 3  # My Tag categories and tags
    UNKNOWN_4 = 4
    UNKNOWN_5 = 5
    UNKNOWN_6 = 6
    UNKNOWN_7 = 7  # one row with five (empty) string slots; purpose unknown
    TAG_TRACKS = 8  # believed to associate tags with tracks; unverified


class TableType(IntEnum):
    TRACKS = 0
    GENRES = 1
    ARTISTS = 2
    ALBUMS = 3
    LABELS = 4
    KEYS = 5
    COLORS = 6
    PLAYLIST_TREE = 7
    PLAYLIST_ENTRIES = 8
    UNKNOWN_9 = 9
    UNKNOWN_10 = 10
    HISTORY_PLAYLISTS = 11
    HISTORY_ENTRIES = 12
    ARTWORK = 13
    UNKNOWN_14 = 14
    UNKNOWN_15 = 15
    COLUMNS = 16
    UNKNOWN_17 = 17
    UNKNOWN_18 = 18
    HISTORY = 19


def _u16(buf: bytes, off: int) -> int:
    return struct.unpack_from("<H", buf, off)[0]


def _u32(buf: bytes, off: int) -> int:
    return struct.unpack_from("<I", buf, off)[0]


def decode_string(buf: bytes, off: int) -> str:
    """Decode a DeviceSQL string starting at `off`."""
    b0 = buf[off]
    if b0 & 1:  # short ASCII: length byte covers itself, mangled *2+1
        length = (b0 >> 1) - 1
        return buf[off + 1 : off + 1 + length].decode("ascii")
    total = _u16(buf, off + 1)
    payload = buf[off + 4 : off + total]
    if b0 == 0x40:  # long ASCII
        return payload.decode("ascii")
    if b0 == 0x90:  # long UTF-16LE
        return payload.decode("utf-16-le")
    raise ValueError(f"unknown DeviceSQL string kind {b0:#04x} at offset {off:#x}")


@dataclass(frozen=True)
class Table:
    type: int
    empty_candidate: int
    first_page: int
    last_page: int


# ---------------------------------------------------------------------------
# Row types
# ---------------------------------------------------------------------------

_TRACK_FIXED = struct.Struct("<HHIIIIIHH12I5H HBBHH")

# Names of the 21 track string slots, in file order. The "unknown_*" ones
# have not been conclusively identified (unknown_string_1 sometimes holds
# the ISRC).
TRACK_STRING_NAMES = (
    "unknown_string_1",
    "texter",
    "unknown_string_2",
    "unknown_string_3",
    "unknown_string_4",
    "message",
    "kuvo_public",
    "autoload_hotcues",
    "unknown_string_5",
    "unknown_string_6",
    "date_added",
    "release_date",
    "mix_name",
    "unknown_string_7",
    "analyze_path",
    "analyze_date",
    "comment",
    "title",
    "unknown_string_8",
    "filename",
    "file_path",
)


@dataclass(frozen=True)
class Track:
    index_shift: int
    bitmask: int
    sample_rate: int
    composer_id: int
    file_size: int
    artwork_id: int
    key_id: int
    original_artist_id: int
    label_id: int
    remixer_id: int
    bitrate: int
    track_number: int
    tempo: int  # BPM * 100
    genre_id: int
    album_id: int
    artist_id: int
    id: int
    disc_number: int
    play_count: int
    year: int
    sample_depth: int
    duration: int  # seconds
    color_id: int
    rating: int
    strings: tuple[str, ...]  # the 21 string slots, in file order

    def _s(self, index: int) -> str:
        return self.strings[index]

    @property
    def message(self) -> str:
        return self._s(5)

    @property
    def date_added(self) -> str:
        return self._s(10)

    @property
    def release_date(self) -> str:
        return self._s(11)

    @property
    def mix_name(self) -> str:
        return self._s(12)

    @property
    def analyze_path(self) -> str:
        return self._s(14)

    @property
    def analyze_date(self) -> str:
        return self._s(15)

    @property
    def comment(self) -> str:
        return self._s(16)

    @property
    def title(self) -> str:
        return self._s(17)

    @property
    def filename(self) -> str:
        return self._s(19)

    @property
    def file_path(self) -> str:
        return self._s(20)

    @classmethod
    def parse(cls, page: bytes, row: int) -> "Track":
        f = _TRACK_FIXED.unpack_from(page, row)
        (
            _, index_shift, bitmask, sample_rate, composer_id, file_size,
            _, _, _,
            artwork_id, key_id, original_artist_id, label_id, remixer_id,
            bitrate, track_number, tempo, genre_id, album_id, artist_id,
            track_id,
            disc_number, play_count, year, sample_depth, duration,
            _, color_id, rating, _, _,
        ) = f
        ofs = struct.unpack_from("<21H", page, row + _TRACK_FIXED.size)
        strings = tuple(decode_string(page, row + o) for o in ofs)
        return cls(
            index_shift=index_shift, bitmask=bitmask, sample_rate=sample_rate,
            composer_id=composer_id, file_size=file_size, artwork_id=artwork_id,
            key_id=key_id, original_artist_id=original_artist_id,
            label_id=label_id, remixer_id=remixer_id, bitrate=bitrate,
            track_number=track_number, tempo=tempo, genre_id=genre_id,
            album_id=album_id, artist_id=artist_id, id=track_id,
            disc_number=disc_number, play_count=play_count, year=year,
            sample_depth=sample_depth, duration=duration, color_id=color_id,
            rating=rating, strings=strings,
        )


@dataclass(frozen=True)
class Genre:
    id: int
    name: str

    @classmethod
    def parse(cls, page: bytes, row: int) -> "Genre":
        return cls(id=_u32(page, row), name=decode_string(page, row + 4))


@dataclass(frozen=True)
class Label:
    id: int
    name: str

    @classmethod
    def parse(cls, page: bytes, row: int) -> "Label":  # same layout as Genre
        return cls(id=_u32(page, row), name=decode_string(page, row + 4))


@dataclass(frozen=True)
class Artist:
    id: int
    name: str
    index_shift: int

    @classmethod
    def parse(cls, page: bytes, row: int) -> "Artist":
        subtype = _u16(page, row)
        index_shift = _u16(page, row + 2)
        artist_id = _u32(page, row + 4)
        if subtype == 0x64:  # name too far away for the one-byte near offset
            ofs_name = _u16(page, row + 0x0A)
        else:  # 0x60
            ofs_name = page[row + 9]
        return cls(id=artist_id, name=decode_string(page, row + ofs_name),
                   index_shift=index_shift)


@dataclass(frozen=True)
class Album:
    id: int
    name: str
    artist_id: int
    index_shift: int

    @classmethod
    def parse(cls, page: bytes, row: int) -> "Album":
        index_shift = _u16(page, row + 2)
        artist_id = _u32(page, row + 8)
        album_id = _u32(page, row + 12)
        ofs_name = page[row + 0x15]
        return cls(id=album_id, name=decode_string(page, row + ofs_name),
                   artist_id=artist_id, index_shift=index_shift)


@dataclass(frozen=True)
class Key:
    id: int
    name: str

    @classmethod
    def parse(cls, page: bytes, row: int) -> "Key":
        return cls(id=_u32(page, row), name=decode_string(page, row + 8))


@dataclass(frozen=True)
class Color:
    id: int
    name: str

    @classmethod
    def parse(cls, page: bytes, row: int) -> "Color":
        return cls(id=_u16(page, row + 5), name=decode_string(page, row + 8))


@dataclass(frozen=True)
class PlaylistTreeNode:
    id: int
    parent_id: int
    sort_order: int
    name: str
    is_folder: bool

    @classmethod
    def parse(cls, page: bytes, row: int) -> "PlaylistTreeNode":
        return cls(
            parent_id=_u32(page, row),
            sort_order=_u32(page, row + 8),
            id=_u32(page, row + 12),
            is_folder=_u32(page, row + 16) != 0,
            name=decode_string(page, row + 20),
        )


@dataclass(frozen=True)
class PlaylistEntry:
    entry_index: int
    track_id: int
    playlist_id: int

    @classmethod
    def parse(cls, page: bytes, row: int) -> "PlaylistEntry":
        return cls(
            entry_index=_u32(page, row),
            track_id=_u32(page, row + 4),
            playlist_id=_u32(page, row + 8),
        )


@dataclass(frozen=True)
class Artwork:
    id: int
    path: str

    @classmethod
    def parse(cls, page: bytes, row: int) -> "Artwork":
        return cls(id=_u32(page, row), path=decode_string(page, row + 4))


@dataclass(frozen=True)
class Column:
    id: int
    name: str  # wrapped in U+FFFA / U+FFFB by rekordbox

    @classmethod
    def parse(cls, page: bytes, row: int) -> "Column":
        return cls(id=_u16(page, row), name=decode_string(page, row + 4))


@dataclass(frozen=True)
class HistoryPlaylist:
    id: int
    name: str

    @classmethod
    def parse(cls, page: bytes, row: int) -> "HistoryPlaylist":
        return cls(id=_u32(page, row), name=decode_string(page, row + 4))


@dataclass(frozen=True)
class HistoryEntry:
    track_id: int
    playlist_id: int
    entry_index: int

    @classmethod
    def parse(cls, page: bytes, row: int) -> "HistoryEntry":
        return cls(
            track_id=_u32(page, row),
            playlist_id=_u32(page, row + 4),
            entry_index=_u32(page, row + 8),
        )


@dataclass(frozen=True)
class Tag:
    """A My Tag category or tag from exportExt.pdb.

    Categories have small ordinal ids (1..4 by default) and
    ``is_category`` set; tags carry their parent's id in
    ``category_id`` and a persistent 32-bit id assigned by rekordbox.
    """

    id: int
    name: str
    category_id: int
    position: int
    is_category: bool
    index_shift: int

    @classmethod
    def parse(cls, page: bytes, row: int) -> "Tag":
        return cls(
            index_shift=_u16(page, row + 2),
            category_id=_u32(page, row + 0x0C),
            position=_u32(page, row + 0x10),
            id=_u32(page, row + 0x14),
            is_category=page[row + 0x1B] != 0,
            name=decode_string(page, row + page[row + 0x1D]),
        )


@dataclass(frozen=True)
class UnknownRow:
    """Row in a table whose layout has not been reverse engineered."""

    page_index: int
    offset: int  # of the row within its page


_ROW_PARSERS: dict[int, Callable[[bytes, int], object]] = {
    TableType.TRACKS: Track.parse,
    TableType.GENRES: Genre.parse,
    TableType.ARTISTS: Artist.parse,
    TableType.ALBUMS: Album.parse,
    TableType.LABELS: Label.parse,
    TableType.KEYS: Key.parse,
    TableType.COLORS: Color.parse,
    TableType.PLAYLIST_TREE: PlaylistTreeNode.parse,
    TableType.PLAYLIST_ENTRIES: PlaylistEntry.parse,
    TableType.ARTWORK: Artwork.parse,
    TableType.COLUMNS: Column.parse,
    TableType.HISTORY_PLAYLISTS: HistoryPlaylist.parse,
    TableType.HISTORY_ENTRIES: HistoryEntry.parse,
}


class PdbFile:
    """Shared DeviceSQL container logic for export.pdb / exportExt.pdb."""

    _row_parsers: dict[int, Callable[[bytes, int], object]] = {}

    def __init__(self, data: bytes):
        self._data = data
        self.page_size = _u32(data, 4)
        num_tables = _u32(data, 8)
        self.tables = [
            Table(
                type=_u32(data, 0x1C + i * 16),
                empty_candidate=_u32(data, 0x1C + i * 16 + 4),
                first_page=_u32(data, 0x1C + i * 16 + 8),
                last_page=_u32(data, 0x1C + i * 16 + 12),
            )
            for i in range(num_tables)
        ]
        self._rows: dict[int, list] = {}
        self._locations: dict[int, list[int]] = {}
        for table in self.tables:
            self._rows[table.type], self._locations[table.type] = (
                self._parse_table(table))

    @classmethod
    def from_file(cls, path: str | Path):
        return cls(Path(path).read_bytes())

    @classmethod
    def from_bytes(cls, data: bytes):
        return cls(data)

    def rows(self, table_type: int) -> list:
        return self._rows.get(table_type, [])

    def row_locations(self, table_type: int) -> list[int]:
        """Absolute file offset of each row, parallel to rows()."""
        return self._locations.get(table_type, [])

    # -- parsing -------------------------------------------------------------

    def _page(self, index: int) -> bytes:
        start = index * self.page_size
        return self._data[start : start + self.page_size]

    def _parse_table(self, table: Table) -> tuple[list, list[int]]:
        parser = self._row_parsers.get(table.type)
        rows: list = []
        locations: list[int] = []
        page_index = table.first_page
        while True:
            page = self._page(page_index)
            for row_offset in self._row_offsets(page):
                if parser is not None:
                    rows.append(parser(page, row_offset))
                else:
                    rows.append(UnknownRow(page_index=page_index, offset=row_offset))
                locations.append(page_index * self.page_size + row_offset)
            if page_index == table.last_page:
                break
            page_index = _u32(page, 12)  # next_page
        return rows, locations

    def _row_offsets(self, page: bytes) -> list[int]:
        """Offsets (within the page) of all present rows on a data page."""
        page_flags = page[27]
        if page_flags & 0x40:  # not a data page
            return []
        # Slot count n is stored as n & 0xFF at 0x18 plus an overflow bit
        # (bit 0 of the u16 at 0x19, whose upper bits hold 0x20 * the
        # number of present rows). The often-cited heuristic based on
        # num_rows_large @0x22 under-reads pages that ever exceeded 255
        # slots; @0x22 is really the slot count at the previous write.
        num_rows = page[24] + 0x100 * (page[25] & 1)
        if num_rows == 0:
            return []
        offsets = []
        num_groups = (num_rows - 1) // 16 + 1
        for group in range(num_groups):
            base = self.page_size - group * ROW_GROUP_SIZE
            present_flags = _u16(page, base - 4)
            in_group = 16 if group < num_groups - 1 else (num_rows - 1) % 16 + 1
            for i in range(in_group):
                if (present_flags >> i) & 1:
                    offsets.append(PAGE_HEADER_SIZE + _u16(page, base - 6 - 2 * i))
        return offsets


class Database(PdbFile):
    """A parsed export.pdb file."""

    _row_parsers = _ROW_PARSERS

    @property
    def tracks(self) -> list[Track]:
        return self.rows(TableType.TRACKS)

    @property
    def genres(self) -> list[Genre]:
        return self.rows(TableType.GENRES)

    @property
    def artists(self) -> list[Artist]:
        return self.rows(TableType.ARTISTS)

    @property
    def albums(self) -> list[Album]:
        return self.rows(TableType.ALBUMS)

    @property
    def labels(self) -> list[Label]:
        return self.rows(TableType.LABELS)

    @property
    def keys(self) -> list[Key]:
        return self.rows(TableType.KEYS)

    @property
    def colors(self) -> list[Color]:
        return self.rows(TableType.COLORS)

    @property
    def playlist_tree(self) -> list[PlaylistTreeNode]:
        return self.rows(TableType.PLAYLIST_TREE)

    @property
    def playlist_entries(self) -> list[PlaylistEntry]:
        return self.rows(TableType.PLAYLIST_ENTRIES)

    @property
    def artwork(self) -> list[Artwork]:
        return self.rows(TableType.ARTWORK)

    @property
    def columns(self) -> list[Column]:
        return self.rows(TableType.COLUMNS)

    @property
    def history_playlists(self) -> list[HistoryPlaylist]:
        return self.rows(TableType.HISTORY_PLAYLISTS)

    @property
    def history_entries(self) -> list[HistoryEntry]:
        return self.rows(TableType.HISTORY_ENTRIES)


class ExtDatabase(PdbFile):
    """A parsed exportExt.pdb file (My Tag data)."""

    _row_parsers = {
        ExtTableType.TAGS: Tag.parse,
    }

    @property
    def tags(self) -> list[Tag]:
        return self.rows(ExtTableType.TAGS)
