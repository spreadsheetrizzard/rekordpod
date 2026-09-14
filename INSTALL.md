# Clean installation and release rehearsal

This guide reproduces the experience of installing Rekordpod immediately after
a clean Rockbox installation. It is written for the primary iPod Classic 6G/7G
target on macOS, including high-capacity flash conversions.

The supported rehearsal is a **file-level clean install**. It keeps the tested
FAT32 filesystem, partition map, firmware area, and Rockbox bootloader intact.
It retires the active `.rockbox` directory, installs a stock Rockbox directory,
then applies Rekordpod exactly as a new user would.

Do not reformat the volume through Rekordpod's translated USB connection. The
host sees an in-memory 512-byte MBR/BPB projection while the iPod retains its
4096-byte virtual-sector geometry. Ordinary file I/O is supported, but a host
formatter can choose FAT32 fields that cannot be translated back losslessly.
Formatting is therefore outside the public-beta installation path.

> **Destructive-operation warning**
>
> Do not use Finder/iTunes Restore, `diskutil eraseDisk`, `diskutil
> eraseVolume`, Disk Utility's Erase button, or a whole `/dev/diskN` device for
> this rehearsal. Continue only after making and verifying an independent
> backup. Never trust a remembered disk number; macOS can change it after every
> reconnect.

## Choose the correct kind of clean install

Use the **file-level rehearsal** below when the iPod already has a working
Rockbox bootloader. This is the normal Rekordpod release test.

Do not perform a full Apple/Finder restore merely to test Rekordpod. Rockbox's
iPod Classic documentation warns that the Apple firmware on early Classic
models MB029, MB145, MB147, and MB150 is limited to 128 GiB; booting it with a
larger replacement drive can cause extensive corruption. Later MB562, MB565,
MC293, and MC297 models do not have that particular limit, but a factory restore
still changes more state than this installation test requires.

