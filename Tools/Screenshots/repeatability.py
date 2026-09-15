#!/usr/bin/env python3
"""Freeze and verify the visibility-null-envelope-v2 calibration protocol.

Each of the fifteen cases supplies nine unculled BMP calibration images (one legacy
anchor followed by eight fresh captures). Per-pixel RGB minima/maxima have no
slack. Span and mixed-pair engineering ceilings use the unchanged vendor-quantization-v1
profile for every MetalFX case and strict for Native TAA and temporal Off. Original
strict results remain visible. Temporal Off must have an exactly zero span. Four
independent held-out images use Off/Cull/Cull/Off or its reverse; every channel must
remain inside the frozen interval and both adjacent mixed pairs must meet their
mode's engineering ceiling. Temporal Off pairs also require identical BMP file hashes.
These numeric budgets are inherited engineering choices, not mathematically derived
tolerances; the unchanged zero-slack invariant and independent controls constrain them. This verifier never renders, changes a
reference, widens a bound, or infers settings/provenance from filenames. The caller
attests mode, fresh-process independence, shader/device identity and case settings;
optional settings objects are checked, but image bytes cannot prove that attestation.
Raw originals remain available and unchanged for verification. Equal content
hashes are expected for exact repeats; reusing an input path is forbidden.
"""
from __future__ import annotations

import argparse
import hashlib
import io
import json
import struct
from pathlib import Path
import sys

from PIL import Image, ImageChops
from compare import image_difference, applied_profile, THRESHOLDS, VENDOR_PROFILE, VENDOR_THRESHOLDS
from parity import load_reference, sha256

PROFILE = "visibility-null-envelope-v2"
REFERENCE = Path(__file__).with_name("reference.json")
EXTENT = (1280, 720)
SETTINGS_KEYS = ("scene", "temporal", "renderScale")


def tool_hashes() -> dict:
    return {"verifierSha256": sha256(Path(__file__)),
            "comparatorSha256": sha256(Path(__file__).with_name("compare.py")),
            "referenceReaderSha256": sha256(Path(__file__).with_name("parity.py"))}


def variable_channels(low: Image.Image, high: Image.Image) -> int:
    return sum(sum(channel.histogram()[1:]) for channel in ImageChops.difference(low, high).split())


def ceiling_difference(a: Path, b: Path, extent: tuple[int, int], temporal: str) -> dict:
    return image_difference(a, b, extent, applied_profile(VENDOR_PROFILE, temporal))


def case_settings(case: dict) -> dict:
    return {**{key: case[key] for key in SETTINGS_KEYS}, "width": 1280, "height": 720, "frames": 32}


def checked_reference(path: Path) -> tuple[dict, str]:
    before = sha256(path)
    try:
        reference = load_reference(path)
    except (KeyError, TypeError, AttributeError) as error:
        raise ValueError("malformed reference schema") from error
    if sha256(path) != before:
        raise ValueError("reference changed while being read")
    return reference, before


def input_path(value: str) -> Path:
    if not isinstance(value, str) or not Path(value).is_absolute():
        raise ValueError("input image paths must be absolute strings")
    path = Path(value).resolve(strict=True)
    if not path.is_file():
        raise ValueError("input image must be a regular file")
    return path


