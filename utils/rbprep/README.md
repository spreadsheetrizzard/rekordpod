# RBPrep Rekordbox metadata importer

`import_rekordbox.py` reads the `PIONEER/rekordbox/export.pdb` Device Library
database and produces two inputs:

* `Playlists/**/*.m3u8`, preserving Rekordbox folders, playlist order, and the
  volume-rooted `/Contents/...` paths Rockbox uses.
* `rbprep-library.sqlite`, using the metadata fields and stable identities from
  Drag'n'Dunk (`rb:<TrackID>`).

The SQLite cache includes title, artist, album, genre, BPM, key, year, rating,
play count, comments, date added, color, playlist membership, and ordinal. When
`--analysis-root` is supplied, it also joins ANLZ files to tracks by their exact
`PPTH` audio paths and imports every beat-grid point and memory/hot cue.
The compact `waveforms` table preserves Rekordbox's native `PWV4` RGB overview
as zlib-compressed amplitude/red/green/blue byte tuples (1,200 points on the
captured exports), ready for a viewport without decoding the audio.
At close zoom levels, the `detail_waveforms` table supplies native `PWV5`
amplitude and RGB samples instead of enlarging the 1,200-point overview.

Install the parser and run:

```
python3 -m pip install -r utils/rbprep/requirements.txt
python3 utils/rbprep/import_rekordbox.py \
    /Volumes/RIZZPOD/PIONEER/rekordbox/export.pdb output-directory \
    --analysis-root /Volumes/RIZZPOD/PIONEER/USBANLZ
```

The importer builds the SQLite cache under a temporary name and atomically
replaces the completed cache. It never changes `export.pdb`.

`build_device_cache.py` combines that SQLite library with a Rockbox ZIP. Its
RBI2 index remains readable by the plugin's RBI1 compatibility path and adds
musical key strings plus stable Rekordbox playlist IDs.

`apply_device_edits.py` is the explicit write-back step. It keeps only the
newest full snapshot per track, diffs that state against the current DeviceSQL
database and ANLZ data, and applies only the net metadata, beat-grid, hot-cue,
and playlist changes. Before replacing anything it builds and validates all
outputs; apply mode creates timestamped copies of every affected device file
and rolls back already-replaced files if the transaction fails. The journals
are compacted to one snapshot/request per object after a successful apply, so
reversed or repeated edits do not accumulate.

Run a read-only preview first:

```
PYTHONPATH=/path/to/rekordbox-pdb/src python3 \
    utils/rbprep/apply_device_edits.py \
    --volume /Volumes/RIZZPOD --backup-root /path/to/backups
```

Add `--apply` only after reviewing the preview. The macOS launcher supplied
with RBPrep performs both passes, displays a confirmation, flushes writes, and
ejects the volume.
