# Rekordpod Stability and Performance Refactor

Status: public-beta release-candidate implementation, 2026-09-13

Public Beta 1 deliberately refines the original memory rule below: the iPod
Video keeps a bounded raw window plus a resident coarse peak pyramid, while
the iPod Classic uses its larger plugin region to preload the complete RBW3
waveform and a fine resident peak pyramid before playback resumes.  Neither
target reads analysis data from storage while the deck is running.  This is
the measured compromise that preserves the detailed Classic 64x/128x view
without reintroducing the periodic storage stalls that prompted the refactor.

## Purpose

Rebuild Rekordpod as a bounded, recoverable DJ-preparation application for
Rockbox. The iPod Video 5G/5.5G is the minimum hardware target. The iPod
Classic 6G/7G uses the same source but receives a larger runtime cache and a
higher rendering budget.

The refactor must preserve the existing navigation and preparation-deck input
layout. It must not require PC-side conversion, re-analysis, or a new desktop
cache-building step.

## Non-negotiable behavior

- Playback and input handling always have priority over drawing and storage.
- Rockbox's reported audio position is the only authoritative playback clock.
- A complete waveform is never loaded on the iPod Video; the iPod Classic may
  preload it at a visible, recoverable track-load boundary.
- Existing RBW3 analysis remains the source format and works without changes.
- Any acceleration data is generated on the iPod from RBW3 in bounded chunks.
- Track experiments remain in RAM until the user leaves the track.
- Leaving a changed track presents `SAVE & LOAD`, `DISCARD & LOAD`, and `STAY`.
- `SAVE & LOAD` is highlighted by default and burns before the new track loads.
- Macros are rewritten as a new format rather than patched again.
- Two workflows are retained, each with 48 ordered and repeatable steps.
- The iPod Video exposes Power Only and Data Transfer USB modes, not USB DAC.
- USB DAC is exposed only on builds where the target and USB stack support it.
- Hold switch, wheel, Select, Menu, Play/Pause, seek buttons, page navigation,
  chord consumption, and arm-then-execute workflow behavior are preserved.
- No device installation occurs automatically during development.

## Why the present structure cannot be the 5G baseline

The current plugin is approximately 10,000 lines in one source file. Its
`waveform[131072][4]` allocation alone is 512 KiB. The iPod Video reserves only
512 KiB for the entire plugin, including code, static data, stacks, waveform,
beats, search results, pending edits, and visualizer PCM.

The current full-waveform allocation, full beat array, 512-entry pending
arrays, and 8,192-entry search result array must therefore be replaced with
bounded caches or streamed views. The iPod Classic's 2 MiB plugin area must not
remain a hidden requirement.

## Target and capability detection

Initialization creates one immutable capability record for the session:

```c
struct rbprep_caps {
    enum rbprep_device_class device_class;
    size_t free_plugin_bytes;
    size_t waveform_cache_bytes;
    size_t io_slice_bytes;
    int deck_fps;
    int visualizer_fps;
    bool usb_audio;
};
```

Selection uses two sources:

1. Compile-time Rockbox target information (`CONFIG_CPU`, `IPOD_6G`, USB
   features, and the configured plugin buffer).
2. The actual remaining plugin workspace returned at startup by
   `plugin_get_buffer()`.

Rockbox treats later iPod Classics through the shared `ipod6g` target. Exact
marketing-generation identification is not needed for performance selection;
the S5L8702/Classic target enters the enhanced profile. Cache sizes are still
clamped to the actual free workspace so a feature addition cannot silently
overrun the plugin region.

### Conservative profile: iPod Video 5G/5.5G

- PP5022 performance ceiling respected.
- Linked plugin image and static working state kept below 464 KiB, leaving at
  least 48 KiB inside the 512 KiB plugin region. Exact sizes are recorded for
  every release build rather than inferred from source allocations.
