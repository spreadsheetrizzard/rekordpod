# Security and data-loss reporting

Rekordpod is a public beta that can rewrite a Rekordbox Device Library and its
analysis files. A defect that silently modifies the wrong track, bypasses path
validation, writes outside the selected volume, damages filesystem geometry,
or makes recovery copies unusable should be treated as a security or
destructive-data-loss issue.

## Reporting privately

Use GitHub's private vulnerability-reporting feature for the Rekordpod
repository. Do not open a public issue containing an `export.pdb`, ANLZ file,
journal, device identifier, private track metadata, or unreleased exploit.

Include:

- the exact commit and installed archive SHA-256;
- iPod model, storage, adapter, capacity, filesystem, and host operating system;
- the complete error text and minimal reproduction sequence;
- whether playback, USB, or a library write was active;
- filenames and hashes of preserved recovery artifacts; and
- the smallest redacted fixture that demonstrates the problem, if one can be
  shared legally and safely.

## Immediate safety response

Stop using the affected write path. Do not ask Rekordbox or a CDJ to repair the
only copy of the export. Preserve the complete `PIONEER` directory, active
`.rockbox` tree, recovery files, installed ZIP, and disk-layout record before
attempting recovery from a verified independent backup.

There is currently no guaranteed response window. This project is experimental
and provided without warranty under GPL-2.0-or-later.
