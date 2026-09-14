# Contributing to Rekordpod

Rekordpod is a Rockbox fork that combines embedded code, host-side cache tools,
reverse-engineered interoperability formats, and testing on aging physical
hardware. Small, reproducible changes are much easier to review than broad UI
and storage rewrites.

## Before opening an issue

- Search existing issues and read [TESTING.md](TESTING.md).
- Record the exact commit and SHA-256 of the installed archive.
- Identify the iPod model, storage type, adapter, capacity, filesystem,
  Rekordbox version, and relevant CDJ model/firmware.
- Preserve the exact on-screen error and the smallest repeatable input sequence.
- Remove track names, private databases, journals, serial numbers, and music
  files from public attachments.

Use the structured beta bug form for defects. Security issues or failures that
could silently corrupt data belong in the private path described by
[SECURITY.md](SECURITY.md).

## Before submitting a patch

1. Start from the Rekordpod release branch, not an unrelated Rockbox daily
   snapshot.
2. Keep existing Rockbox style and follow [docs/CONTRIBUTING](docs/CONTRIBUTING).
3. Preserve fail-closed validation and rollback behavior in every write path.
4. Add or update host-side tests when changing installer, cache, journal,
   DeviceSQL, ANLZ, cue-color, or geometry behavior.
5. Run the relevant automated gates in [TESTING.md](TESTING.md).
6. State which physical configurations were actually tested. Do not infer a
   storage or player pass from another configuration.

Pull requests should explain the user-visible outcome, failure modes, test
evidence, and whether the change touches Apple firmware, USB geometry,
filesystem writes, playback timing, or Rekordbox data.

## Licensing and provenance

Contributions are accepted under GPL-2.0-or-later, consistent with Rockbox.
Retain existing copyright and SPDX notices. Identify copied or adapted code,
format research, fonts, generated assets, and licenses in the pull request and
update [ATTRIBUTION.md](ATTRIBUTION.md) when necessary.