def validate_cases(items: list[dict], reference: dict, holdout: bool = False,
                   forbidden: set[Path] | None = None) -> dict[str, dict]:
    expected = {row["name"]: row for row in reference["images"]}
    if not isinstance(items, list) or len(items) != 15:
        raise ValueError("exactly fifteen distinct cases are required")
    result, used = {}, {Path(path).resolve() for path in (forbidden or ())}
    for item in items:
        if not isinstance(item, dict) or set(item) - {"name", "inputs", "settings"}:
            raise ValueError("malformed case schema")
        name = item.get("name")
        if not isinstance(name, str) or name not in expected or name in result:
            raise ValueError("missing, unknown or duplicate case")
        settings = case_settings(expected[name])
        if "settings" in item and item["settings"] != settings:
            raise ValueError(f"{name}: settings differ from the frozen reference")
        inputs = item.get("inputs")
        if not isinstance(inputs, list) or len(inputs) != (4 if holdout else 9):
            raise ValueError(f"{name}: expected {'four held-out' if holdout else 'nine calibration'} inputs")
        parsed = []
        for entry in inputs:
            if holdout:
                if not isinstance(entry, dict) or set(entry) != {"mode", "path"}:
                    raise ValueError(f"{name}: malformed held-out input")
                if entry["mode"] not in ("off", "cull"):
                    raise ValueError(f"{name}: unsupported visibility mode")
                path = input_path(entry["path"])
                record = {"path": str(path), "mode": entry["mode"]}
            else:
                path = input_path(entry)
                record = {"path": str(path), "mode": "off"}
            if path in used:
                raise ValueError("calibration and held-out inputs must use distinct paths")
            used.add(path)
            parsed.append(record)
        if holdout and [row["mode"] for row in parsed] not in (
                ["off", "cull", "cull", "off"], ["cull", "off", "off", "cull"]):
            raise ValueError(f"{name}: held-out order must be ABBA or BAAB")
        result[name] = {"name": name, "settings": settings, "inputs": parsed}
    if set(result) != set(expected):
        raise ValueError("missing case")
    return result


def read_image(path: Path, extent: tuple[int, int] = EXTENT) -> tuple[Image.Image, str, str]:
    data = path.read_bytes()
    # Luminex BI_RGB32 captures store opaque BGRA bytes even though Pillow exposes them as
    # RGB and discards byte four. Inspect those source alpha bytes before that conversion.
    if data[:2] == b"BM" and len(data) >= 54:
        header_size = struct.unpack_from("<I", data, 14)[0]
        width, height, planes, bits, compression = struct.unpack_from("<iiHHI", data, 18)
        if header_size >= 40 and bits == 32 and compression == 0:
            pixel_offset = struct.unpack_from("<I", data, 10)[0]
            pixel_end = pixel_offset + width * abs(height) * 4
            if width <= 0 or height == 0 or planes != 1 or pixel_offset < 14 + header_size or pixel_end > len(data):
                raise ValueError(f"{path}: malformed 32-bit BMP pixels")
            alpha = data[pixel_offset + 3:pixel_end:4]
            if alpha.count(255) != len(alpha):
                raise ValueError(f"{path}: expected opaque BGRA capture bytes")
    with Image.open(io.BytesIO(data)) as image:
        if image.size != extent:
            raise ValueError(f"{path}: wrong extent {image.size}; expected {extent}")
        if image.mode not in ("RGB", "RGBA"):
            raise ValueError(f"{path}: expected 8-bit RGB or RGBA")
        if image.mode == "RGBA" and image.getchannel("A").getextrema() != (255, 255):
            raise ValueError(f"{path}: expected opaque capture")
        return image.convert("RGB"), image.format, hashlib.sha256(data).hexdigest()


def extrema(images: list[Image.Image]) -> tuple[Image.Image, Image.Image]:
    if not images:
        raise ValueError("empty calibration")
    low, high = images[0].copy(), images[0].copy()
    for image in images[1:]:
        if image.mode != "RGB" or image.size != low.size:
            raise ValueError("calibration images must share RGB mode and extent")
        low, high = ImageChops.darker(low, image), ImageChops.lighter(high, image)
    return low, high


def interval_difference(image: Image.Image, low: Image.Image, high: Image.Image) -> dict:
    if any(value.mode != "RGB" or value.size != image.size for value in (image, low, high)):
        raise ValueError("interval images must share RGB mode and extent")
    if ImageChops.subtract(low, high).getbbox() is not None:
        raise ValueError("inverted calibration interval")
    excess = ImageChops.lighter(ImageChops.subtract(low, image), ImageChops.subtract(image, high))
    channels = excess.split()
    maximum = ImageChops.lighter(ImageChops.lighter(channels[0], channels[1]), channels[2]).histogram()
    outside_channels = sum(sum(channel.histogram()[1:]) for channel in channels)
    return {"outsidePixels": sum(maximum[1:]), "outsideChannels": outside_channels,
            "maxExcess": max(index for index, count in enumerate(maximum) if count),
            "inside": outside_channels == 0}


