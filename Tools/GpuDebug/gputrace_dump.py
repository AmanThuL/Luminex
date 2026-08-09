#!/usr/bin/env python3
"""Decode a .gputrace capture bundle into a manifest.json describing what was recovered.

Walks the bundle, loads its schema sidecar, recovers lmx.* labels from the device-resources
streams, joins labels/blobs/schema resources according to the measured bundle format, then
decodes each matched texture's primary blob to a PNG and statistics (see
decodelib.py/pnglib.py), and
finally runs the anomaly checks over everything recovered (see anomalylib.py) into
manifest["anomalies"], severity-ordered.

Usage: python3 gputrace_dump.py <bundle.gputrace> [--schema <path>] [--out <dir>]
  --schema defaults to <bundle>.schema.json (the sidecar CaptureSchema writes next to the bundle)
  --out defaults to <bundle-stem>-dump/ (also where decoded PNGs land, alongside manifest.json)

Exit codes: 0 on success, including partial success (gaps are recorded in the manifest, not
treated as failure -- e.g. a fallback texture with no captured contents, or a BC1-compressed
texture with no pixel decode, are both normal). Non-zero (2) only when the bundle or schema can't
be read at all (BundleError/SchemaError).
"""
import argparse
import json
import pathlib
import re
import sys

import anomalylib
import bundlelib
import decodelib
import pnglib
import schemalib
import uniformlib

_MIP_SLICE_RE = re.compile(r"mipmap(\d+)-slice(\d+)")

_MATRIX_ORDER_NOTE = "row-major (transposed from column-major storage)"


def _resource_json(resource: schemalib.Resource) -> dict:
    out = {"label": resource.label, "kind": resource.kind}
    if resource.kind == "buffer":
        out["sizeBytes"] = resource.size_bytes
    else:
        out["format"] = resource.format
        out["width"] = resource.width
        out["height"] = resource.height
        out["mipLevels"] = resource.mip_levels
    return out


def _blob_json(blob: bundlelib.Blob) -> dict:
    return {"path": blob.path.name, "sizeBytes": blob.size_bytes}


def _resources_json(join_result: bundlelib.JoinResult) -> dict:
    return {
        "matched": [
            {"resource": _resource_json(resource), "blob": _blob_json(blob)}
            for resource, blob in join_result.matched
        ],
        "matchedUndecodable": [
            {"resource": _resource_json(resource), "reason": reason}
            for resource, reason in join_result.matched_undecodable
        ],
        "unmatchedBlobs": [_blob_json(blob) for blob in join_result.unmatched_blobs],
        "missingResources": [
            _resource_json(resource) for resource in join_result.missing_resources
        ],
    }


def _mip_level(blob: bundlelib.Blob):
    match = _MIP_SLICE_RE.search(blob.path.name)
    return int(match.group(1)) if match else None


def _primary_texture_pairs(join_result: bundlelib.JoinResult):
    """One (resource, blob) pair per texture resource in `join_result.matched`.

    join() emits one matched entry per mip/slice blob a resource owns, but decoding is scoped to
    each resource's primary
    (numerically lowest mip) blob only -- mirroring bundlelib._select_primary_texture_blob's own
    tie-break, since that's the blob join() already geometry-validated against the schema. Buffer
    resources are excluded because uniformlib decodes those separately.
    """
    blobs_by_label: dict = {}
    resource_by_label: dict = {}
    for resource, blob in join_result.matched:
        if resource.kind == "buffer":
            continue
        blobs_by_label.setdefault(resource.label, []).append(blob)
        resource_by_label[resource.label] = resource

    pairs = []
    for label, blobs in blobs_by_label.items():
        numbered = [(level, blob) for blob in blobs if (level := _mip_level(blob)) is not None]
        if numbered:
            numbered.sort(key=lambda pair: (pair[0], pair[1].path.name))
            primary = numbered[0][1]
        else:
            primary = sorted(blobs, key=lambda b: b.path.name)[0]
        pairs.append((resource_by_label[label], primary))
    return pairs


def _label_to_filename(label: str) -> str:
    """"lmx.render.shadowMap" -> "shadow-map.png": the last dot-separated component, with
    camelCase word boundaries hyphenated and the whole thing lowercased."""
    last_component = label.rsplit(".", 1)[-1]
    hyphenated = re.sub(r"(?<!^)(?=[A-Z])", "-", last_component)
    return f"{hyphenated.lower()}.png"


