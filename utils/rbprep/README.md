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

Install the parser and run:

```
python3 -m pip install -r utils/rbprep/requirements.txt
python3 utils/rbprep/import_rekordbox.py \
    /Volumes/RIZZPOD/PIONEER/rekordbox/export.pdb output-directory \
    --analysis-root /Volumes/RIZZPOD/PIONEER/USBANLZ
```

The importer builds the SQLite cache under a temporary name and atomically
replaces the completed cache. It never changes `export.pdb`.
