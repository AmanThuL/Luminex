#!/usr/bin/env python3
"""Inspect native capture labels, decoded targets and uniquely identifiable guarded arguments."""

import argparse
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "Tools/GpuDebug"))
import bundlelib
import decodelib
import pnglib
import schemalib


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--capture", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    if args.output.exists():
        parser.error("output must be a new directory")
    meta = json.loads(Path(str(args.capture) + ".capture.json").read_text())
    manifest = json.loads(args.manifest.read_text())
    if meta["logicalFrame"] != 0 or meta["candidateCount"] != manifest["count"]:
        parser.error("inspection currently supports the matching phase-zero capture only")
    bundle = bundlelib.walk_bundle(args.capture)
    hits = bundlelib.scan_labels(bundle)
    labels = {hit.label for hit in hits}
    resources = [schemalib.Resource(t["label"], "texture2d", t["format"], t["width"],
                                   t["height"], 1, None) for t in meta["textures"]]
    schema = schemalib.Schema({}, resources, [], [])
    joined = bundlelib.join(bundle, schema, hits)
    args.output.mkdir(parents=True)
    images = []
    for resource, blob in joined.matched:
        decoded = decodelib.decode_texture(resource, blob.path.read_bytes())
        filename = resource.label.rsplit(".", 1)[-1] + ".png"
        pnglib.write_png(args.output / filename, decoded.width, decoded.height, decoded.png_rgb_rows)
        images.append(dict(label=resource.label, file=filename, blob=blob.path.name,
                           stats=decoded.stats))
    expected = []
    for object_id, values in enumerate(manifest["phaseZeroTransforms"]):
        x, y, z, _, radius = values
        if all(p[0] * x + p[1] * y + p[2] * z + p[3] >= -radius
               for p in manifest["camera"]["planes"]):
            expected.append(object_id)
    argument = next(b for b in meta["buffers"] if b["binding"] == 3)
    # Buffer labels are not joined to contents in this capture format. Combine exact size with
    # the changing per-submission guard, and reject ambiguous matches instead of choosing a blob.
    guard = struct.pack("<I", 0xC04D0000 ^ meta["completionValue"]) * 64
    candidates = []
    for blob in bundle.blobs:
        if blob.size_bytes != argument["requestedBytes"]:
            continue
        data = blob.path.read_bytes()
        if data[:256] == guard and data[-256:] == guard:
            candidates.append((blob, data))
    observed, records, agreement = [], [], None
    if meta["variant"] == "gpu-args" and meta["candidateCount"] and len(candidates) == 1:
        blob, data = candidates[0]
        start = argument["dataOffset"]
        records = list(struct.iter_unpack("<4I", data[start:start + meta["candidateCount"] * 16]))
        if any(a != 3 * meta["triangles"] or b > 1 or c != i * a or d != 0
               for i, (a, b, c, d) in enumerate(records)):
            raise ValueError("captured GPU arguments have an invalid layout/value")
        observed = [i for i, record in enumerate(records) if record[1]]
        agreement = observed == expected
        if not agreement:
            raise ValueError("captured argument visibility differs from independent manifest oracle")
    report = dict(schemaVersion=1, suite=meta["suite"], variant=meta["variant"],
                  candidateCount=meta["candidateCount"], expectedVisible=len(expected),
                  observedVisible=len(observed) if agreement is not None else None,
                  labels=[dict(label=b["label"], present=b["label"] in labels)
                          for b in meta["buffers"] + meta["textures"]],
                  images=images, argumentAgreement=agreement,
                  argumentAttribution="unique exact size and changing slot guard; not label identity",
                  argumentBlobs=[b.path.name for b, _ in candidates],
                  argumentNote="No live arguments in an empty capture" if not meta["candidateCount"]
                  else ("verified" if agreement else "unresolved; absent/ambiguous guarded blob"),
                  firstArguments=records[:8], lastArguments=records[-8:])
    (args.output / "inspection.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