- Approximately 24-32 KiB waveform page cache.
- 4 KiB cooperative I/O slices.
- Deck rendering targets 18-20 fps and may shed optional work before dropping
  below 12 fps.
- Visualizers use a smaller PCM window and lower update rate.
- USB options: Power Only and Data Transfer.

### Enhanced profile: iPod Classic 6G/7G

- S5L8702 performance path.
- Up to 512 KiB resident raw waveform cache, clamped to actual free plugin
  workspace, plus a fine multi-resolution peak pyramid.
- 8 KiB cooperative I/O slices during the visible pre-playback load only.
- Deck rendering targets 30 fps.
- More adjacent waveform tiles and visualizer history remain resident.
- USB DAC appears only when USB audio is compiled and operational.

The enhanced profile changes smoothness and read-ahead, not file formats or
editing semantics.

## Source decomposition

`rbprep.c` becomes a small coordinator. Proposed modules:

- `rbprep_app.[ch]`: lifecycle, high-level mode changes, and scheduler.
- `rbprep_caps.[ch]`: target detection, memory budgets, and feature gates.
- `rbprep_input.[ch]`: button edges, wheel events, chords, and modal routing.
- `rbprep_transport.[ch]`: playback, seek, cue audition, loops, and timecode.
- `rbprep_track.[ch]`: loaded-track baseline and editable working state.
- `rbprep_wave.[ch]`: RBW3 access, page cache, overview, and acceleration index.
- `rbprep_grid.[ch]`: beat lookup, grid projection, quantize, and bar.beat.
- `rbprep_library.[ch]`: streamed collection, search, sorting, and visible rows.
- `rbprep_playlist.[ch]`: tree, favorites, creation, movement, and membership.
- `rbprep_macro.[ch]`: workflow model, editor, execution, and RBM5 persistence.
- `rbprep_store.[ch]`: checksummed generations and transaction primitives.
- `rbprep_burn.[ch]`: bounded PDB and Rekordpod-index transactions.
- `rbprep_render.[ch]`: dirty regions, shared drawing primitives, and HUD.
- `rbprep_views_*.c`: main menu, browsers, settings, deck, and editors.
- `rbprep_visualizers.[ch]`: RGB waveform, boombox, EQ, Oscillo-Turntable.

Module boundaries must not introduce heap allocation during a frame or place
storage calls inside drawing functions.

## Cooperative scheduler

Each main-loop iteration services work in this order:

1. USB/system events and hold switch.
2. Audio state and authoritative elapsed position.
3. Button/wheel edge collection and state-machine dispatch.
4. Due waveform/playhead dirty-region drawing.
5. Due optional visualizer work.
6. One bounded waveform-cache or storage-maintenance slice.
7. Idle/menu animation work.

No loop may scan a whole song, journal, library, playlist, or PDB table. Long
jobs expose a resumable cursor and return after their byte or time budget.

Instrumentation records maximum loop delay, late frames, I/O duration, cache
hits, cache misses, dropped optional frames, and storage recovery events. A
developer diagnostics page exposes these without changing the normal UI.

## Waveform streaming and on-device acceleration

### Source compatibility

RBW3 files remain untouched and authoritative. Header, cue, and beat offsets
are validated before use. The plugin keeps an RBW3 descriptor and file offset,
not a full sample array.

### Direct page cache

- Raw waveform data is read in aligned pages into a small LRU/ring cache.
- The visible window is requested first, followed by one page in the current
  playback or scrubbing direction.
- A seek changes priority immediately; stale prefetch is abandoned.
- HDD reads are grouped and sequential wherever possible.
- Flash media receive the same low-write, aligned access pattern.

### On-device RBX1 sidecar

The iPod may build `/.rockbox/rekordpod/wave-index/<track-id>.rbx` from RBW3. This
is an acceleration cache, never the sole copy of analysis.

