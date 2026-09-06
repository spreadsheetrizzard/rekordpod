# RBPrep Rekordbox metadata importer

`import_rekordbox.py` reads the `PIONEER/rekordbox/export.pdb` Device Library
database and produces two inputs:

* `Playlists/**/*.m3u8`, preserving Rekordbox folders, playlist order, and the
  volume-rooted `/Contents/...` paths Rockbox uses.
* `rbprep-library.sqlite`, using the metadata fields and stable identities from
  Drag'n'Dunk (`rb:<TrackID>`).

The SQLite cache includes title, artist, album, genre, BPM, key, year, rating,
play count, comments, date added, color, playlist membership, and ordinal.
Cue and beat-grid columns are present but remain zero until the ANLZ importer is
connected.

Install the parser and run:

```
python3 -m pip install rekordbox-pdb
python3 utils/rbprep/import_rekordbox.py \
    /Volumes/RIZZPOD/PIONEER/rekordbox/export.pdb output-directory
```

The importer builds the SQLite cache under a temporary name and atomically
replaces the completed cache. It never changes `export.pdb`.
