# Rekordpod public beta 1

Rekordpod public beta 1 turns an iPod Classic 6G/7G into a smart DJ drive built
on Rockbox. It combines portable playback and library browsing with on-device
preparation tools for an existing traditional Rekordbox Device Library.

## Public release assets

The GitHub release is expected to contain:

- `Rekordpod-Installer-macOS.zip` — the macOS desktop installer;
- `Rekordpod-Installer-Windows.zip` — the Windows desktop installer;
- `rekordpod-public-beta-1-ipod6g.zip` — the expert/manual iPod Classic
  overlay; and
- a source archive generated from the exact release tag.

Most users should choose the installer for their computer. The installer
validates the selected iPod, builds its library cache, preserves existing
Rekordpod settings during upgrades, and applies the matched firmware and plugin
set. Copying `rekordpod.rock` by itself is not supported.

## Highlights

- Collection search and sorting, playlist/folder browsing, favorites, shuffle,
  and ordered playlist work.
- A click-wheel Prep Deck with seek, scrub, zoom, gain, grid, cue, loop,
  metadata, key, tempo/RPM, pitch-lock, quantize, and workflow-pad tools.
- Detailed RGB waveforms plus 20-band EQ, phrase map, harmonic constellation,
  spectral canyon, boombox, stereo orbit, and Oscillo-Turntable views.
- Confirmed on-device writes to supported traditional Device Library and ANLZ
  structures, with temporary files, rollback copies, and recovery state.
- Power-only USB by default, deliberately armed Data Transfer, and an
  experimental iPod Classic USB DAC path.
- Rekordpod autoboot, matching Rockbox theme, and persistent user settings.

## Requirements

- iPod Classic 6G/7G with a working Rockbox installation and bootloader;
- a Rekordbox-analyzed traditional Device Library already exported to the iPod;
- Windows or macOS for initial provisioning;
- an independent backup of `.rockbox` and the complete `PIONEER` directory; and
- enough time to complete the first-boot and write-parity checks in
  [TESTING.md](TESTING.md).

## Important limitations

- Only the iPod Classic 6G/7G build is distributed or supported.
- Rekordpod does not analyze audio on the iPod.
- Rekordpod does not read or write OneLibrary / Device Library Plus. The iPod
  can carry a separate OneLibrary export, but the two databases can diverge.
- USB DAC is experimental and is not a release-readiness dependency.
- Storage adapters, capacities, original disks, CDJ models, and CDJ firmware
  must be tested as distinct configurations.
- The public binary compiles the framebuffer screenshot shortcut out.

## Safety

This beta can rewrite a DJ library. Do not test write paths against the only
copy of a performance collection. Stop after an ATA error, writeback panic,
incomplete-journal message, unexpected reboot, filesystem warning, or database
mismatch. Preserve the affected files and report the exact commit, archive
SHA-256, hardware, and reproduction steps.

See [INSTALL.md](INSTALL.md) before installation and [TESTING.md](TESTING.md)
before describing an archive as validated.