def save_png(image: Image.Image, path: Path) -> dict:
    with path.open("xb") as stream:
        image.save(stream, format="PNG")
    return {"path": str(path), "sha256": sha256(path)}


def write_new_json(path: Path, report: dict) -> None:
    with path.open("x") as stream:
        stream.write(json.dumps(report, indent=2, allow_nan=False) + "\n")


def freeze(calibration: list[dict], output: Path, reference_path: Path = REFERENCE) -> dict:
    """Write a new envelope directory and return its report, including invalid calibration spans."""
    output, reference_path = Path(output).resolve(), Path(reference_path).resolve()
    if output.exists():
        raise ValueError("freeze directory must be new; evidence is never overwritten")
    reference, reference_hash = checked_reference(reference_path)
    cases = validate_cases(calibration, reference)
    tools = tool_hashes()
    output.mkdir(parents=True, exist_ok=False)
    report = {"schemaVersion": 1, "profile": PROFILE, "referencePath": str(reference_path),
              "referenceSha256": reference_hash, "thresholds": THRESHOLDS,
              "ceilingProfile": VENDOR_PROFILE, "vendorThresholds": VENDOR_THRESHOLDS, **tools,
              "calibrationCount": 9, "freshCalibrationCount": 8,
              "provenance": "caller-attested unculled legacy anchor then eight fresh captures; no filename inference",
              "images": [], "complete": True, "allValid": False}
    for case in reference["images"]:
        row = cases[case["name"]]
        images = []
        for record in row["inputs"]:
            image, container, digest = read_image(Path(record["path"]))
            if container != "BMP":
                raise ValueError("calibration inputs must be BMP")
            images.append(image)
            record.update(sha256=digest, container=container)
        low, high = extrema(images)
        row["minimum"] = save_png(low, output / (row["name"] + ".min.png"))
        row["maximum"] = save_png(high, output / (row["name"] + ".max.png"))
        row["strictSpan"] = image_difference(Path(row["minimum"]["path"]), Path(row["maximum"]["path"]), EXTENT, "strict")
        row["ceilingSpan"] = ceiling_difference(Path(row["minimum"]["path"]), Path(row["maximum"]["path"]), EXTENT, case["temporal"])
        row["ceilingPass"] = row["ceilingSpan"]["pass"]
        row["variableChannels"] = variable_channels(low, high)
        row["valid"] = row["ceilingPass"] and (case["temporal"] != "off" or row["strictSpan"]["differingPixels"] == 0)
        report["images"].append(row)
    if sha256(reference_path) != reference_hash:
        raise ValueError("reference changed during calibration")
    if tool_hashes() != tools:
        raise ValueError("verifier tools changed during calibration")
    for row in report["images"]:
        for record in row["inputs"]:
            if sha256(Path(record["path"])) != record["sha256"]:
                raise ValueError("calibration input changed during freeze")
    report["allValid"] = all(row["valid"] for row in report["images"])
    write_new_json(output / "envelope.json", report)
    return report


