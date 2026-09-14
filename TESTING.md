# Rekordpod public-beta test and media guide

This is the canonical release gate for Rekordpod. Test the exact archives that
will be distributed; rebuilding after a pass creates a different candidate and
requires a new identity, checksum, and smoke test.

## Safety rules

Before any write-path test:

- Keep an offline backup of the complete `PIONEER` directory and the Rekordpod
  state listed in [README.md](README.md).
- Confirm the backup can be read and contains `PIONEER/rekordbox/export.pdb`.
- Prefer a reproducible test export or cloned device for destructive and
  interruption tests.
- Never test a new target ZIP, storage adapter, or database shape for the first
  time on the only copy of a performance library.
- Stop after any ATA panic, filesystem warning, audio stutter introduced by a
  write, incomplete-journal error, unexplained reboot, or metadata mismatch.
  Preserve the device and recovery files for diagnosis before reconnecting it
  to software that may rewrite them.

## Release identity

Record these before testing:

| Field | Value |
| --- | --- |
| Release name | |
| Git commit | |
| Branch | |
| iPod target | |
| Storage medium and adapter | |
| Capacity and filesystem | |
| Track / playlist counts | |
| ZIP filename | |
| ZIP SHA-256 | |
| Tester and date | |

The source tree must be clean after the release commit. The repository remote
must point to the intended Rekordpod fork before anything is pushed; the
upstream Rockbox mirror is a source reference, not a Rekordpod publishing
destination.

## Automated gates

All of these must pass on the release commit:

1. Build and package iPod Classic 6G/7G with the default release flags.
2. Confirm `REKORDPOD_PRIVATE_SCREENSHOTS=0` in the build log or generated
   command lines. The public binary must not expose a screenshot hotkey.
3. Run `utils/rekordpod/test_geometry.py`.
4. Run `utils/rekordpod/test_edit_formats.py`.
5. Run Python bytecode compilation on every script under `utils/rekordpod`.
6. Run the complete Rockbox ZIP integrity check for the Classic archive.
7. Run `git diff --check` and confirm there are no tracked caches, generated
   `.pyc` files, device libraries, journals, or screenshots.
8. Record plugin-region use from the Classic linker report. Do not release an
   image that crosses the target's plugin memory budget.
9. Record SHA-256 digests for the binary archive, installers, and source
   archive.

For the private build only, compile once with
`REKORDPOD_PRIVATE_SCREENSHOTS=1` to ensure the retained capture code does not rot.
That build must be labeled private and must not replace a public archive.

## Installation and boot

For a destructive clean-install rehearsal, follow [INSTALL.md](INSTALL.md)
through every geometry and backup checkpoint. The abbreviated steps below are
for an already prepared test volume.

1. Verify the ZIP target and SHA-256 against the release manifest.
2. Overlay the ZIP's `.rockbox` directory; do not delete the existing directory
   first. For this clean namespace release, generate a fresh Rekordpod device
   cache and configure the app anew.
3. On later Rekordpod overlay tests, confirm workflow/config/state files survive
   unchanged unless a documented format migration applies.
4. Eject cleanly and boot with no USB cable attached.
5. Confirm the complete intro animation, `rekordpod` title, screen zoom, and
   clean main menu. Watch for flashes of the default Rockbox background.
6. Reboot with MENU held and confirm the one-time autoboot bypass.
7. Toggle Autoboot off and on in Rekordpod Settings, rebooting after each.
8. Confirm HOLD behaves normally. While locked, confirm the screen and
   visualizer update rate fall and audio continues without interruption.

## Main menu and common controls

- The background is pure black and all text remains readable.
- The miniature CDJ screen, controls, and platter remain within the intended
  bounds with no clipped or overlapping labels.
- A complete physical click-wheel trace produces one visual platter rotation;
  touch position, drag direction, and momentum agree.
- Selection, scrolling, cardinal inputs, SELECT, PLAY, MENU, and HOLD never
  leak into an unrelated control.
- Hold MENU for the configured escape action and confirm one hold produces only
  one escape.
- No context change flashes stale content. Prep Deck page changes are immediate
  and have no transition effect.
- The activity ticker reflects actual work without restarting a transition or
  hiding an error.

## Collection, search, and sorting

Test a library large enough to expose cache and lazy-loading behavior.

1. Scroll slowly, then rapidly, through the Collection. Confirm titles do not
   duplicate, jump, or resolve to the wrong track when prefetched tiles settle.
2. Search title, artist, genre, key, comments, and tags. Open a result and
   verify its waveform, grid, cues, and metadata belong to that track.
3. Sort ascending and descending by title, BPM, year, key, comments, tags, and
   import date. Confirm BPM and key remain paired with the correct track.
4. Verify second-line `[Genre]` text yields to the fixed BPM/key fields instead
   of drawing through them.
5. Load tracks through ordinary Collection, search, and Shuffle Collection.
   Confirm all three paths load identical waveform and analysis data.