A true bootloader-from-zero exercise is a separate recovery test. Follow the
[official iPod Classic manual](https://download.rockbox.org/daily/manual/rockbox-ipod6g.pdf)
and its macOS `mks5lboot` procedure. Do not substitute the file-level rehearsal
below for a bootloader recovery. The corresponding instructions are also in
[`manual/getting_started/ipod6g_install.tex`](manual/getting_started/ipod6g_install.tex).

## Required files and equipment

- The iPod charged above 50 percent and connected directly with a reliable USB
  cable, not through an unpowered hub.
- A separate backup location for the active `.rockbox` tree and rekordbox
  database. A full music backup remains strongly recommended but is not copied
  or rewritten by this install-only rehearsal.
- The exact Rekordpod release files and hashes from the release manifest.
- A stock Rockbox iPod Classic build obtained through Rockbox Utility or the
  official Rockbox download site.
- `Build Full Rekordpod Waveform Cache.command` and
  `Install Rekordpod on RIZZPOD.command` beside the Rekordpod release ZIP.
- A terminal window kept open for recording the before/after disk geometry.

## Stage 1: back up and prove recovery

1. In Rekordpod, select **USB → Data Transfer** and wait for `RIZZPOD` to mount.
2. Copy the current `.rockbox` directory and
   `PIONEER/rekordbox/export.pdb` to a dated folder on the Mac or another drive.
   The 133 GB `Contents` tree remains in place and is not touched by the cache
   builder or installer.
3. Do not use the current `.rockbox/rekordpod` directory as the new installation
   seed. Keep it only as rollback evidence; this rehearsal intentionally starts
   with a new Rekordpod state namespace.
4. Confirm the backup contains `.rockbox/rockbox.ipod` and
   `PIONEER/rekordbox/export.pdb`. Confirm the live iPod still contains the
   `PIONEER/USBANLZ` tree.
5. Record the size and SHA-256 of the original and backed-up `export.pdb`:

   ```sh
   shasum -a 256 /Volumes/RIZZPOD/PIONEER/rekordbox/export.pdb
   shasum -a 256 "/Volumes/BACKUP/Rekordpod clean install/PIONEER/rekordbox/export.pdb"
   ```

   Substitute the real backup path. The two values must match.
6. Compare the small `.rockbox` backup with a checksum dry run. This reads both
   trees but changes neither:

   ```sh
   rsync -aEcn --itemize-changes "/Volumes/RIZZPOD/.rockbox/" \
       "/Volumes/BACKUP/Rekordpod clean install/.rockbox/"
   ```

   Investigate every Rockbox-file difference.

Before Stage 8 performs a rekordbox write, separately back up the complete
`PIONEER` tree or, at minimum, every ANLZ file belonging to the test tracks.
Installation and cache creation are read-only with respect to `PIONEER`; the
write-parity stage is not.

**Checkpoint A:** do not retire the active `.rockbox` tree until the backup
opens correctly and the database hashes match.

## Stage 2: identify and record the exact disk

Run these read-only commands after every reconnect:

```sh
diskutil list external physical
diskutil info /Volumes/RIZZPOD
sudo fdisk /dev/diskN
```

Replace `diskN` only after matching all of the following:

- external, physical device;
- approximately the expected capacity;
- an FDisk/MBR partition map;
- one FAT32 data partition named `RIZZPOD`;
- the expected data-partition offset and size.

Record the whole-disk identifier, partition identifier, device block size,
partition start sector, partition offset, and partition size. On the original
1 TB RIZZPOD test configuration, the translated host view used a 512-byte
device block and the FAT32 partition began at sector 394224, or 201,842,688
bytes. Those are comparison values for that device, not identifiers to paste
blindly.

**Checkpoint B:** if the disk layout differs from the recorded pre-test layout,
stop. Do not attempt to repair it by repartitioning.

## Stage 3: retire the active Rockbox tree

Keep the iPod mounted in Rekordpod Data Transfer mode. Rename the active
`.rockbox` directory on the same volume instead of deleting it:

```sh
mv /Volumes/RIZZPOD/.rockbox \
   /Volumes/RIZZPOD/.rockbox-pre-rekordpod-test
```

This directory is inactive but remains available for immediate recovery. The
independent backup from Stage 1 is still the authoritative copy.

Confirm that `PIONEER`, music, playlists, and the data-volume geometry remain
unchanged, and that `/Volumes/RIZZPOD/.rockbox` no longer exists. Do not reboot
in this intermediate state: the bootloader has no active Rockbox firmware to
load until Stage 4 completes.

## Stage 4: create the stock Rockbox baseline

Install only the normal Rockbox files:

- In Rockbox Utility, choose the correct iPod Classic mount point and install
  Rockbox without reinstalling or removing the bootloader; or
- Extract the official iPod Classic Rockbox ZIP directly at
  `/Volumes/RIZZPOD` so it creates `/Volumes/RIZZPOD/.rockbox`.

Verify the baseline:

```sh
test -f /Volumes/RIZZPOD/.rockbox/rockbox.ipod
test ! -e /Volumes/RIZZPOD/.rockbox/rocks/apps/rekordpod.rock
test ! -e /Volumes/RIZZPOD/.rockbox/rekordpod
test -d /Volumes/RIZZPOD/.rockbox-pre-rekordpod-test
```

All four commands must exit successfully. Save a directory listing and the
stock `.rockbox/rockbox-info.txt` as evidence.

For an installer-overlay rehearsal, it is sufficient to verify this stock
filesystem baseline without booting it. This avoids depending on the stock USB
presentation of a nonstandard 1 TB storage conversion. A separate stock-boot
test is optional: perform it only if a tested path back to a writable mounted
volume is already available, because stock USB lacks Rekordpod's translated
geometry. Otherwise continue without ejecting or rebooting.

## Stage 5: build a fresh Rekordpod cache

Because `PIONEER` and the music stayed in place, no terabyte-scale restore is
needed. Run `Build Full Rekordpod Waveform Cache.command` from the release
folder. It:

1. reads `export.pdb` and `USBANLZ` from the mounted iPod;
2. displays terminal progress for tracks, playlists, analysis, and packaging;
3. creates a fresh compact Rekordpod index and per-track waveform files;
4. packages them with the exact iPod Classic release ZIP as
   `rekordpod-rizzpod-full.zip`;
5. leaves the mounted iPod unchanged.

The resulting full ZIP contains private library metadata. Do not publish or
attach it to a public issue. Keep the generic release ZIP and source ZIP as the
public artifacts.

**Checkpoint C:** the builder must reach 100 percent with no parser warning for
a track used in the smoke test, and `unzip -t rekordpod-rizzpod-full.zip` must
report no errors.

## Stage 6: run the one-click installer

Run `Install Rekordpod on RIZZPOD.command`. The installer:

- refuses to run unless `RIZZPOD` is mounted and writable;
- requires the freshly generated `rekordpod-rizzpod-full.zip`;
- asks for confirmation before copying;
- overlays Rockbox, `rekordpod.rock`, theme files, and the new cache;
- writes the device name under `/.rockbox/rekordpod`;
- flushes pending writes and ejects the iPod.

It does not format the disk, replace the partition table, or import the retired
pre-release state.

After installation, do not unplug until macOS reports that the volume was
ejected.

## Stage 7: first boot acceptance test

Boot with no USB cable attached and record each result:

- [ ] Rockbox finds and launches Rekordpod automatically.
- [ ] The complete intro runs once without a stale Rockbox background.
- [ ] Collection and Playlists report the expected counts.
- [ ] A known track loads with the correct title, artist, BPM, key, genre,
      waveform, beat grid, and cues.
- [ ] Playback remains smooth beyond 90 seconds.
- [ ] The main waveform, miniature playhead, bar.beat, and beat phase remain
      locked after seek, pause/resume, and a track change.
- [ ] Rekordpod Settings show Power Only as the default USB behavior.
- [ ] Autoboot off/on and the boot-time MENU bypass both work.
- [ ] Rebooting retains newly created Rekordpod settings and workflows.

## Stage 8: minimal write-parity test

Use backed-up test tracks, not an irreplaceable performance set.

1. Change one harmless metadata field on one track and unload it.
2. Confirm Save; reboot; reopen the track and verify the value.
3. Create one green cue, move it, save, reboot, and verify it.
4. Create one test playlist and add one track; confirm it updates immediately.
5. Enter Data Transfer deliberately, eject cleanly, and open the device in
   rekordbox.
6. Verify the metadata, cue time/color, and playlist membership.
7. Preserve before/after copies and hashes of `export.pdb` and affected ANLZ
   files.

Complete the broader matrix in [TESTING.md](TESTING.md) before calling the
archive publicly validated.

## Cleanup after a successful rehearsal

Keep `.rockbox-pre-rekordpod-test` through the complete write-parity and USB
tests. Once the exact release has passed and another verified independent
backup exists, it can be removed during a deliberate maintenance session. Its
presence does not affect the active `.rockbox` tree.

## Stop and recover

Stop immediately after an ATA error, writeback panic, incomplete-journal
message, filesystem warning, unexpected reboot, audio stutter caused by a
write, wrong-track waveform, or database mismatch. Do not repeat the failing
write and do not let rekordbox automatically repair or replace the export.

Preserve the complete `PIONEER` directory, active `.rockbox` tree, every
`.rekordpod-*` recovery file, the exact installed ZIP/hash, and the disk-layout
record. Restore only from the verified offline backup after the failure has
been documented.

If the active tree fails before installation completes and the volume is still
mounted, restore the retired tree without formatting:

```sh
mv /Volumes/RIZZPOD/.rockbox \
   /Volumes/RIZZPOD/.rockbox-failed-test
mv /Volumes/RIZZPOD/.rockbox-pre-rekordpod-test \
   /Volumes/RIZZPOD/.rockbox
```

Preserve the failed tree for diagnosis. If the data volume no longer mounts or
the geometry changed, do not run another formatter; stop and follow the
official iPod Classic recovery documentation.

## Unsupported true-format experiment

Rekordpod USB synthesizes a one-partition, 512-byte host MBR and patches the
FAT32 BPB in RAM. On writes, it accepts only the presented FAT partition and
converts compatible BPB fields back to 4096-byte units. A newly created host
filesystem is compatible only if its cluster size, reserved sectors, total
sectors, FAT size, FSInfo sector, and backup-boot-sector values are all exactly
representable through that multiplier.

The macOS formatter is not guaranteed to choose such a layout. A true format
must first be developed and validated against a disposable byte-for-byte clone,
with explicit BPB parameters and recovery media. It is not part of the public
beta dry run.