def load_frozen(path: Path) -> tuple[dict, dict, set[Path]]:
    """Recheck reference, original input hashes, envelope hashes/content, settings and strict spans."""
    report = json.loads(path.read_text())
    if not isinstance(report, dict) or type(report.get("schemaVersion")) is not int or report["schemaVersion"] != 1:
        raise ValueError("unsupported frozen schema")
    if report.get("profile") != PROFILE:
        raise ValueError("unsupported frozen profile")
    if report.get("complete") is not True:
        raise ValueError("incomplete frozen calibration")
    if any(report.get(key) != value for key, value in tool_hashes().items()):
        raise ValueError("frozen verifier or comparator hash changed")
    if report.get("ceilingProfile") != VENDOR_PROFILE or report.get("vendorThresholds") != VENDOR_THRESHOLDS:
        raise ValueError("changed engineering ceiling profile or thresholds")
    if report.get("allValid") is not True or report.get("thresholds") != THRESHOLDS:
        raise ValueError("invalid calibration or changed thresholds")
    if report.get("calibrationCount") != 9 or report.get("freshCalibrationCount") != 8:
        raise ValueError("calibration plan changed")
    reference_path = input_path(report.get("referencePath"))
    reference, digest = checked_reference(reference_path)
    if digest != report.get("referenceSha256"):
        raise ValueError("frozen reference hash changed")
    rows = report.get("images")
    if not isinstance(rows, list) or len(rows) != 15:
        raise ValueError("frozen envelope must contain fifteen cases")
    calibration = []
    for row in rows:
        if not isinstance(row, dict) or not isinstance(row.get("inputs"), list):
            raise ValueError("malformed frozen case")
        records = row["inputs"]
        if any(not isinstance(entry, dict) or entry.get("mode") != "off" for entry in records):
            raise ValueError("calibration must be unculled")
        calibration.append({"name": row.get("name"), "settings": row.get("settings"),
                            "inputs": [entry.get("path") for entry in records]})
    validate_cases(calibration, reference)
    used = set()
    for row in rows:
        images = []
        for record in row["inputs"]:
            source = input_path(record["path"])
            image, container, actual = read_image(source)
            if container != "BMP" or actual != record.get("sha256") or container != record.get("container"):
                raise ValueError("calibration input changed")
            used.add(source)
            images.append(image)
        derived = extrema(images)
        bounds = []
        for key, expected in zip(("minimum", "maximum"), derived):
            value = row.get(key)
            if not isinstance(value, dict):
                raise ValueError("malformed frozen bound")
            source = input_path(value.get("path"))
            if source.parent != path.parent.resolve() or source.name != row["name"] + (".min.png" if key == "minimum" else ".max.png"):
                raise ValueError("unexpected frozen bound path")
            image, container, actual = read_image(source)
            if container != "PNG" or actual != value.get("sha256"):
                raise ValueError("frozen envelope hash changed")
            if ImageChops.difference(image, expected).getbbox() is not None:
                raise ValueError("frozen envelope differs from calibration extrema")
            bounds.append(source)
        if row.get("variableChannels") != variable_channels(*derived):
            raise ValueError("frozen variable-channel count changed")
        span = image_difference(*bounds, EXTENT, "strict")
        if span != row.get("strictSpan"):
            raise ValueError("frozen strict span changed")
        ceiling = ceiling_difference(*bounds, EXTENT, row["settings"]["temporal"])
        if ceiling != row.get("ceilingSpan") or row.get("ceilingPass") is not True or not ceiling["pass"] or row.get("valid") is not True:
            raise ValueError("frozen engineering ceiling changed or invalid")
        if row["settings"]["temporal"] == "off" and span["differingPixels"]:
            raise ValueError("temporal Off calibration must be exact")
    return report, reference, used


