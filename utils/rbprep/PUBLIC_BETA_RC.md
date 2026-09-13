# Rekordpod Public Beta Release Gate

This checklist distinguishes code-complete validation from hardware acceptance.
A package is a release candidate only until every device test below passes on
the exact ZIP being distributed.

## Automated gates

- Build iPod Video 5G/5.5G and iPod Classic 6G/7G from a clean tree.
- Run all RBPrep geometry and edit-format tests.
- Reject overflowing 4096-to-512 USB geometry instead of wrapping it.
- Verify every edit-journal append by reopening and comparing the exact record.
- Verify rewritten PDB metadata after close/reopen before publishing success.
- Verify RBW analysis size, header, cue count (including zero cues), beat count,
  metadata, and each cue record after close/reopen.
- Verify temporary journal rewrites before and after their atomic rename.
- Retain rollback files until the replacement and its completion marker both
  validate.
- Test both archives with ZIP integrity checks and record SHA-256 digests.

Clean release-candidate link measurements on 2026-09-13:

- iPod Video: 474,768 bytes used, 49,520 bytes (48.36 KiB) free in the
  512 KiB plugin region.
- iPod Classic: 466,784 bytes used, 1,630,368 bytes (1,592.16 KiB) free in the
  2 MiB plugin region.

## Required iPod smoke test

1. Overlay-install the target ZIP while preserving `/.rockbox/rbprep/state/`.
2. Boot normally and once with MENU held to verify autoboot bypass.
3. Browse Collection and a five-row playlist carousel. Confirm solid black
   backgrounds, unobscured labels, consistent selection capsules, and that one
   physical click-wheel revolution maps to one rendered CDJ-wheel revolution.
4. Load a track with one cue, delete that cue, choose `SAVE & LOAD`, reopen it,
   and confirm that the cue bank is empty.
5. On separate tracks, burn rating, color/no-color, year, genre, key, BPM/grid,
   and cue add/move/color changes. Reopen each track and confirm parity.
6. Create, rename, move, reorder, favorite, add to, and delete a playlist while
   audio is active. Confirm the browser overlay refreshes immediately.
7. Reboot, reconnect through Data Transfer, and validate every changed track
   and playlist in Rekordbox. Confirm no recovery or pending-journal warning.
8. On iPod Video, confirm USB DAC is absent. On iPod Classic, test Power Only,
   Data Transfer, and USB DAC separately if DAC is compiled and operational.
9. Play continuously for at least 90 minutes while using seek, zoom, cue, macro,
   and playlist navigation. Confirm no audio interruption, frozen waveform, ATA
   error, or accumulating playhead drift.

## Release decision

Do not label the archive Public Beta 1 until the exact packaged binaries pass
the automated gates and the smoke test. A failed write must retain the loaded
track and report its stage; it must never claim that the edit was burned.
