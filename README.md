# Rekordpod

**Rekordpod turns a Rockbox-compatible 6th- or 7th-generation iPod Classic into
a standalone "smart DJ drive" for library preparation.** It is built on top of
a substantial [Rockbox](https://www.rockbox.org/) fork: Rockbox provides the
bootloader, playback, storage, display, and click-wheel foundation, while
Rekordpod adds its DJ interface and a data layer for traditional Rekordbox
exports.

**What does this smart DJ drive actually do?** It lets you browse and search the
collection, preview tracks, inspect analysis, edit cues and beat grids, organize
playlists, update metadata, and retain that work without a laptop nearby.

The iPod still carries the music and connects to a computer or compatible
player as USB storage, but it also has its own screen, controls, playback engine,
waveform views, preparation deck, and traditional Device Library writer.

Rekordbox on the laptop remains the analysis and export station. Once the drive
is provisioned, Rekordpod can work independently and later return supported
changes through the traditional Device Library by updating
`PIONEER/rekordbox/export.pdb` and the matching ANLZ files. **OneLibrary / Device
Library Plus is a separate, newer backend that Rekordpod does not read or
write.** The iPod can physically carry a separate OneLibrary export and present
it to compatible players, but Rekordpod will not modify or reconcile that
database. Changes intended for OneLibrary must first return to Rekordbox through
the traditional Device Library and then be published as a separate OneLibrary
export.

> **Public beta warning**
>
> Rekordpod can rewrite a DJ library. Keep a tested backup of the entire
> `PIONEER` directory and use a disposable or reproducible export until the
> exact build has passed the release gate in [TESTING.md](TESTING.md). A
> successful compile is not evidence that database writes are safe on every
> library, storage adapter, or player.

![Rekordpod main menu on an iPod Classic](docs/screenshots/rekordpod/main-menu.png)

*A DJ library inside the click wheel: Rekordpod's CDJ-inspired main menu.*

## Current targets

| Target | Status | Notes |
| --- | --- | --- |
| iPod Classic 6G/7G | Primary public-beta target | Larger waveform/index caches; optional USB audio path remains experimental |
| Other Rockbox devices | Unsupported | No other target is distributed or claimed compatible in this public beta |

Flash-storage conversions, SSDs, microSD adapter arrays, and original hard
drives all exercise different timing and write behavior. They are separate test
configurations, not interchangeable proof of reliability.

## What is included

- Rekordbox-style collection search, precomputed sort orders, playlist and
  folder browsing, favorites, shuffle, and ordered playlist work.
- A playback-first Prep Deck with seek, scrub, zoom, gain, hot-cue, beat-grid,
  metadata, key, tempo/RPM, loop, playlist, and quantize tools, plus two
  persistent workflow pads.
- RGB waveform and seven audio/analysis views: 20-band EQ, phrase map,
  harmonic constellation, spectral canyon, boombox, stereo orbit, and
  Oscillo-Turntable.
- Transactional on-device editing with verified temporary files, retained
  rollback copies, and append-only edit state.
- A 512-byte USB Mass Storage presentation for the iPod Classic while the
  device keeps its internal 4096-byte virtual-sector handling.
- Power-only USB by default, with Data Transfer and the experimental USB DAC
  mode armed deliberately inside Rekordpod.
- A matching grey Rockbox theme and an optional Rekordpod auto-boot path.

## Rekordpod on the device

These are direct framebuffer captures from the primary iPod Classic build—not
desktop recreations or design mockups. The interface is built around a 320×240
screen, five physical buttons, and the click wheel.

| Collection browser | Playlist carousel |
| :---: | :---: |
| ![Collection browser sorted by import date](docs/screenshots/rekordpod/collection-browser.png) | ![Five-blade playlist carousel](docs/screenshots/rekordpod/playlist-carousel.png) |

### Preparation and analysis

| Hot-cue tools and detailed waveform | 20-band EQ |
| :---: | :---: |
| ![Hot-cue placement tools over the detailed RGB waveform](docs/screenshots/rekordpod/hotcue-tools.png) | ![Audio-reactive 20-band equalizer](docs/screenshots/rekordpod/twenty-band-eq.png) |

| Phrase map | Harmonic constellation |
| :---: | :---: |
| ![Rekordbox analysis-derived phrase map](docs/screenshots/rekordpod/phrase-map.png) | ![Harmonic constellation with chromatic key labels](docs/screenshots/rekordpod/harmonic-constellation.png) |

![Sub-resonance boombox visualizer](docs/screenshots/rekordpod/boombox.png)

![Oscillo-Turntable with RPM, pitch, bend, and tempo data](docs/screenshots/rekordpod/oscillo-turntable.png)

*Oscillo-Turntable combines a Technics-inspired deck, an unrotated oscilloscope,
RPM conversion, pitch, bend, tempo, and a live VU meter.*

The private framebuffer screenshot shortcut is compiled **out** of the public
build. The gallery above was captured from a controlled device-testing build
with that build-time flag enabled. The implementation remains visible in the
source, but the shortcut is absent from public binaries. See the harvesting
checklist in [TESTING.md](TESTING.md).

## Rekordbox compatibility

Rekordpod works with the traditional Rekordbox Device Library: `export.pdb`
plus DAT/EXT analysis files. Supported edits are written directly to that data
on the iPod for use by Rekordbox and compatible players that support the
traditional Device Library.

Rekordpod does not read or write OneLibrary / Device Library Plus. The same
iPod can contain a separately generated OneLibrary export and serve as the
physical USB drive for that export, but that does not make the newer database
visible to Rekordpod. Traditional Device Library and OneLibrary data on one
drive are independent snapshots. Editing one does not update the other, so
using both without deliberately re-importing and re-exporting changes will
produce database discrepancies.

### Using Rekordpod with a OneLibrary collection

Rekordpod can still be part of a OneLibrary preparation workflow:

1. Export a traditional Device Library from Rekordbox to the iPod.
2. Prepare tracks and make supported changes in Rekordpod.
3. Reconnect the iPod and import those recognized metadata and preparation
   changes into the main Rekordbox collection.
4. Create or update the OneLibrary / Device Library Plus export from Rekordbox,
   either on the same iPod or on separate performance media.

The traditional iPod export is the interchange path in this workflow. It does
**not** become a OneLibrary database. Playback hardware that requires the newer
format still needs a separate, valid OneLibrary export, even when both exports
are stored on the same iPod.

### Using the iPod as CDJ media

Users who primarily use the traditional Device Library can also connect the
iPod as a USB drive to compatible CDJs and play from that export. This is an
experimental, at-your-own-risk use: test the exact iPod, storage adapter,
capacity, Rekordpod build, CDJ model, and CDJ firmware before relying on it.
Keep another verified copy of the performance library available.

Original spinning disks are particularly vulnerable to shock, vibration,
spin-up delays, and marginal USB power in a live environment. For performance
use, solid-state storage or flash cards in a known-compatible third-party iPod
adapter are recommended. That reduces mechanical risk but does not guarantee
player, filesystem, adapter, or power compatibility.

The desktop utilities in [`utils/rekordpod`](utils/rekordpod) convert an existing
Device Library and ANLZ corpus into the compact index and waveform cache used by
the plugin. They are provisioning, recovery, and diagnostic tools. Normal
supported edits are made on the device once that cache exists.

## What setup requires

Rekordpod is not installed by copying `rekordpod.rock` onto an arbitrary
Rockbox build. The plugin, modified Rockbox firmware, codecs, USB behavior, and
generated device cache are a matched set.

You need:

1. **A supported 6th- or 7th-generation iPod Classic with a working Rockbox
   installation and bootloader.** Rekordpod runs on Rockbox; it does not replace
   the Apple firmware partition or install its own bootloader.
2. **A traditional Rekordbox Device Library already exported to the iPod.**
   Analyze the tracks in Rekordbox before exporting them because Rekordpod does
   not perform full audio analysis on the iPod.
3. **One desktop installer for your computer:** `Rekordpod Installer.exe` on
   Windows or `Rekordpod Installer.app` on macOS. They provide the same setup
   workflow; you do not need both.

### Why the desktop installer is required

The executable is the provisioning bridge between a Rekordbox export and the
iPod application. It:

- reads only the mounted volume you choose and verifies that it is a supported
  Rockbox iPod with a usable traditional Device Library;
- verifies and installs the matching iPod Classic Rekordpod build;
- converts the existing database and ANLZ material into the compact index and
  waveform cache that the iPod can browse quickly;
- installs the matched Rockbox overlay, Rekordpod plugin, theme, and cache.

It does **not** format the iPod, install the Rockbox bootloader, analyze or
re-encode music, erase the `Contents` tree, or remain necessary while Rekordpod
is running. The `.exe` and `.app` are operating-system packages around the same
installer logic; they exist so users do not need Python or command-line setup.
Run the installer for the first installation and again when a Rekordpod update
or newly exported desktop library requires a fresh device cache.

### Setup in four steps

1. Install and boot the normal Rockbox build for the exact iPod model.
2. Analyze the collection in Rekordbox and export a traditional Device Library
   to the iPod.
3. Mount the iPod, open the installer for Windows or macOS, choose that volume,
   and let it build and install the matched package.
4. Eject cleanly, boot without USB attached, and complete the first-boot and
   write-parity checks in [TESTING.md](TESTING.md).

For the safe clean-install rehearsal, translated USB-storage warning, and
recovery checkpoints, follow [INSTALL.md](INSTALL.md). Installer packaging and
platform-specific signing details are in
[`utils/rekordpod/installer/README.md`](utils/rekordpod/installer/README.md).

## Upgrades and user state

Use the desktop installer for ordinary installation and upgrades. This public
beta intentionally starts with a clean Rekordpod state namespace; it does not
import settings, caches, or workflows from earlier pre-release namespaces.
Create a new device cache and configure the app anew on the first install.
Preserve these user-owned files across later Rekordpod upgrades:

- `/.rockbox/rekordpod/state/`
- `/.rockbox/rekordpod/rekordpod.cfg`
- `/.rockbox/rekordpod/smart-playlists.rbq`
- `/.rekordpod-workflows.rbl`

The plugin is packaged as `/.rockbox/rocks/apps/rekordpod.rock`, but it depends
on the rest of the matching Rekordpod Rockbox build and cache. Installation and
testing must never alter Apple firmware partitions. Follow the [clean-install
rehearsal](INSTALL.md), then complete [TESTING.md](TESTING.md) before connecting
a production library.

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
[`utils/rekordpod/requirements.txt`](utils/rekordpod/requirements.txt). Their
detailed workflow is documented in
[`utils/rekordpod/README.md`](utils/rekordpod/README.md).

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

## Project documentation

- [About Rekordpod](ABOUT.md)
- [Installation and recovery](INSTALL.md)
- [Public-beta release notes](RELEASE_NOTES.md)
- [Testing and screenshot checklist](TESTING.md)
- [Contributing](CONTRIBUTING.md)
- [Security and destructive data-loss reporting](SECURITY.md)
- [Sources, licenses, and disclosure](ATTRIBUTION.md)

## License, sources, and disclosure

This fork is distributed under the GNU General Public License, version 2 or
later, following Rockbox. See [docs/COPYING](docs/COPYING) for the license and
[ATTRIBUTION.md](ATTRIBUTION.md) for the complete source, dependency, font,
trademark, and AI-assistance record.

Rekordpod has been developed through a **vibe-coding workflow with OpenAI Codex
using GPT-5.6 Sol**, under human direction and testing. AI assistance is not a
substitute for code review, data backups, or physical-device validation.

Rekordpod is an independent interoperability project. It is not affiliated
with or endorsed by Rockbox, Apple, AlphaTheta/Pioneer DJ, Ableton,
Panasonic/Technics, or OpenAI. Product names and trademarks belong to their
respective owners.