def verify(frozen: Path, holdout: list[dict], output: Path) -> dict:
    """Write a new report for held-out images; return allInside/allPairsWithinCeiling/allPassed decisions."""
    frozen, output = Path(frozen).resolve(), Path(output).resolve()
    if output.exists():
        raise ValueError("verification report must be new; evidence is never overwritten")
    frozen_hash = sha256(frozen)
    envelope, reference, used = load_frozen(frozen)
    cases = validate_cases(holdout, reference, holdout=True, forbidden=used)
    report = {"schemaVersion": 1, "profile": PROFILE, "frozenPath": str(frozen),
              "frozenSha256": frozen_hash, "referenceSha256": envelope["referenceSha256"], **tool_hashes(),
              "thresholds": THRESHOLDS, "ceilingProfile": VENDOR_PROFILE,
              "vendorThresholds": VENDOR_THRESHOLDS, "images": [], "allInside": False,
              "allPairsStrict": False, "allPairsWithinCeiling": False, "allTemporalOffExact": False, "allPassed": False, "complete": True,
              "provenance": "caller-attested held-out modes and settings; no filename inference"}
    for bound in envelope["images"]:
        row = cases[bound["name"]]
        loaded_bounds = [read_image(Path(bound[key]["path"])) for key in ("minimum", "maximum")]
        if any(value[2] != bound[key]["sha256"] for key, value in zip(("minimum", "maximum"), loaded_bounds)):
            raise ValueError("frozen envelope changed during verification")
        low, high = [value[0] for value in loaded_bounds]
        for record in row["inputs"]:
            image, container, digest = read_image(Path(record["path"]))
            if container != "BMP":
                raise ValueError("held-out inputs must be BMP")
            record.update(sha256=digest, container=container, **interval_difference(image, low, high))
        row["pairs"] = []
        for a, b in ((0, 1), (2, 3)):
            pair = image_difference(Path(row["inputs"][a]["path"]), Path(row["inputs"][b]["path"]), EXTENT, "strict")
            if pair["parentSha256"] != row["inputs"][a]["sha256"] or pair["candidateSha256"] != row["inputs"][b]["sha256"]:
                raise ValueError("held-out input changed during comparison")
            ceiling = ceiling_difference(Path(row["inputs"][a]["path"]), Path(row["inputs"][b]["path"]), EXTENT, row["settings"]["temporal"])
            if ceiling["parentSha256"] != pair["parentSha256"] or ceiling["candidateSha256"] != pair["candidateSha256"]:
                raise ValueError("held-out input changed during ceiling comparison")
            pair.update(ceiling=ceiling, ceilingPass=ceiling["pass"], indices=[a, b], modes=[row["inputs"][a]["mode"], row["inputs"][b]["mode"]],
                        bmpHashesExact=row["inputs"][a]["sha256"] == row["inputs"][b]["sha256"])
            row["pairs"].append(pair)
        row["allInside"] = all(record["inside"] for record in row["inputs"])
        row["allPairsStrict"] = all(pair["pass"] for pair in row["pairs"])
        row["allPairsWithinCeiling"] = all(pair["ceilingPass"] for pair in row["pairs"])
        row["temporalOffExact"] = row["settings"]["temporal"] != "off" or all(pair["bmpHashesExact"] for pair in row["pairs"])
        row["pass"] = row["allInside"] and row["allPairsWithinCeiling"] and row["temporalOffExact"]
        report["images"].append(row)
    if sha256(frozen) != frozen_hash or sha256(Path(envelope["referencePath"])) != envelope["referenceSha256"]:
        raise ValueError("frozen reference or envelope changed during verification")
    if any(envelope.get(key) != value for key, value in tool_hashes().items()):
        raise ValueError("verifier tools changed during verification")
    for row in envelope["images"] + report["images"]:
        for record in row["inputs"]:
            if sha256(Path(record["path"])) != record["sha256"]:
                raise ValueError("input changed during verification")
    report["allInside"] = all(row["allInside"] for row in report["images"])
    report["allPairsStrict"] = all(row["allPairsStrict"] for row in report["images"])
    report["allPairsWithinCeiling"] = all(row["allPairsWithinCeiling"] for row in report["images"])
    report["allTemporalOffExact"] = all(row["temporalOffExact"] for row in report["images"])
    report["allPassed"] = all(row["pass"] for row in report["images"])
    output.parent.mkdir(parents=True, exist_ok=True)
    write_new_json(output, report)
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--calibration", type=Path)
    parser.add_argument("--freeze-dir", type=Path)
    parser.add_argument("--reference", type=Path, default=REFERENCE)
    parser.add_argument("--frozen", type=Path)
    parser.add_argument("--holdout", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    if args.selftest:
        import unittest
        from test_repeatability import RepeatabilityTests
        result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(RepeatabilityTests))
        return 0 if result.wasSuccessful() else 1
    try:
        if args.calibration and args.freeze_dir and not any((args.frozen, args.holdout, args.output)):
            return 0 if freeze(json.loads(args.calibration.read_text()), args.freeze_dir, args.reference)["allValid"] else 1
        if args.frozen and args.holdout and args.output and not any((args.calibration, args.freeze_dir)):
            return 0 if verify(args.frozen, json.loads(args.holdout.read_text()), args.output)["allPassed"] else 1
        parser.error("choose --calibration JSON --freeze-dir DIR, or --frozen JSON --holdout JSON --output JSON")
    except (OSError, ValueError, TypeError, KeyError) as error:
        print(f"repeatability refused: {error}", file=sys.stderr)
        return 1
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