6. Let Shuffle Collection reach a track boundary and confirm autoplay follows
   the shuffled order without corrupting ordinary Collection order.

## Playlists and folders

1. Verify five-row carousel alignment after entry, return, rapid wheel motion,
   a dialog, a rename, and a tree refresh. At least three blades must remain
   readable.
2. Browse nested folders and playlists; check playlist/folder/smart glyphs.
3. Create, rename, move, reorder, favorite, and delete a test playlist and a
   test folder. Test backspace and space in the on-device keyboard.
4. Add the current track to an existing playlist and create a new playlist from
   that flow. Confirm the new track is visible immediately in Rekordpod.
5. Test Favorite Playlist 1 and 2. Selecting a tool must be immediate; no
   playlist write or folder scan begins until the user confirms its dialog.
6. Test temporary sorting and shuffle without changing permanent order, then
   perform an explicit permanent reorder and verify it after restart.
7. With audio playing, perform playlist-only changes and confirm playback is
   not interrupted.
8. Reopen every affected playlist in Rekordpod and then in rekordbox/CDJ browse.

## Prep Deck and playback clock

Use tracks around 60, 90, 120, 142, 180, and 250 BPM, including short and long
files, VBR audio, a dense grid, and a track with no cues.

1. Play continuously for 90 minutes while periodically browsing pages, zooming,
   seeking, editing cues, and opening playlists. Record any repeatable cadence
   in a visual pause (especially near 51–60 seconds).
2. Compare audio, main playhead, miniature cursor, beat phase, and bar.beat
   after ordinary playback, pause/resume, seek, scrub, beat jump, track restart,
   and a mid-playback track change. They must rejoin one authoritative Rockbox
   playback clock without drift or catch-up scrolling.
3. Change tracks during playback. Audio must pause and settle before the old
   analysis is replaced, then resume only with the new track ready.
4. Test 1× through 128×. On Classic, 64× and 128× must use detailed resident
   data without storage reads in the render loop. On Video, verify the bounded
   compatibility path stays within memory.
5. Cardinal buttons may continue a deliberate wheel gesture but must never seed
   scrub/seek from incidental click-wheel contact.
6. With Platter Wheel Mode on and off, test touch, drag, momentum, release,
   pause, and free-pitch playback. Ordinary seeking remains silent.
7. Double-click SELECT while a tool is active to enter temporary precision
   SEEK. The seek icon must blink; wheel movement must not alter the previous
   tool. Double-click again to restore the exact prior tool/workflow cell.
   MENU must not accidentally escape this temporary mode.
8. Test the traditional iPod seek orb in both first and last PLAYER positions.
9. Test Cue audition independently and confirm it lasts only for the intended
   hold, with no stuck cue state.

## Grid, cues, loops, metadata, and workflows

### Beat grid

- Verify imported grid origin and bar.beat across at least 128 beats.
- Downbeats are red and remain aligned after seek, tempo change, and reload.
- With Quantize on, cue creation snaps down to the intended beat marker.
- Beat Jump first snaps to the grid, then moves exactly the requested amount.
- Exercise nudge, downbeat, BPM, and joined/flexible-grid cases separately.

### Hot cues

- A newly loaded track resets shared cue focus to Cue 01.
- Slot, Delete, Color, and Move share one cue number focus.
- Existing-cue selection jumps immediately with no confirmation.
- Cue creation uses SELECT with no confirmation and defaults to green.
- Deletion affects the selected cue, not the most recent cue.
- Test creating, moving, recoloring, deleting, and recreating the same slot;
  the journal must reduce redundant operations to the final valid state.
- Test deletion of the only cue as the only track change.
- Cue flags stay readable on the main and miniature waveform and scale with
  zoom.

### Loops and metadata

- Verify Loop In, Loop Out, size, active state, and tinted waveform region.
- Burn rating, no color, every named track color, year (including a blank year
  that starts at 2000), genre including Add Genre, key, and BPM/grid.
- Check Original, flat Chromatic, and Camelot display preferences without
  changing the stored key unless Key Editor is confirmed.
- Confirm `[year] Artist - Title [extension] || comments ||` scrolls and wraps
  without overwriting time or battery status.

### Workflows

- Create two 48-step workflows with repeated tools, rename them, insert,
  replace, reorder, and clear steps, and set fixed or choose-on-execution values.
- Reboot after every mutation type and confirm both workflows persist exactly.
- Upgrade by overlaying the release ZIP and confirm both remain intact.
- SELECT+LEFT/RIGHT arms a workflow step; SELECT executes it. Releasing a
  direction while SELECT remains held permits another chord but never auto-fires.
- Wheel-locked values ignore incidental touch; the wheel falls back to the
  intended seek/scrub behavior without changing the selected value.

## Transaction and rekordbox parity tests

Use a cloned library for this section.

1. Make one metadata change, unload the track, choose Save, and confirm the
   change burns before the next track becomes active.
