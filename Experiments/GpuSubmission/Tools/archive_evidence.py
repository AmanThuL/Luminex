#!/usr/bin/env python3
"""Index an external evidence bundle without modifying or discarding retained attempts."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import stat
import subprocess


def digest(path):
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def inventory(bundle):
    """Hash regular files without following links; resolve aliases only in this inventory.

    Directory-relative, no-follow opens keep traversal inside the opened evidence root.
    Schema 2 aliases retain linkTarget and contentSHA256; totalBytes counts logical
    content per path, including aliases, and is not physical/deduplicated storage.
    """
    entries = {}

    def walk(directory, parent):
        with os.scandir(directory) as children:
            names = sorted(child.name for child in children)
        for name in names:
            relative = parent / name
            mode = os.stat(name, dir_fd=directory, follow_symlinks=False).st_mode
            if stat.S_ISLNK(mode):
                entries[relative] = {"linkTarget": os.readlink(name, dir_fd=directory)}
            elif stat.S_ISDIR(mode):
                entries[relative] = None
                child = os.open(name, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW,
                                dir_fd=directory)
                try:
                    walk(child, relative)
                finally:
                    os.close(child)
            elif stat.S_ISREG(mode):
                descriptor = os.open(name, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK,
                                     dir_fd=directory)
                with os.fdopen(descriptor, "rb") as stream:
                    if not stat.S_ISREG(os.fstat(stream.fileno()).st_mode):
                        raise ValueError(f"special file: {relative}")
                    value = hashlib.sha256()
                    size = 0
                    for block in iter(lambda: stream.read(1024 * 1024), b""):
                        value.update(block)
                        size += len(block)
                entries[relative] = {"bytes": size, "sha256": value.hexdigest()}
            else:
                raise ValueError(f"special file: {relative}")

    root = os.open(bundle, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    try:
        walk(root, Path())
    finally:
        os.close(root)

    def resolve_alias(alias, active):
        if alias in active:
            raise ValueError(f"cyclic symlink: {alias}")
        target = entries[alias]["linkTarget"]
        if Path(target).is_absolute():
            raise ValueError(f"absolute symlink: {alias}")
        current = list(alias.parent.parts)
        # Preserve trailing slashes and dot components: "regular-file/." is not a file alias.
        parts = target.split("/")
        entry = None
        for index, part in enumerate(parts):
            if part in ("", "."):
                pass
            elif part == "..":
                if not current:
                    raise ValueError(f"out-of-bundle symlink: {alias}")
                current.pop()
            else:
                current.append(part)
            relative = Path(*current)
            if not current:
                entry = None
                continue
            if relative not in entries:
                raise ValueError(f"broken symlink: {alias}")
            entry = entries[relative]
            if entry is not None and "linkTarget" in entry:
                entry = resolve_alias(relative, active | {alias})
            if entry is not None and index != len(parts) - 1:
                raise ValueError(f"non-directory symlink path: {alias}")
        if entry is None:
            raise ValueError(f"directory symlink: {alias}")
        return entry

    records = []
    for relative, entry in sorted(entries.items()):
        if entry is None:
            continue
        if "linkTarget" in entry:
            content = resolve_alias(relative, set())
            entry = {"linkTarget": entry["linkTarget"], "bytes": content["bytes"],
                     "contentSHA256": content["sha256"]}
        records.append({"path": relative.as_posix(), **entry})
    return records


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bundle", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args(argv)
    bundle = args.bundle.resolve(strict=True)
    # Do not resolve the final component: even a dangling output symlink is a collision.
    output = args.output.absolute()
    if output.exists() or output.is_symlink():
        parser.error("refusing to overwrite an existing artifact index")
    try:
        # Enumerate before exclusive creation, so an index inside the bundle excludes itself.
        records = inventory(bundle)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    root = Path(__file__).resolve().parents[3]
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip()
    status = subprocess.check_output(["git", "status", "--porcelain", "--untracked-files=all"],
                                     cwd=root, text=True)
    data = {"schemaVersion": 2, "sourceRevision": revision, "sourceDirty": bool(status),
            "artifactCount": len(records), "totalBytes": sum(row["bytes"] for row in records),
            "files": records}
    with output.open("x", encoding="utf-8") as stream:
        json.dump(data, stream, indent=2, sort_keys=True, allow_nan=False)
        stream.write("\n")
    print(f"Indexed {len(records)} artifacts; index SHA256 {digest(output)}")


if __name__ == "__main__":
    main()
