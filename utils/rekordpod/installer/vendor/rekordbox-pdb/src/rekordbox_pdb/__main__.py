"""Dump the contents of a rekordbox export.pdb / exportExt.pdb file.

Usage:
    python -m rekordbox_pdb <file.pdb>          # export.pdb
    python -m rekordbox_pdb <file.pdb> --ext    # exportExt.pdb
"""

import argparse

from .pdb import Database, ExtDatabase


def _dump_export(db: Database) -> None:
    artists = {a.id: a.name for a in db.artists}
    albums = {a.id: a.name for a in db.albums}
    keys = {k.id: k.name for k in db.keys}
    genres = {g.id: g.name for g in db.genres}

    print(f"{len(db.tracks)} tracks")
    for t in sorted(db.tracks, key=lambda t: t.id):
        bpm = f"{t.tempo / 100:.2f}"
        details = [
            f"artist={artists.get(t.artist_id, '')!r}",
            f"album={albums.get(t.album_id, '')!r}",
            f"genre={genres.get(t.genre_id, '')!r}",
            f"key={keys.get(t.key_id, '')!r}",
            f"bpm={bpm}",
            f"{t.duration // 60}:{t.duration % 60:02d}",
            f"rating={t.rating}",
        ]
        print(f"  [{t.id}] {t.title!r} " + " ".join(details))
        print(f"        {t.file_path}")

    print(f"{len(db.playlist_tree)} playlists")
    entries_by_playlist: dict[int, list] = {}
    for e in db.playlist_entries:
        entries_by_playlist.setdefault(e.playlist_id, []).append(e)
    for node in db.playlist_tree:
        kind = "folder" if node.is_folder else "playlist"
        count = len(entries_by_playlist.get(node.id, []))
        print(f"  [{node.id}] {kind} {node.name!r} ({count} entries)")


def _dump_ext(ext: ExtDatabase) -> None:
    categories = sorted(
        (t for t in ext.tags if t.is_category), key=lambda t: t.position)
    print(f"{len(categories)} My Tag categories")
    for cat in categories:
        print(f"  [{cat.id}] {cat.name!r}")
        tags = sorted(
            (t for t in ext.tags if not t.is_category and t.category_id == cat.id),
            key=lambda t: t.position,
        )
        for tag in tags:
            print(f"      {tag.name!r} (id={tag.id:#010x})")


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(
        prog="rekordbox_pdb", description=__doc__)
    parser.add_argument("file", help="path to export.pdb or exportExt.pdb")
    parser.add_argument(
        "--ext", action="store_true",
        help="parse as exportExt.pdb (My Tag database)")
    args = parser.parse_args(argv)

    if args.ext:
        _dump_ext(ExtDatabase.from_file(args.file))
    else:
        _dump_export(Database.from_file(args.file))


if __name__ == "__main__":
    main()
