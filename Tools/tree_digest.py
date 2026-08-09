#!/usr/bin/env python3
"""Hash a directory from sorted relative paths and file contents."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path


def tree_digest(root: Path) -> str:
    digest = hashlib.sha256()
    for path in sorted(
            candidate for candidate in root.rglob("*")
            if candidate.is_file() and "Baked" not in candidate.relative_to(root).parts):
        # "Baked" holds Tools/bake_gltf_textures.py's derived DDS output, not fetched/converted
        # provenance data -- it is excluded so the digest stays stable across bake re-runs.
        digest.update(path.relative_to(root).as_posix().encode("utf-8"))
        digest.update(b"\0")
        digest.update(hashlib.sha256(path.read_bytes()).digest())
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    if not args.directory.is_dir():
        parser.error(f"not a directory: {args.directory}")
    print(tree_digest(args.directory))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