- It is built by streaming RBW3 through a 4-16 KiB buffer.
- The waveform is never resident in full on the iPod Video. The Classic may
  keep it fully resident; both targets render active playback from RAM only.
- Each raw tile produces peak/color summaries for blocks of 16, 64, 256, and
  1,024 source points.
- A 320-column full-track overview is produced in the same pass.
- Tile-grouped output permits one-pass construction and bounded viewport reads.
- Typical sidecar size is about 45 KiB for a maximum-size 512 KiB waveform.
- Header fields bind the sidecar to the track ID, RBW3 header, point count,
  source length, format version, and payload CRC.

The builder writes a temporary sidecar, closes it, reopens and validates it,
then publishes it. An incomplete temporary file is disposable and can never
replace a valid sidecar.

When no sidecar exists, the deck starts from direct RBW3 pages. The miniature
overview fills progressively as the cooperative scan advances. Playback and
input can pause the builder at any chunk boundary. An optional on-device
`Optimize Waveforms` task can warm the whole library while connected to power;
it is never a prerequisite for using a track.

The conservative and enhanced device profiles use the same RBX1 layout. The
Classic simply retains more tiles and processes larger slices.

## Rendering model

- Use Rockbox's existing framebuffer; do not allocate a second 320x240 RGB565
  screen buffer.
- Redraw static HUD and menu regions only when their state changes.
- Update the playhead and waveform with bounded dirty rectangles.
- Cache projected screen columns, cue geometry, and beat-line positions.
- Use fixed-point projection and lookup tables in frame-time code.
- The miniature waveform updates at its own slower cadence.
- Main-menu animation runs during navigation/transition, then settles.
- When locked, visualizers stop and dynamic frame rate falls to zero except for
  required system-state changes.

Portable C, fixed-point arithmetic, and better algorithms are the first
optimization pass. Target-specific ARM assembly is permitted only for a small,
profiled kernel with a measurable gain and a tested C fallback. Persistence,
parsing, and state machines remain portable C.

## Playback and synchronization

- `audio_current_track()->elapsed` is sampled as the authoritative position.
- A short interpolation is allowed only between valid audio samples and is
  clamped back to the next authoritative sample.
- Seek, scrub, cue audition, loop, waveform, overview cursor, beat phase, and
  bar.beat all consume the same position snapshot for a frame.
- Input never directly advances a separate visual clock.
- Non-seek tools cannot emit seek commands.
- Scrub preview work is cancellable and cannot block the audio event path.

## Track edit lifecycle

Loading a track creates:

- An immutable baseline decoded from Rekordbox/RBW/index data.
- A mutable session copy used by grid, cues, metadata, color, genre, loops,
  rating, and related tools.
- A compact dirty-field mask.

Individual gestures do not write storage. When a request would leave a dirty
track, a three-way dialog appears:

- `SAVE & LOAD`: commit the current session, verify it, and load the requested
  track only after success.
- `DISCARD & LOAD`: restore the baseline and load the requested track.
- `STAY`: cancel the requested navigation.

A failed save leaves the current track and edits loaded. It never silently
loads the next track or claims permanence.

## Storage-independent persistence

The default policy assumes the weakest useful combination of FAT32 semantics,
write latency, and interruption behavior. It therefore works for HDD, SSD,
single microSD, and multi-card adapters without risky media detection.

Small persistent stores use two fixed generation files rather than a chain of
canonical, temporary, previous, and root-level mirrors. Each generation has:

- Magic and schema version.
- Header and payload lengths.
- Monotonic generation number.
- Payload CRC32.
- Store-specific record count and validation bounds.

To save, Rekordpod writes the older/inactive generation, closes it, reopens it,
validates every field and CRC, and only then reports success. Loading chooses
the highest valid generation. No separate pointer file is required, and a
power loss cannot invalidate the older generation.

Writes are sequential and infrequent. Wheel movement and cue experimentation
never cause repeated sector updates. State files live under
`/.rockbox/rekordpod/state/`; installers preserve that directory.

