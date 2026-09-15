# Rekordpod Rekordbox metadata importer

These host-side utilities are optional maintainer, recovery, and format-test
tools. They are not part of ordinary Rekordpod installation. Public-beta users
merge the single Rockbox overlay on Windows, macOS, or Linux; Rekordpod then
builds its collection index and converts existing Rekordbox analysis locally on
the iPod. The device does not analyze or re-encode the audio itself.

The commands below document the reproducible host implementation used to
inspect formats, construct fixtures, and recover a damaged beta installation.

`import_rekordbox.py` reads the `PIONEER/rekordbox/export.pdb` Device Library
database and produces two inputs:

* `Playlists/**/*.m3u8`, preserving Rekordbox folders, playlist order, and the
  volume-rooted `/Contents/...` paths Rockbox uses.
* `rekordpod-library.sqlite`, using the metadata fields and stable identities from
  Drag'n'Dunk (`rb:<TrackID>`).

The SQLite cache includes title, artist, album, genre, BPM, key, year, rating,
play count, comments, comment hashtags, date added, color, playlist membership,
and ordinal. When
`--analysis-root` is supplied, it also joins ANLZ files to tracks by their exact
`PPTH` audio paths and imports every beat-grid point and memory/hot cue.
The compact `waveforms` table preserves Rekordbox's native `PWV4` RGB overview
as zlib-compressed amplitude/red/green/blue byte tuples (1,200 points on the
captured exports), ready for a viewport without decoding the audio.
At close zoom levels, the `detail_waveforms` table supplies native `PWV5`
amplitude and RGB samples instead of enlarging the 1,200-point overview.

Install the parser and run:

```
python3 -m pip install -r utils/rekordpod/requirements.txt
python3 utils/rekordpod/import_rekordbox.py \
    /Volumes/RIZZPOD/PIONEER/rekordbox/export.pdb output-directory \
    --analysis-root /Volumes/RIZZPOD/PIONEER/USBANLZ
```

The importer builds the SQLite cache under a temporary name and atomically
replaces the completed cache. It never changes `export.pdb`.

For maintainer testing, `build_device_cache.py` combines that SQLite library
with a Rockbox ZIP. Its
RBI3 index remains readable by the plugin's RBI1 compatibility path and adds a
single-read search string per track, year/comments/tags/import-date metadata,
six compact precomputed sort maps, musical keys, and stable Rekordbox playlist IDs. The
device Collection can therefore search title/artist/genre/key/comments/tags
or import date and sort by title, BPM, year, key, comments, tags, or import
date without sorting thousands of records on the iPod. BPM and musical key
remain visible together in every Collection row.

The Rekordpod plugin writes confirmed changes to its verified journal immediately,
but coalesces repeated work on one track into the newest complete snapshot.
When a dirty track unloads, Rekordpod asks whether to save or discard it before
the next track takes over. Confirmed playlist operations burn immediately
without running from wheel movement. Analysis edits stop playback only while
borrowing the codec buffer; playlist-only commits keep playback allocated.
Local burn keeps persistent
`.rekordpod-bak` originals, consumes only the journal tail after the last
successful marker, and refuses incomplete or over-capacity journals instead of
silently advancing them. Repeated playlist operations replay idempotently, the
local RBI3 membership table is compacted on every playlist transaction, and
both the rebuilt RBI3 index and every DeviceSQL page chain are validated before
their rollback copies are removed. A failure names the exact stage or
track/list pair and rolls the transaction back.

The deck offers eight signal-oriented views across VIZ and MORVIZ: RGB
waveform, 20-band EQ, phrase map, harmonic constellation, spectral canyon,
boombox, stereo orbit, and Oscillo-Turntable. The boombox follows the live
sub-120 Hz envelope and shows a confidence-gated observed bass note; the
turntable combines a readable horizontal oscilloscope with platter, Phase,
cue, RPM, and tonearm motion. Non-waveform analyzers intentionally omit edit
overlays; beatgrid, loop, and cue detail stays on the RGB waveform.

