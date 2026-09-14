# About Rekordpod

## The smart DJ thumb drive

Rekordpod asks a simple question: what should a DJ drive be able to do when it
is away from the laptop?

A normal export stores music and preparation data. Rekordpod adds a screen,
click-wheel controls, playback, detailed waveforms, collection search,
playlist organization, cue and beat-grid editing, metadata tools, and a writer
for the traditional Rekordbox Device Library. The result is a portable library
that can help prepare itself.

Rekordpod is not a skin or a single `.rock` file. It is a substantial
GPL-licensed fork of [Rockbox](https://www.rockbox.org/) with matched firmware,
plugin, USB-storage behavior, desktop provisioning tools, and a compact cache
built from an existing Rekordbox export.

## How the workflow fits together

Rekordbox remains the heavy analysis and export station. It analyzes audio and
writes a traditional Device Library to the iPod. The Rekordpod desktop
installer validates that export, builds an iPod-friendly index and waveform
cache, and installs the matched Rockbox distribution. After that, Rekordpod can
browse, play, organize, and make supported preparation changes without a
laptop nearby.

When the iPod returns to Rekordbox, recognized changes can be imported into the
main collection. Rekordbox can then publish them to other media or to a
separate OneLibrary / Device Library Plus export.

The same iPod can physically carry both database generations, but Rekordpod
only reads and writes the traditional Device Library. It does not inspect,
modify, or reconcile OneLibrary. Keeping both exports on one drive without an
intentional import-and-re-export workflow will allow them to diverge.

## Public-beta scope

The first public build targets the iPod Classic 6G/7G. No other Rockbox target
is distributed or claimed compatible in this public beta.

This beta writes DJ-library data on unusually varied legacy hardware. Original
hard disks, SSDs, and single- or multi-card flash adapters have different
timing and failure behavior. Every tester needs an independent backup of the
complete `PIONEER` directory and should treat a new build, storage adapter, CDJ,
or database shape as a new test configuration.

## Project map

- [README.md](README.md) — overview, requirements, screenshots, and setup
- [INSTALL.md](INSTALL.md) — safe installation and clean-install rehearsal
- [RELEASE_NOTES.md](RELEASE_NOTES.md) — public-beta scope and limitations
- [TESTING.md](TESTING.md) — device test matrix and release gate
- [CONTRIBUTING.md](CONTRIBUTING.md) — patches and reproducible reports
- [SECURITY.md](SECURITY.md) — private reporting for security or destructive
  data-loss issues
- [ATTRIBUTION.md](ATTRIBUTION.md) — licenses, research sources, design lineage,
  and AI-assistance disclosure

Rekordpod is independent and is not affiliated with or endorsed by Rockbox,
Apple, AlphaTheta/Pioneer DJ, Panasonic/Technics, or OpenAI.
