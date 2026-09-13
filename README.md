# Rekordpod

Rekordpod is an experimental DJ-library preparation environment for click-wheel
iPods, built as a substantial fork of [Rockbox](https://www.rockbox.org/). It
loads rekordbox Device Library metadata and analysis already exported to the
iPod, presents a Pioneer-inspired preparation deck, and can write supported
track, cue, beat-grid, and playlist changes back to the traditional
`PIONEER/rekordbox/export.pdb` and ANLZ files on the device.

> **Public beta warning**
>
> Rekordpod can rewrite a DJ library. Keep a tested backup of the entire
> `PIONEER` directory and use a disposable or reproducible export until the
> exact build has passed the release gate in [TESTING.md](TESTING.md). A
> successful compile is not evidence that database writes are safe on every
> library, storage adapter, or player.

## Current targets

| Target | Status | Notes |
| --- | --- | --- |
| iPod Classic 6G/7G | Primary public-beta target | Larger waveform/index caches; optional USB audio path remains experimental |
| iPod Video 5G/5.5G | Compatibility preview | Smaller memory budget and waveform cache; USB DAC is intentionally unavailable |
| Other Rockbox devices | Unsupported | The plugin currently requires the iPod click wheel and 320×240 color UI |

Flash-storage conversions, SSDs, microSD adapters/arrays, and original hard
drives all exercise different timing and write behavior. They are separate test
configurations, not interchangeable proof of reliability.

## What is included

- Rekordbox-style collection search, precomputed sorting, playlist and folder
  browsing, favorites, shuffle, and ordered playlist work.
- A playback-first Prep Deck with seek/scrub/zoom/gain, hot cues, beat-grid,
  metadata, key, tempo/RPM, loop, playlist, quantize, and two persistent
  workflow pads.
- RGB waveform and seven audio/analysis views: 20-band EQ, phrase map,
  harmonic constellation, spectral canyon, boombox, stereo orbit, and
  Oscillo-Turntable.
- Transactional on-device editing with verified temporary files, retained
  rollback copies, and append-only edit state.
- A 512-byte USB Mass Storage presentation for the iPod Classic while the
  device keeps its internal 4096-byte virtual-sector handling.
- Power-only USB by default, with Data Transfer and supported USB DAC modes
  armed deliberately inside Rekordpod.
- A matching grey Rockbox theme and an optional Rekordpod autoboot path.

The public build compiles the private framebuffer screenshot shortcut **off**.
Release screenshots must come from simulator captures or real-device
photography; see the harvesting checklist in [TESTING.md](TESTING.md).

## Data model and limits

Rekordpod works with the traditional rekordbox Device Library: `export.pdb`
plus DAT/EXT analysis files. It does not yet update OneLibrary / Device Library
Plus, so players configured to read only that newer database may not see local
edits. It does not analyze audio on the iPod; tracks are expected to have been
analyzed by rekordbox before export.

The desktop utilities in [`utils/rekordpod`](utils/rekordpod) convert an existing
Device Library and ANLZ corpus into the compact index and waveform cache used by
the plugin. They are provisioning, recovery, and diagnostic tools. Normal
supported edits are made on-device once that cache exists.

## Install and preserve user state

Use only the ZIP for the exact iPod target. Overlay its `.rockbox` directory at
the root of an existing Rockbox installation; do not erase the existing
directory first. This public beta intentionally starts with a clean Rekordpod
state namespace; it does not import settings, caches, or workflows from earlier
pre-release namespaces. Create a new device cache and configure the app anew on
the first install. Preserve these user-owned files across later Rekordpod
upgrades:

- `/.rockbox/rekordpod/state/`
- `/.rockbox/rekordpod/rekordpod.cfg`
- `/.rockbox/rekordpod/smart-playlists.rbq`
- `/.rekordpod-workflows.rbl`

The plugin is packaged as `/.rockbox/rocks/apps/rekordpod.rock`. Installation
and testing must never alter Apple firmware partitions. See
[TESTING.md](TESTING.md) before connecting a production library.

## Building

Start with Rockbox's [build instructions](docs/README), configure a separate
build directory for each target, then build and package normally:

```sh
make -j4
make zip
```

Public builds use the default `REKORDPOD_PRIVATE_SCREENSHOTS=0`. Maintainers can
compile the retained private capture implementation for controlled internal
testing by setting that build variable to `1`; such binaries are not public
release artifacts.

Host-side Rekordpod utilities use the pinned packages in
[`utils/rekordpod/requirements.txt`](utils/rekordpod/requirements.txt). Their detailed
workflow is documented in [`utils/rekordpod/README.md`](utils/rekordpod/README.md).

## Release evidence

Every distributed ZIP should be tied to one commit and accompanied by:

- target model and storage configuration;
- SHA-256 digest of the exact ZIP;
- clean build and ZIP-integrity results;
- automated test results;
- the completed hardware matrix from [TESTING.md](TESTING.md);
- known limitations and reproducible failure reports.

Do not advertise an archive as passing a device configuration that was not
tested with that exact archive.

## License, sources, and disclosure

This fork is distributed under the GNU General Public License, version 2 or
later, following Rockbox. See [docs/COPYING](docs/COPYING) for the license and
[ATTRIBUTION.md](ATTRIBUTION.md) for the complete source, dependency, font,
trademark, and AI-assistance record.

Rekordpod has been developed through a **vibe-coding workflow with OpenAI Codex
using GPT-5.6 Sol**, under human direction and testing. AI assistance is not a
substitute for code review, data backups, or physical-device validation.

Rekordpod is an independent interoperability project. It is not affiliated
with or endorsed by Rockbox, Apple, AlphaTheta/Pioneer DJ, Ableton, Panasonic/
Technics, or OpenAI. Product names and trademarks belong to their respective
owners.