def _decode_images(join_result: bundlelib.JoinResult, out_dir: pathlib.Path) -> list:
    """Decode each matched texture's primary blob to a PNG + stats under `out_dir`.

    Mutates `join_result` in place: a resource whose primary blob raises DecodeError has ALL of
    its (resource, blob) pairs (every mip) pulled out of `matched` and replaced with a single
    matched_undecodable entry carrying the DecodeError's reason -- so the manifest's resource
    buckets stay consistent with what was actually decoded: DecodeError moves that pair's
    manifest entry to matchedUndecodable.

    decodelib.decode_texture has its own geometry sanity checks, but
    this loop also guards against anything decodelib didn't anticipate: a real capture is
    untrusted input, and a single malformed blob raising an unforeseen exception must degrade
    that one resource to matched_undecodable, not crash the CLI before manifest.json is written
    for every OTHER resource. This is a deliberate, narrow last-resort backstop -- decodelib
    should still raise DecodeError for every case it recognizes.
    """
    images = []
    undecodable_labels = set()
    for resource, primary_blob in _primary_texture_pairs(join_result):
        blob_bytes = primary_blob.path.read_bytes()
        try:
            decoded = decodelib.decode_texture(resource, blob_bytes)
        except decodelib.DecodeError as exc:
            undecodable_labels.add(resource.label)
            join_result.matched_undecodable.append((resource, str(exc)))
            continue
        except Exception as exc:  # noqa: BLE001 -- deliberate backstop, see docstring above
            undecodable_labels.add(resource.label)
            join_result.matched_undecodable.append(
                (resource, f"decode failed unexpectedly ({type(exc).__name__}: {exc})"))
            continue

        filename = _label_to_filename(resource.label)
        pnglib.write_png(out_dir / filename, decoded.width, decoded.height, decoded.png_rgb_rows)
        images.append({
            "label": resource.label,
            "file": filename,
            "format": resource.format,
            "width": decoded.width,
            "height": decoded.height,
            "stats": decoded.stats,
        })

    if undecodable_labels:
        join_result.matched = [
            (resource, blob) for resource, blob in join_result.matched
            if resource.label not in undecodable_labels
        ]
    return images


def _upload_json(upload: uniformlib.DecodedUpload) -> dict:
    return {
        "index": upload.index,
        "structName": upload.struct_name,
        "slot": upload.slot,
        "ringLabel": upload.ring_label,
        "ringOffset": upload.ring_offset,
        "values": upload.values,
        "finite": upload.finite,
    }


def _decode_uniforms(schema: schemalib.Schema, bundle: bundlelib.Bundle):
    """Resolve ring bytes via uniformlib's positional-attribution policy (bundlelib.join() cannot
    do this itself -- buffer blobs aren't label-joinable and same-size rings are ambiguous by
    design, see uniformlib's module docstring) and decode every schema-declared upload against
    them.

    Returns (uniforms_doc, manifest_note). `manifest_note` is None on the two normal paths --
    nothing to decode, or a ring was attributed and decoded cleanly -- and a short string when the
    attribution policy declined to decode anything despite recorded uploads, or a decode itself
    failed; both are still exit-0 (uniforms.json always gets a well-formed, possibly-empty
    document), but the reason is echoed into manifest.json's notes too.
    """
    resolution = uniformlib.resolve_ring_bytes(schema, bundle)
    manifest_note = None
    decoded_uploads = []
    if resolution.ring_bytes_by_label:
        try:
            decoded_uploads = uniformlib.decode_uploads(schema, resolution.ring_bytes_by_label)
        except uniformlib.UniformError as exc:
            manifest_note = f"uniform upload decode failed: {exc}"
            print(f"warning: {manifest_note}", file=sys.stderr)
    elif schema.uniform_uploads:
        # Attribution declined (ambiguous/absent ring blob) despite the schema recording
        # uploads -- worth flagging in the manifest, unlike "no uploads this capture at all".
        manifest_note = resolution.attribution
        print(f"warning: {manifest_note}", file=sys.stderr)

    uniforms_doc = {
        "version": 1,
        "matrixOrder": _MATRIX_ORDER_NOTE,
        "ringAttribution": resolution.attribution,
        "uploads": [_upload_json(u) for u in decoded_uploads],
    }
    return uniforms_doc, manifest_note


