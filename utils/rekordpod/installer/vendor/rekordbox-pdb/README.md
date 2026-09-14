# rekordbox-pdb

A small, dependency-free Python library to **read and write** the databases
rekordbox writes to USB sticks for CDJ/XDJ players:

* `PIONEER/rekordbox/export.pdb` — tracks, artists, albums, genres, keys,
  colors, labels, playlists, artwork, history
* `PIONEER/rekordbox/exportExt.pdb` — My Tag categories and tags

The on-disk format is a custom page-based store ("DeviceSQL"). It is fully
documented in [FORMAT.md](FORMAT.md), reverse engineered and verified
byte-for-byte against real exports (`tests/data/`), cross-checked
field-by-field against the independent Kaitai/crate-digger parser, and — for
the write path — validated by opening edited sticks in rekordbox itself.

No third-party dependencies; pure standard-library Python (3.10+).

## Install

```sh
pip install rekordbox-pdb            # from a checkout: pip install .
# or, with uv:
uv add rekordbox-pdb
```

## Reading

```python
from rekordbox_pdb import Database, ExtDatabase

db = Database.from_file("PIONEER/rekordbox/export.pdb")
for track in db.tracks:
    print(track.title, track.tempo / 100, track.file_path)

artists = {a.id: a.name for a in db.artists}
for entry in db.playlist_entries:
    print(entry.playlist_id, entry.track_id)

ext = ExtDatabase.from_file("PIONEER/rekordbox/exportExt.pdb")
for tag in ext.tags:
    print(tag.category_id, tag.name)
```

CLI dump:

```sh
python -m rekordbox_pdb path/to/export.pdb
python -m rekordbox_pdb path/to/exportExt.pdb --ext
```

## Writing

`PdbEditor` edits an `export.pdb` the way rekordbox itself does — surgical,
incremental changes rather than rewriting the file — so the result stays
structurally valid.

```python
from rekordbox_pdb.edit import PdbEditor

ed = PdbEditor.from_file("PIONEER/rekordbox/export.pdb")

# edit a fixed field in place
ed.set_track_field(track_id=1, field="rating", value=5)

# add a track (creates/reuses artist/album/genre/key rows as needed)
tid = ed.add_track(
    title="My Track",
    file_path="/Contents/Artist/Album/My Track.mp3",
    artist="Artist", album="Album", genre="House", key="Am",
    tempo=12800, duration=200, bitrate=320, sample_rate=44100,
)

# playlists and folders
pid = ed.create_playlist("Warm-up set")
ed.add_to_playlist(pid, tid)

ed.save("PIONEER/rekordbox/export.pdb")
```

It handles the hard parts of the format: heap allocation with the real
per-row size rules, the row directory that grows down from the page end,
presence/written bitmasks, the >255-slot count encoding, and allocating fresh
pages when one fills.

> **Note:** a database entry alone does not make a CDJ play a track — the
> audio file must exist at `file_path` on the stick, and players expect ANLZ
> analysis files (which rekordbox generates during analysis; this library does
> not create them). Always keep a backup of a stick before editing it.

## Tests

```sh
uv run pytest
```

Tests assert hand-verified values (from hex dumps and from what rekordbox
displayed for these libraries) against real export files. Some test groups run
only on machines that have larger local captures (a 713-track full-library
export and a differential test against the Kaitai parser generated from Deep
Symmetry's `rekordbox_pdb.ksy`); they skip gracefully elsewhere.

## Prior art

* [crate-digger](https://github.com/Deep-Symmetry/crate-digger) — Kaitai
  spec for `export.pdb`; the baseline this work verifies against.
* [rekordcrate](https://github.com/Holzhaus/rekordcrate) — Rust
  implementation, includes write support.
* [pyrekordbox](https://github.com/dylanljones/pyrekordbox) — Python
  toolkit for the desktop rekordbox databases (`master.db` etc.).

The `exportExt.pdb` My Tag row layout, the corrected `num_rows` slot-count
encoding, and the write-path rules documented in `FORMAT.md` were reverse
engineered here.

## Disclaimer

Not affiliated with or endorsed by AlphaTheta / Pioneer DJ. "rekordbox",
"CDJ", and "XDJ" are trademarks of their respective owners. This is an
independent, interoperability-focused project; use it on your own media and
keep backups.

## License

MIT — see [LICENSE](LICENSE).
