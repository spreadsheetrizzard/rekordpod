# Sources, licenses, and disclosure

This document records the material code bases, libraries, format research, and
design references relevant to Rekordpod. It distinguishes code dependencies
from visual inspiration and avoids implying endorsement.

## Rockbox

Rekordpod is a modified Rockbox distribution, not a standalone firmware.

- Project: [Rockbox](https://www.rockbox.org/)
- Public source mirror: [Rockbox/rockbox](https://github.com/Rockbox/rockbox)
- Local baseline for this fork: merge base
  `2ec4760117236f598b60e15224ef72bcbeb7c288`
- License: GNU General Public License, version 2 or later; the complete local
  license text is in [docs/COPYING](docs/COPYING).
- Existing component-specific notices remain in
  [docs/LICENSES](docs/LICENSES), source headers, fonts, codecs, and bundled
  libraries. Those notices must not be removed when redistributing the fork.

The Rekordpod plugin, on-device writer, integration changes, and Rekordpod Python
utilities carry `SPDX-License-Identifier: GPL-2.0-or-later` markers and are
distributed as part of this fork under the same terms.

## Direct host-tool dependencies

These Python packages are installed separately; their source is not vendored in
this repository.

### rekordbox-pdb 0.1.0

- Source: [fragmede/rekordbox-pdb](https://github.com/fragmede/rekordbox-pdb)
- License: MIT
- Use here: reading the traditional DeviceSQL `export.pdb` during cache import,
  optional host-side journal recovery, and format reference for the on-device
  interoperability writer.

### pyrekordbox 0.4.4

- Source: [dylanljones/pyrekordbox](https://github.com/dylanljones/pyrekordbox)
- Documentation: [pyrekordbox documentation](https://pyrekordbox.readthedocs.io/)
- License: MIT
- Copyright identified by the project: Dylan Jones and contributors
- Use here: parsing rekordbox ANLZ analysis files during cache provisioning.

The versions above are pinned in
[`utils/rekordpod/requirements.txt`](utils/rekordpod/requirements.txt). Their MIT
licenses apply to those packages; they do not replace this fork's GPL license.

## Format research and prior art

The DeviceSQL and ANLZ formats are undocumented interoperability targets. The
following public projects are important provenance and cross-checking sources;
they are not vendored dependencies in this repository unless a file says
otherwise.

- [Deep Symmetry crate-digger](https://github.com/Deep-Symmetry/crate-digger),
  including its Kaitai `rekordbox_pdb.ksy` format specification. The spec
  identifies its own EPL-1.0 license and documents earlier reverse-engineering
  contributors.
- [rekordcrate](https://github.com/Holzhaus/rekordcrate), an independent Rust
  implementation cited as prior art by `rekordbox-pdb`.

Rekordpod's Python importer also intentionally aligns its metadata shape,
playlist ordering, stable `rb:<TrackID>` identities, and SHA-256 metadata digest
convention with the project owner's local **Drag'n'Dunk v0.0.40** companion
project. Drag'n'Dunk source is not vendored here, no public repository was found
in the audited checkout, and the inspected local package did not contain a
license file. This attribution therefore records design lineage without
granting or asserting a third-party license.

## Fonts and interface references

The Rekordpod startup uses Adobe Helvetica BDF files already present in
Rockbox. Their Adobe Systems and Digital Equipment Corporation copyright and
permission notices are embedded in the relevant files, including
[`fonts/18-Adobe-Helvetica.bdf`](fonts/18-Adobe-Helvetica.bdf). Preserve those
notices.

The interface is original low-resolution pixel rendering informed by familiar
DJ-player conventions and by user-directed references to Pioneer CDJ/
rekordbox, Technics turntables, Ableton cue markers, the Xbox 360 Blades
dashboard, Apple Music, Spotify, and Teenage Engineering products. These are
design references, not bundled assets or source-code dependencies. No claim is
made that the interface is an exact replica, authorized skin, or official
implementation of any referenced product.

## AI-assisted development disclosure

Rekordpod has been developed through a **vibe-coding workflow with OpenAI Codex
using GPT-5.6 Sol**, under human direction, iteration, and physical-device
testing. GPT-5.6 Sol is the model name; “ChatGPT-5.6 Sol” is not used because it
would inaccurately conflate the model with the ChatGPT product.

AI-generated or AI-modified code can contain defects, unsafe assumptions,
licensing mistakes, and security issues. The disclosure is not evidence of
correctness and does not alter copyright ownership, contributor history, or
the obligations of the GPL and third-party licenses. Public release requires
human review and the exact-build test evidence described in [TESTING.md](TESTING.md).

Model reference:
[OpenAI GPT-5.6 Sol documentation](https://developers.openai.com/api/docs/models/gpt-5.6-sol).

## Trademarks and independence

Rekordpod is not affiliated with or endorsed by Rockbox, Apple, AlphaTheta/
Pioneer DJ, Ableton, Panasonic/Technics, Spotify, Microsoft, Teenage
Engineering, Adobe, or OpenAI. Rockbox, iPod, rekordbox, CDJ, XDJ, Ableton,
Technics, Xbox, Spotify, Teenage Engineering, Helvetica, OpenAI, ChatGPT, and
Codex are trademarks or names of their respective owners.

This software is provided without warranty under the terms of the GPL. Back up
every library before testing write paths.