2. Repeat and choose Discard. Confirm neither PDB nor analysis files change.
3. Test combined metadata, cue, and grid edits on one track.
4. Test cue deletion as the only change and metadata as the only change.
5. Test playlist-only create/add/remove/reorder/delete operations.
6. Reboot immediately after a completed save and verify state.
7. On a disposable clone only, interrupt power during each safe transaction
   phase. Reboot, retain all `.rekordpod-*` recovery files, and verify the original
   library is either intact or recoverable; never accept a partial success.
8. Connect by Data Transfer, eject cleanly, and open the export in rekordbox.
   Verify track fields, cue slots/colors/times, beat grid, and playlist order.
9. Browse and load changed tracks on every claimed CDJ/XDJ model. Record exact
   models and firmware versions. Rekordbox parity alone does not prove hardware
   compatibility.
10. Confirm no change is claimed in OneLibrary / Device Library Plus. That
    format is outside the current write path.

## USB and storage matrix

Run these tests on each claimed storage class: original HDD, SSD, single
microSD adapter, and multi-card adapter.

- Connect a cable while on Main, Collection, Playlists, Prep Deck, Settings,
  Power Only, and while booting. Non-USB pages must not freeze or force a mode.
- Backing out of USB always selects and saves Power Only.
- Data Transfer initializes once, presents the FAT32 partition correctly,
  remains mounted through ordinary host access, and returns to Rekordpod after
  a clean eject.
- Hotplug, first handshake, repeated mount/eject, and boot-with-cable produce no
  ATA `-2`, `-2147483605`, writeback panic, duplicate initialization, or menu reset.
- Test USB DAC separately if enabled. Confirm gain initializes safely, audio is
  audible, UI remains responsive, and exit returns to Rekordpod.
- After every USB write test, run a filesystem check and compare the protected
  library against its expected state.

## Screenshot harvesting checklist

The public release has **no on-device screenshot hotkey**. Do not document Hold
PLAY as a capture gesture. Use simulator framebuffer capture for clean UI
images and photograph the physical iPod for hardw
are proof.

### Capture matrix

- [ ] Intro: record, transformed wheel, `rekordpod`, and screen zoom
- [ ] Main menu with default and custom accent/body/wheel colors
- [ ] Collection, search, each sort, Shuffle Collection
- [ ] Playlist carousel, nested folder, smart playlist, edit dialog
- [ ] Rekordpod Settings at the top and bottom of its vertical list
- [ ] USB Power Only and Data Transfer; DAC only on supported Classic build
- [ ] Every Prep Deck page with the active orb and readable tool hint
- [ ] RGB waveform at 1×, 64×, and 128× with grid, loop, and cue flags
- [ ] 20-band EQ, Phrase Map with cues, Harmonic Constellation
- [ ] Spectral Canyon, Boombox, Stereo Orbit, Oscillo-Turntable
- [ ] Straight and S-shaped arms plus each headshell option
- [ ] Save/Discard, playlist confirmation, keyboard, and a safe staged failure
- [ ] HOLD state and low-battery status bar

### Capture quality

- Use the exact release commit, target ZIP, and default settings for the primary
  set; label customized sets clearly.
- Capture native 320×240 output without smoothing, scaling, filters, or color
  correction. Provide a nearest-neighbor enlarged copy only as a derivative.
- For device photos, control reflections, show the complete iPod in at least one
  frame, and keep the display square to the camera.
- Check pure-black backgrounds, readable selection text, no clipping, no stale
  frame, no tearing, correct cue/grid alignment, and matching track metadata.
- Redact private filenames, comments, playlist names, device serials, and USB
  names unless the owner approved publication.
- Name files
  `<commit>_<target>_<screen>_<state>_<sim-or-device>.<png-or-jpg>`.
- Keep a manifest beside the images with commit, ZIP SHA-256, device/storage,
  capture method, settings changes, photographer, and license/permission.
- Never use a private screenshot-enabled binary as visual evidence for a public
  binary without labeling the difference.

## Stop-ship conditions

Any one of these blocks public release:

- unverified or incomplete source/license attribution;
- dirty source tree or archive/commit/hash mismatch;
- build failure, plugin memory overflow, or failed automated test;
- ATA panic, writeback failure, filesystem corruption, unexplained reboot, or
  persistent journal/recovery error;
- audio stutter caused by browsing, rendering, or burns;
- recurring waveform freeze, visual/audio drift, or wrong-track analysis;
- lost, duplicated, or misaddressed cue/metadata/playlist changes;
- workflow loss across reboot or overlay upgrade;
- unreadable popup selection or a control that triggers an unrelated action;
- an unsupported target or USB mode presented as tested.

## Beta report checklist

Include release commit and ZIP hash, iPod generation, storage/adapter, capacity,
filesystem, track/playlist counts, source rekordbox version, affected track
duration/BPM/format, playback state, waveform zoom, elapsed time, exact visible
error, and the shortest repeatable input sequence. Attach photos/simulator
captures and recovery files only after removing private library information.
