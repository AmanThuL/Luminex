#!/usr/bin/env python3
"""Fetch, verify and atomically publish the optional San Miguel realtime scene."""

from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import json
import shutil
import subprocess
import zipfile
from pathlib import Path

from convert_obj_to_gltf import convert
from tree_digest import tree_digest


def sha256(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def checked_hash(path: Path, expected: str) -> None:
    actual = sha256(path)
    if actual != expected:
        raise ValueError(f"{path}: SHA-256 mismatch: expected {expected}, got {actual}")


def fetch_archive(url: str, destination: Path, size: int, expected: str) -> None:
    if destination.exists() and sha256(destination) == expected:
        return
    # The archive host supports byte ranges but throttles individual transfers. Bound concurrency
    # and memory, retain completed chunks across retries, then verify the complete pinned archive.
    chunks = destination.with_suffix(f".parts-{expected}")
    chunks.mkdir(exist_ok=True)
    chunk_size = 8 * 1024 * 1024
    ranges = [(start, min(size - 1, start + chunk_size - 1))
              for start in range(0, size, chunk_size)]

    def fetch(bounds: tuple[int, int]) -> Path:
        start, end = bounds
        complete = chunks / f"{start:012d}"
        if complete.exists() and complete.stat().st_size == end - start + 1:
            return complete
        partial = complete.with_suffix(".part")
        subprocess.run(["curl", "--fail", "--silent", "--show-error", "--location",
                        "--retry", "3", "--connect-timeout", "30", "--max-time", "900",
                        "--range", f"{start}-{end}", "--output", str(partial), url], check=True)
        if partial.stat().st_size != end - start + 1:
            raise ValueError(f"{url}: range response has the wrong length")
        partial.replace(complete)
        return complete

    with concurrent.futures.ThreadPoolExecutor(max_workers=16) as pool:
        pieces = list(pool.map(fetch, ranges))
    partial_archive = destination.with_suffix(f".part-{expected}")
    with partial_archive.open("wb") as output:
        for piece in pieces:
            with piece.open("rb") as source:
                shutil.copyfileobj(source, output)
    try:
        checked_hash(partial_archive, expected)
    except ValueError:
        # Length alone cannot identify a corrupt cached range. None of this version's chunks
        # can be trusted after the assembled digest fails, so let the next run fetch them anew.
        partial_archive.unlink(missing_ok=True)
        shutil.rmtree(chunks)
        raise
    partial_archive.replace(destination)
    shutil.rmtree(chunks)


def publish(args: argparse.Namespace) -> None:
    target = Path("Assets/Fetched/SanMiguel")
    if target.exists():
        actual = tree_digest(target)
        if actual != args.tree_sha256:
            raise ValueError(f"{target}: tree checksum mismatch: expected {args.tree_sha256}, got {actual}")
        print("San Miguel: verified existing converted scene")
        return
    target.parent.mkdir(parents=True, exist_ok=True)
    archive = target.parent / ".san-miguel.zip"
    fetch_archive(args.url, archive, args.size, args.archive_sha256)
    source = target.parent / ".san-miguel-source"
    stage = target.parent / ".san-miguel-stage"
    for path in (source, stage):
        if path.exists():
            shutil.rmtree(path)
        path.mkdir()
    with zipfile.ZipFile(archive) as package:
        for member in package.infolist():
            if member.filename not in ("san-miguel-low-poly.obj", "san-miguel-low-poly.mtl",
                                       "license.txt") and not member.filename.startswith("textures/"):
                continue
            output = (source / member.filename).resolve()
            if source.resolve() not in output.parents:
                raise ValueError(f"archive member escapes source directory: {member.filename}")
            package.extract(member, source)
    metadata = stage / "ARCHIVE_INFO.js"
    subprocess.run(["curl", "--fail", "--silent", "--show-error", "--location", "--retry", "3",
                    "--max-time", "120", "--output", str(metadata), args.info_url], check=True)
    checked_hash(metadata, args.info_sha256)
    convert(source / "san-miguel-low-poly.obj", source / "san-miguel-low-poly.mtl",
            stage, scale=1.0, name="SanMiguel", alpha_mask=True,
            normal_map_prefix="N_", phong_roughness=True)
    shutil.copyfile(source / "license.txt", stage / "LICENSE.txt")
    (stage / "PROVENANCE.json").write_text(json.dumps({
        "title": "San Miguel 2.0 (archive license names version 2.1)",
        "archiveUrl": args.url, "archiveSha256": args.archive_sha256,
        "archiveInfoUrl": args.info_url, "archiveInfoSha256": args.info_sha256,
        "variant": "san-miguel-low-poly.obj",
        "attribution": "Guillermo M. Leal Llaguno; Morgan McGuire, Guedis Cardenas, Michael Mara, Nicholas Hull",
        "license": "Archive metadata: CC BY 3.0; preserve the accompanying LICENSE.txt terms verbatim",
        "changes": "Core glTF conversion, metre-scale geometry, diffuse alpha as two-sided MASK at 0.5; N_ tangent-space normal maps; Phong roughness approximation; no blended transparency or height-map conversion",
    }, indent=2) + "\n")
    actual = tree_digest(stage)
    if actual != args.tree_sha256:
        raise ValueError(f"converted San Miguel checksum mismatch: expected {args.tree_sha256}, got {actual}")
    stage.replace(target)
    shutil.rmtree(source)
    print("San Miguel: converted, verified and published")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url", required=True)
    parser.add_argument("--info-url", required=True)
    parser.add_argument("--size", type=int, required=True)
    parser.add_argument("--archive-sha256", required=True)
    parser.add_argument("--info-sha256", required=True)
    parser.add_argument("--tree-sha256", required=True)
    args = parser.parse_args()
    if args.size <= 0:
        parser.error("archive size must be positive")
    publish(args)


if __name__ == "__main__":
    main()
