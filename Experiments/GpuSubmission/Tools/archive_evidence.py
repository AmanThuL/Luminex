#!/usr/bin/env python3
"""Index an external evidence bundle without modifying or discarding retained attempts."""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def digest(path):
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bundle", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    bundle = args.bundle.resolve(strict=True)
    output = args.output.resolve()
    if output.exists():
        parser.error("refusing to overwrite an existing artifact index")
    root = Path(__file__).resolve().parents[3]
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip()
    status = subprocess.check_output(["git", "status", "--porcelain", "--untracked-files=all"],
                                     cwd=root, text=True)
    paths = sorted(path for path in bundle.rglob("*") if path.is_file())
    if any(path.is_symlink() for path in bundle.rglob("*")):
        parser.error("bundle must be self-contained, without symlinks")
    records = [{"path": path.relative_to(bundle).as_posix(), "bytes": path.stat().st_size,
                "sha256": digest(path)} for path in paths]
    data = {"schemaVersion": 1, "sourceRevision": revision, "sourceDirty": bool(status),
            "artifactCount": len(records), "totalBytes": sum(row["bytes"] for row in records),
            "files": records}
    with output.open("x", encoding="utf-8") as stream:
        json.dump(data, stream, indent=2, sort_keys=True, allow_nan=False)
        stream.write("\n")
    print(f"Indexed {len(records)} artifacts; index SHA256 {digest(output)}")


if __name__ == "__main__":
    main()