def build_manifest(bundle_path: pathlib.Path, schema_path: pathlib.Path, bundle: bundlelib.Bundle,
                    schema: schemalib.Schema, join_result: bundlelib.JoinResult,
                    images: list, uniforms_note: str | None = None,
                    uniforms_doc: dict | None = None) -> dict:
    header_version = bundlelib.first_texture_header_version(bundle)
    notes = []
    if header_version is not None and header_version != bundlelib.EXPECTED_HEADER_VERSION:
        note = (f"bundle header version {header_version:#010x} != expected "
                f"{bundlelib.EXPECTED_HEADER_VERSION:#010x} -- decoding may be unreliable")
        print(f"warning: {note}", file=sys.stderr)
        notes.append(note)
    if uniforms_note is not None:
        notes.append(uniforms_note)

    manifest = {
        "version": 1,
        "bundle": str(bundle_path),
        "schema": str(schema_path),
        "bundleHeaderVersion": header_version,
        "capture": schema.context,
        "resources": _resources_json(join_result),
        "images": images,
        "uniforms": "uniforms.json",
        "anomalies": [],
    }
    if notes:
        manifest["notes"] = notes

    # Last stage, deliberately: the checks read the finished manifest (images + stats, resource
    # buckets, capture context) rather than the intermediate objects those were built from, so
    # they see exactly what a reader of manifest.json sees and cannot quietly depend on something
    # the file doesn't record. Assigned back into the key reserved for it above so the JSON key
    # order stays stable across versions.
    manifest["anomalies"] = anomalylib.run_checks(manifest, uniforms_doc, schema)
    return manifest


def _default_schema_path(bundle: pathlib.Path) -> pathlib.Path:
    return pathlib.Path(str(bundle) + ".schema.json")


def _default_out_dir(bundle: pathlib.Path) -> pathlib.Path:
    # A sibling of the bundle, like the schema default -- independent of the caller's CWD, so
    # `python3 gputrace_dump.py /path/to/x.gputrace` from anywhere writes next to the bundle.
    return bundle.parent / f"{bundle.stem}-dump"


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("bundle", type=pathlib.Path, help="path to the .gputrace bundle directory")
    parser.add_argument("--schema", type=pathlib.Path, default=None,
                        help="path to the schema sidecar (default: <bundle>.schema.json)")
    parser.add_argument("--out", type=pathlib.Path, default=None,
                        help="output directory (default: <bundle-stem>-dump/)")
    args = parser.parse_args(argv)

    schema_path = args.schema if args.schema is not None else _default_schema_path(args.bundle)

    try:
        bundle = bundlelib.walk_bundle(args.bundle)
        schema = schemalib.load_schema(schema_path)
    except (bundlelib.BundleError, schemalib.SchemaError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2

    hits = bundlelib.scan_labels(bundle)
    join_result = bundlelib.join(bundle, schema, hits)

    out_dir = args.out if args.out is not None else _default_out_dir(args.bundle)
    out_dir.mkdir(parents=True, exist_ok=True)

    images = _decode_images(join_result, out_dir)

    uniforms_doc, uniforms_note = _decode_uniforms(schema, bundle)
    uniforms_path = out_dir / "uniforms.json"
    uniforms_path.write_text(json.dumps(uniforms_doc, indent=2) + "\n")
    print(f"wrote {uniforms_path}")

    manifest = build_manifest(args.bundle, schema_path, bundle, schema, join_result, images,
                              uniforms_note, uniforms_doc)

    manifest_path = out_dir / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"wrote {manifest_path}")

    # A one-line count on stdout, not the anomalies themselves: a tool that writes errors into a
    # file and says nothing invites them being missed, but the findings are long enough that
    # printing them would bury the "wrote ..." lines. Exit code stays 0 either way -- an anomaly
    # is a finding about the captured frame, not a failure of this tool (see the module docstring).
    counts = {severity: sum(1 for a in manifest["anomalies"] if a["severity"] == severity)
              for severity in ("error", "warning", "info")}
    print(f"anomalies: {counts['error']} error, {counts['warning']} warning, "
          f"{counts['info']} info")
    return 0


if __name__ == "__main__":
    sys.exit(main())