## PDB and Rekordpod-index transaction

Track and playlist burns use a recoverable transaction ID and explicit phases:

1. Validated intent recorded.
2. Required PDB pages identified and bounds checked.
3. Before-image or page journal written and verified.
4. PDB page changes applied and reread for validation.
5. Rekordpod index overlay/update applied.
6. Transaction marked complete.

Recovery is idempotent: boot either finishes an applicable transaction or
restores the recorded page image. It never guesses table boundaries. PDB work
uses bounded page buffers and does not borrow the entire audio buffer.

Track burns occur at the confirmed next-load boundary. Playlist UI changes may
appear immediately through an in-memory overlay, while their durable intent is
journaled and the PDB/index transaction runs at the same safe boundary. This
keeps playlist creation useful during playback without performing a large PDB
operation in the frame loop.

## RBM5 workflow system

RBM5 replaces the existing macro persistence implementation.

- Two named workflows.
- 48 ordered slots per workflow.
- Repeated tools are explicitly allowed.
- Each step stores a stable 16-bit tool ID, typed value, execution flags, wheel
  lock policy, and choose-on-execution state.
- Tool IDs are independent of UI enum order and never silently remap.
- Unknown IDs are preserved as disabled cells for forward compatibility.
- Names, ordering, active workflow, active position, and every edit operation
  are part of the checksummed payload.
- Insert, replace, delete, reorder, rename, clear, and default-value changes
  commit a new generation after the user confirms the operation.
- The UI shows success only after reopen-and-verify succeeds.
- The old RBM1-RBM4 stores are import-only. A valid import is written to RBM5
  and verified before the legacy files are left alone as recovery evidence.

Execution remains select-to-arm and select-again-to-execute. Chord releases
are fully consumed before Select can execute a workflow cell. Left/right macro
navigation has no key repeat. Up/down swaps between the two workflows.

## Playlist seeds and favorites

`Add to Playlist` places `NEW PLAYLIST...` at the top of the destination list.
Choosing it opens the existing keyboard, creates the playlist under the
current folder context (root when no folder context exists), and adds the
loaded track as one transaction. Failure cannot leave a half-created playlist.

Playlist and folder hold actions add:

- `SET AS FAVORITE 1`
- `SET AS FAVORITE 2`
- `CLEAR FAVORITE`

Favorites persist by stable Rekordbox source ID, not row position. The two
favorites appear as pinned, clearly marked entries in playlist view. A deleted
or unavailable target becomes an empty favorite without redirecting to a
different node.

## Visualizers and main menu

### Oscillo-Turntable

- User-facing name is `Oscillo-Turntable`.
- The audio oscilloscope remains horizontal and unrotated so it can be read.
- Platter dots, RPM lamp, spindle, and tonearm may animate independently.
- Tonearm position follows normalized track duration.
- Higher Classic budgets improve history depth and update rate, not semantics.

### Boombox steelpan field

- Bass pitch and resonance dots are arranged as labeled steelpan-like tone
  fields rather than a linear EQ row.
- Radial placement conveys octave/register; note labels convey pitch class.
- Brightness and area convey energy; restrained motion conveys bass impact.
- The 20-120 Hz emphasis and rattle threshold remain musically meaningful.
- Centered explanatory text stays clear of speaker and note geometry.

### Main menu

- Preserve current menu order, wheel navigation, Select behavior, and Menu
  behavior.
- Use overlapping curved blades, circular record forms, and strong depth cues
  inspired by the Xbox 360 blades dashboard.
- Apply a Teenage Engineering-like lens through compact labels, deliberate
  white space, simple glyphs, and precise state indicators.
- Spend animation budget during transitions and selection changes; avoid a
  continuous full-screen idle animation.

## Compatibility retained during the refactor