Prep Deck tools are arranged in ten pages: PLAYER, PLNAV, BTGRID, LISTS,
HOTCUE, VIZ, MORVIZ, TEMPO, LOOPS, and LOCK. Pages contain at most five orbs;
PLAYER uses the fifth position and the rest use four or fewer. Star rating is
the fourth BTGRID tool. Genre, track color, year, and musical key are imported
for display/search but are not editable.
While the tool picker is open, LEFT/RIGHT or the wheel moves within a page and
physical up/down banks pages while preserving (or clamping) the orb column.
Two named workflow pads can retain up to 48 ordered tool selections each;
repeated tools remain repeated cells. Workflows never replay edits or input
timing: horizontal stepping only selects the next tool. SELECT+LEFT/RIGHT
steps within a row and SELECT+up/down swaps the two rows. A playlist's hold
menu can associate M1 or M2 so the pad appears automatically during prep.
Hold SELECT on either LOCK workflow orb to open its manager. Each workflow can be
renamed, edited, or cleared. The sequence editor supports explicit INSERT,
REPLACE, and DELETE operations at any valid cell; its paged picker exposes all
ordinary Prep Deck tools but excludes the two workflow orbs to prevent
recursive pads. Editing only changes the stored tool sequence and never
executes a tool, seeks, or modifies track metadata.
Workflow files use stable, append-only tool identifiers. Older RBM1/RBM2 files
are upgraded to RBM3 without changing their sequence, and the workflow picker
uses the same canonical names and icon renderer as the live tool orbs. The
LOCK page orders M1, M2, Pitch Lock, and Quantize.
`tool-macros.rbm` and `playlist-workflows.rbl` are independent of analysis
caches and are preserved by ordinary overlay installs.

Internally smart playlists use RBI node kind `2` (folder is `0`, materialized
playlist is `1`) and are marked SMART in the device browser. Their rules live
in `/.rockbox/rekordpod/smart-playlists.rbq`: the first line is `RBQ1`; each later
line begins with the stable playlist ID and a tab, followed by versioned flags,
a reserved native-Rekordbox rule-kind field, and the query payload. Rekordpod only
reads the leading ID today, deliberately retaining the complete remaining
query text byte-for-byte. Local burn materializes current membership as a
traditional playlist for CDJ compatibility while preserving node kind `2` and
the RBQ rule for future experimental native smart-playlist export.

The playback tools have independent host and played RPM selectors (33, 45, or
78; 33→33 by default), a +/-16% pitch-bend control, and a 0.1%-resolution tempo
control. RPM and pitch bend change pitch and speed together; tempo is applied
through Rockbox timestretch as a temporary pitch-lock experiment. All rate
settings reset for a newly loaded song and the pre-plugin Rockbox
pitch/timestretch state is restored on exit. The main menu's Prep Deck item can
attach to the current indexed Rockbox track without restarting it.

This fork launches Rekordpod once during Rockbox startup when
`/.rockbox/rocks/apps/rekordpod.rock` is present. The `autoboot` launch draws an
animated `rekordpod` wordmark using the bundled Adobe Helvetica font. Holding
MENU during boot bypasses Rekordpod, and Rekordpod's Exit to Rockbox item returns to
the ordinary root menu without relaunching it. Main and browser selections use
rounded capsule geometry; the main menu is icon-first and the playlist tree has
separate folder and playlist glyphs. Search and playlist naming use a Rekordpod
wheel keyboard with selectable Space, Backspace, and Done keys plus direct LEFT,
RIGHT, PLAY, and MENU shortcuts.

Local burn updates cue/grid/rating data in the traditional Rekordbox Device
Library (`export.pdb`) and its DAT/EXT analysis files. Playlist curation is
written beneath the `REKORDPOD - IMPORT ME` root folder. Smart-playlist rules
remain device-local and only their materialized membership can be represented
as an ordinary Device Library playlist. Hardware that reads only OneLibrary / Device
Library Plus needs a matching Plus-library update; Rekordpod does not write that
second database yet.

`apply_device_edits.py` is an optional host-side recovery and diagnostic tool,
not part of the normal Rekordpod workflow. Normal rating, beat-grid, hot-cue,
and playlist burns happen entirely on the iPod. The utility can preview or
apply a surviving device journal when investigating a damaged beta install;
it builds and validates all outputs before replacement, creates timestamped
copies of every affected file, and rolls back already-replaced files if the
transaction fails.

Run a read-only preview first:

```
PYTHONPATH=/path/to/rekordbox-pdb/src python3 \
    utils/rekordpod/apply_device_edits.py \
    --volume /Volumes/RIZZPOD --backup-root /path/to/backups
```

Add `--apply` only after reviewing the preview. It is not required to prepare
tracks or playlists in Rekordpod.

Public builds compile the retained private screenshot implementation out with
`REKORDPOD_PRIVATE_SCREENSHOTS=0`. Simulator capture and real-device photography
are the supported release-media paths; see the checklist in
[`TESTING.md`](../../TESTING.md).

Rekordpod and these utilities are part of this Rockbox fork and are distributed
under GPL-2.0-or-later. Dependency, format-research, Drag'n'Dunk lineage, font,
trademark, and AI-assistance disclosures are recorded in
[`ATTRIBUTION.md`](../../ATTRIBUTION.md).