- Current library and playlist browsing behavior.
- Search and sorting, including BPM/key/genre/import date presentation.
- Cue flags, cue colors, cue snapping, grid editing, quantize, and bar.beat.
- Existing tool pages and five-orb maximum (only PLAYER and DETAIL use five).
- Playlist autoplay, collection shuffle, and playlist refresh behavior.
- Power-only USB unless Data or supported DAC is armed from the USB page.
- Return to Rekordpod after USB disconnect.
- No HID by default.
- Screen-lock battery behavior.
- Existing theme colors, keyboards, settings, boot animation, and status bar
  unless changed by a separately approved UI refinement.

## Implementation sequence

1. **Checkpoint and measurement**
   - Preserve the current working 6G source and build artifacts.
   - Record plugin section sizes and frame/I/O baselines.
   - Add host-side parsers and fault-injection fixtures for existing files.
2. **Mechanical module split**
   - Move code without behavior changes.
   - Build and smoke-test after each module boundary.
3. **Capability and memory layer**
   - Remove large static arrays.
   - Establish conservative and enhanced profiles.
   - Produce both iPod Video and iPod Classic builds.
4. **Waveform and beat streaming**
   - Add RBW3 page access, bounded beat windows, RBX1 generation, and cache.
   - Replace full waveform and full beat allocations.
5. **Scheduler and renderer**
   - Centralize timing, dirty rectangles, and cooperative work quotas.
   - Validate that playback/input preempt cache and visualizer work.
6. **Persistence primitives**
   - Implement dual generations, CRC validation, and recovery tests.
7. **RBM5 workflows**
   - Implement model, editor, legacy import, persistence, and 48-step UI.
8. **Track/PDB transaction path**
   - Implement RAM experiments and the Save/Discard/Stay load boundary.
   - Harden page writes and Rekordpod-index parity.
9. **Playlist seeds and favorites**
   - Add transactional create-and-add and two stable favorite slots.
10. **UI and visualizer refinement**
    - Rebuild the main menu presentation.
    - Update Boombox and Oscillo-Turntable without changing input layout.
11. **Hardening and release builds**
    - Run storage-failure, interruption, long-playback, USB, and reboot tests.
    - Package separate 5G and 6G/7G archives and installers.

## Verification matrix

- Both target builds link within their plugin limits.
- Zero full-waveform allocation on the iPod Video and zero whole-library
  pending arrays; the Classic's optional full preload is bounded by its
  runtime plugin buffer.
- RBW3 tracks at minimum, median, and maximum point counts.
- Missing, truncated, stale, interrupted, and valid RBX1 sidecars.
- Waveform playback, prolonged playback, rapid seek, scrub, loop, and cue use.
- Every macro tool and value type at 0, 1, 47, and 48 steps.
- Repeated workflow tools, rename/reorder/clear, reboot, USB, and power-loss
  interruption at each generation-write phase.
- Track Save, Discard, Stay, failed burn, and recovery on next boot.
- Playlist create-and-seed, rename, move, delete, favorites, and smart lists.
- HDD-like delayed reads and flash-like short-write/error injection.
- USB insertion on every page, Data exit, supported DAC exit, and lock state.
- No visualizer or waveform work while the display is locked.

## Acceptance criteria

- The iPod Video build leaves at least 48 KiB free in its documented 512 KiB
  plugin region and never depends on the Classic's larger plugin region.
- Normal input is serviced within one 50 ms tick budget unless a confirmed
  track-boundary transaction is visibly in progress.
- Waveform/playhead position is derived from the same audio snapshot and does
  not accumulate drift during an hour-long playback test.
- No storage or visualization task contains an unbounded main-loop scan.
- A valid older state generation always survives an interrupted save.
- A completed macro edit survives immediate reboot and 100 repeated save/load
  cycles in the fault-injection harness.
- Failed PDB/index transactions remain recoverable and never report success.
- 6G/7G devices demonstrate measurably higher cache hit rate and frame cadence
  without producing incompatible device data.
