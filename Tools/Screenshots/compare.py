#!/usr/bin/env python3
"""Compare frozen SDR captures with the strict gate or an explicit vendor quantization profile.

Requires Pillow (the same image reader as Tools/TemporalCompare). Reports all fifteen
matrix cases, or the three frames of a paired TemporalLab sequence, without changing
reference.json or the strict hash runner. Deltas are 8-bit encoded RGB code values.
"""
from __future__ import annotations

import argparse
import contextlib
import io
import json
import math
from pathlib import Path
import sys
import tempfile
import unittest

from parity import load_reference, sha256

THRESHOLDS = {"differingPixelsPercent": 1.0, "over8PixelsPercent": 0.05,
              "largeDeltaExclusive": 8}

VENDOR_PROFILE = "vendor-quantization-v1"
PROFILES = ("strict", VENDOR_PROFILE)
VENDOR_THRESHOLDS = {"differingPixelsPercent": 1.0, "differenceExclusive": 1,
                     "over8PixelsPercent": 0.05, "largeDeltaExclusive": 8,
                     "meanAbsoluteDelta": 0.1}


def applied_profile(profile: str, temporal: str) -> str:
    if profile not in PROFILES:
        raise ValueError(f"unknown comparison profile: {profile}")
    return VENDOR_PROFILE if profile == VENDOR_PROFILE and temporal == "metalfx" else "strict"


def image_difference(parent: Path, candidate: Path, extent: tuple[int, int],
                     profile: str = "strict") -> dict:
    from PIL import Image, ImageChops
    if profile not in PROFILES:
        raise ValueError(f"unknown comparison profile: {profile}")
    images = []
    for path in (parent, candidate):
        with Image.open(path) as image:
            if image.size != extent:
                raise ValueError(f"{path}: expected {extent[0]}x{extent[1]}, got {image.size}")
            if image.mode not in ("RGB", "RGBA"):
                raise ValueError(f"{path}: expected 8-bit RGB or RGBA, got {image.mode}")
            if image.mode == "RGBA" and image.getchannel("A").getextrema() != (255, 255):
                raise ValueError(f"{path}: expected opaque SDR capture")
            images.append(image.convert("RGB"))
    difference = ImageChops.difference(*images)
    red, green, blue = difference.split()
    maximum = ImageChops.lighter(ImageChops.lighter(red, green), blue).histogram()
    pixels = extent[0] * extent[1]
    differing = sum(maximum[1:])
    over8 = sum(maximum[9:])
    total_delta = sum((index % 256) * count for index, count in enumerate(difference.histogram()))
    threshold_differing = sum(maximum[2:]) if profile == VENDOR_PROFILE else differing
    strict_pass = differing * 100 <= pixels and over8 * 2000 <= pixels
    passed = (threshold_differing * 100 <= pixels and over8 * 2000 <= pixels
              and (profile == "strict" or total_delta * 10 <= pixels * 3))
    return {"pixels": pixels, "differingPixels": differing,
            "differingPixelsPercent": 100.0 * differing / pixels,
            "over8Pixels": over8, "over8PixelsPercent": 100.0 * over8 / pixels,
            "maxChannelDelta": max(index for index, count in enumerate(maximum) if count),
            "meanAbsoluteDelta": total_delta / (pixels * 3),
            "appliedProfile": profile, "thresholdDifferingPixels": threshold_differing,
            "thresholdDifferingPixelsPercent": 100.0 * threshold_differing / pixels,
            "strictPass": strict_pass,
            "parentSha256": sha256(parent), "candidateSha256": sha256(candidate),
            # Integer cross-products pin inclusive boundaries without floating-point ambiguity.
            "pass": passed}


def sequence_inputs(parent: Path, candidate: Path) -> tuple[list[dict], dict, tuple[int, int]]:
    manifests = []
    for root in (parent, candidate):
        data = json.loads((root / "manifest.json").read_text())
        if data.get("schemaVersion") != 2 or data.get("complete") is not True or data.get("failure"):
            raise ValueError(f"{root}: incomplete or unsupported manifest")
        if data.get("scene") != "temporal-lab" or data.get("frameCount") != 3 or len(data.get("frames", [])) != 3:
            raise ValueError(f"{root}: expected exactly three TemporalLab frames")
        if (data.get("width"), data.get("height"), data.get("fps")) != (1280, 720, 60):
            raise ValueError(f"{root}: expected 1280x720 at 60 Hz")
        if (data.get("container") not in ("png", "bmp") or data.get("debugView") != 0
                or data.get("dynamicResolution") is not False or data.get("ui") != {"composited": False}
                or data.get("display", {}).get("view") != "sdr"
                or data.get("display", {}).get("transfer") != "srgb"):
            raise ValueError(f"{root}: expected fixed-resolution offscreen SDR output")
        filenames = set()
        for index, frame in enumerate(data["frames"]):
            filename = frame.get("file", "")
            if (not filename or Path(filename).name != filename or filename in filenames
                    or Path(filename).suffix != "." + data["container"]):
                raise ValueError(f"{root}: invalid or duplicate frame filename")
            filenames.add(filename)
            if (frame.get("ordinal") != index or frame.get("simulationFrame") != data["warmup"] + index
                    or not math.isclose(frame["timeSeconds"], frame["simulationFrame"] / 60, abs_tol=1e-12)):
                raise ValueError(f"{root}: frame numbering/time mismatch")
            if frame.get("fallback") != 0 or frame.get("effectiveMode") != data["requestedMode"]:
                raise ValueError(f"{root}: fallback or substituted reconstruction refused")
        manifests.append(data)
    # Parent and candidate run exactly the same schedule, including camera, reset state and domain.
    def settings(manifest: dict) -> dict:
        return {**manifest, "frames": [{k: v for k, v in frame.items() if k != "file"}
                                         for frame in manifest["frames"]]}
    if settings(manifests[0]) != settings(manifests[1]):
        raise ValueError("paired sequence settings or per-frame state differ")
    pairs = [{"name": f"frame-{index:06}", "parent": a["file"], "candidate": b["file"],
              "temporal": manifests[0]["requestedMode"]}
             for index, (a, b) in enumerate(zip(manifests[0]["frames"], manifests[1]["frames"]))]
    provenance = {"parentManifestSha256": sha256(parent / "manifest.json"),
                  "candidateManifestSha256": sha256(candidate / "manifest.json"),
                  "parentManifest": manifests[0], "candidateManifest": manifests[1]}
    return pairs, provenance, (1280, 720)


def compare(parent: Path, candidate: Path, output: Path, sequence: bool = False,
            profile: str = "strict") -> bool:
    applied_profile(profile, "off")  # Reject unknown profiles before creating evidence.
    parent, candidate, output = parent.resolve(), candidate.resolve(), output.resolve()
    if parent == candidate:
        raise ValueError("parent and candidate directories must be distinct")
    if output.exists():
        raise ValueError("report path must be new; retained comparison evidence is never overwritten")
    if sequence:
        pairs, provenance, extent = sequence_inputs(parent, candidate)
    else:
        reference_path = Path(__file__).with_name("reference.json")
        reference = load_reference(reference_path)
        pairs = [{"name": row["name"], "parent": row["name"] + ".bmp",
                  "candidate": row["name"] + ".bmp", "temporal": row["temporal"]} for row in reference["images"]]
        provenance = {"referenceSha256": sha256(reference_path)}
        extent = (1280, 720)
    report = {"schemaVersion": 1, "kind": "sequence" if sequence else "matrix",
              "parent": str(parent), "candidate": str(candidate), "thresholds": THRESHOLDS,
              "comparisonProfile": profile, "vendorThresholds": VENDOR_THRESHOLDS if profile == VENDOR_PROFILE else None,
              "comparatorSha256": sha256(Path(__file__)),
              "units": "8-bit encoded RGB channel values; mean over all pixels and three channels",
              **provenance, "images": [], "complete": False, "allPassed": False}
    output.parent.mkdir(parents=True, exist_ok=True)
    for pair in pairs:
        effective_profile = applied_profile(profile, pair["temporal"])
        record = {**pair, "appliedProfile": effective_profile, "pass": False}
        try:
            record.update(image_difference(parent / pair["parent"], candidate / pair["candidate"], extent, effective_profile))
        except (OSError, ValueError) as error:
            record["error"] = str(error)
        report["images"].append(record)
        output.write_text(json.dumps(report, indent=2, allow_nan=False) + "\n")
        result = (record.get("error") or
                  f"different={record['differingPixelsPercent']:.6f}% "
                  f"counted={record['thresholdDifferingPixelsPercent']:.6f}% "
                  f"over8={record['over8PixelsPercent']:.6f}% max={record['maxChannelDelta']} "
                  f"mean={record['meanAbsoluteDelta']:.9f}")
        print(f"{'PASS' if record['pass'] else 'FAIL'} {pair['name']} [{effective_profile}]: {result}", flush=True)
    report["complete"] = True
    report["allPassed"] = all(row["pass"] for row in report["images"])
    output.write_text(json.dumps(report, indent=2, allow_nan=False) + "\n")
    print(f"{sum(row['pass'] for row in report['images'])}/{len(pairs)} passed; report: {output}")
    return report["allPassed"]


class ComparisonTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.parent = self.root / "parent.png"
        self.candidate = self.root / "candidate.png"

    def difference(self, values: list[int], count: int = 2000) -> dict:
        from PIL import Image
        parent = Image.new("RGB", (count, 1))
        candidate = parent.copy()
        for index, value in enumerate(values):
            candidate.putpixel((index, 0), (value, 0, 0))
        parent.save(self.parent)
        candidate.save(self.candidate)
        return image_difference(self.parent, self.candidate, (count, 1))

    def test_equal(self):
        result = self.difference([])
        self.assertTrue(result["pass"])
        self.assertEqual(result["maxChannelDelta"], 0)
        self.assertEqual(result["meanAbsoluteDelta"], 0)

    def test_inclusive_boundaries_and_exclusive_eight(self):
        result = self.difference([9] + [8] * 19)
        self.assertTrue(result["pass"])
        self.assertEqual(result["differingPixelsPercent"], 1.0)
        self.assertEqual(result["over8PixelsPercent"], 0.05)
        self.assertEqual(result["maxChannelDelta"], 9)
        self.assertEqual(result["meanAbsoluteDelta"], 161 / 6000)
        self.assertFalse(self.difference([8] * 21)["pass"])
        self.assertFalse(self.difference([9, 9])["pass"])

    def test_channel_direction_and_pixel_count(self):
        from PIL import Image
        Image.new("RGB", (1, 1), (250, 2, 30)).save(self.parent)
        Image.new("RGB", (1, 1), (1, 12, 22)).save(self.candidate)
        result = image_difference(self.parent, self.candidate, (1, 1))
        self.assertEqual(result["differingPixels"], 1)
        self.assertEqual(result["over8Pixels"], 1)
        self.assertEqual(result["maxChannelDelta"], 249)
        self.assertEqual(result["meanAbsoluteDelta"], 267 / 3)

    def vendor_difference(self, values: list[int], count: int = 2000) -> dict:
        self.difference(values, count)
        return image_difference(self.parent, self.candidate, (count, 1), VENDOR_PROFILE)

    def test_vendor_quantization_preserves_raw_and_strict_results(self):
        result = self.vendor_difference([1] * 600)
        self.assertTrue(result["pass"])
        self.assertFalse(result["strictPass"])
        self.assertEqual(result["differingPixels"], 600)
        self.assertEqual(result["thresholdDifferingPixels"], 0)
        self.assertEqual(result["meanAbsoluteDelta"], 0.1)
        self.assertFalse(self.vendor_difference([1] * 601)["pass"])

    def test_vendor_inclusive_area_and_large_error_boundaries(self):
        self.assertTrue(self.vendor_difference([2] * 20)["pass"])
        self.assertFalse(self.vendor_difference([2] * 21)["pass"])
        self.assertTrue(self.vendor_difference([9] + [8] * 19)["pass"])
        self.assertFalse(self.vendor_difference([9, 9])["pass"])

    def test_vendor_rejects_synthetic_brightness_shift_and_bad_block(self):
        from PIL import Image, ImageChops, ImageDraw
        parent = Image.new("RGB", (200, 100), (64, 96, 128))
        draw = ImageDraw.Draw(parent)
        for x in range(0, 200, 4):
            draw.rectangle((x, 0, x + 1, 99), fill=(144, 48, 80))
        parent.save(self.parent)
        bad_block = parent.copy()
        ImageDraw.Draw(bad_block).rectangle((80, 30, 111, 61), fill=(255, 255, 255))
        shifted = parent.copy()
        shifted.paste(parent.crop((0, 0, 199, 100)), (1, 0))
        examples = {"full+1": ImageChops.add(parent, Image.new("RGB", parent.size, (1, 1, 1))),
                    "full+4": ImageChops.add(parent, Image.new("RGB", parent.size, (4, 4, 4))),
                    "shift1px": shifted, "badblock32x32": bad_block}
        for name, candidate in examples.items():
            with self.subTest(name=name):
                candidate.save(self.candidate)
                result = image_difference(self.parent, self.candidate, parent.size, VENDOR_PROFILE)
                self.assertFalse(result["pass"])
                if name == "full+1":
                    self.assertEqual(result["thresholdDifferingPixels"], 0)
                    self.assertEqual(result["meanAbsoluteDelta"], 1)
                if name == "badblock32x32":
                    self.assertGreater(result["over8PixelsPercent"], 0.05)

    def test_profile_uses_reference_mode_and_rejects_unknown_profile(self):
        output = self.root / "profile-matrix.json"
        with contextlib.redirect_stdout(io.StringIO()):
            compare(self.root / "a", self.root / "b", output, profile=VENDOR_PROFILE)
        report = json.loads(output.read_text())
        self.assertEqual(report["comparisonProfile"], VENDOR_PROFILE)
        self.assertEqual(report["thresholds"], THRESHOLDS)
        self.assertEqual(report["vendorThresholds"], VENDOR_THRESHOLDS)
        self.assertEqual(sum(row["appliedProfile"] == VENDOR_PROFILE for row in report["images"]), 6)
        for row in report["images"]:
            self.assertEqual(row["appliedProfile"], VENDOR_PROFILE if row["temporal"] == "metalfx" else "strict")
        self.assertEqual(applied_profile(VENDOR_PROFILE, "raw"), "strict")
        self.assertEqual(applied_profile("strict", "metalfx"), "strict")
        with self.assertRaisesRegex(ValueError, "unknown comparison profile"):
            compare(self.root / "a", self.root / "b", self.root / "unknown.json", profile="unknown")
        self.assertFalse((self.root / "unknown.json").exists())

    def test_malformed_extent_and_alpha_refused(self):
        from PIL import Image
        self.difference([])
        with self.assertRaisesRegex(ValueError, "expected 1x1"):
            image_difference(self.parent, self.candidate, (1, 1))
        Image.new("RGBA", (2000, 1), (0, 0, 0, 254)).save(self.candidate)
        with self.assertRaisesRegex(ValueError, "opaque"):
            image_difference(self.parent, self.candidate, (2000, 1))
        self.candidate.write_bytes(b"broken image")
        with self.assertRaises(OSError):
            image_difference(self.parent, self.candidate, (2000, 1))

    def test_missing_matrix_reports_every_case_and_preserves_report(self):
        output = self.root / "matrix.json"
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertFalse(compare(self.root / "a", self.root / "b", output))
        report = json.loads(output.read_text())
        self.assertTrue(report["complete"])
        self.assertFalse(report["allPassed"])
        self.assertEqual(len(report["images"]), 15)
        self.assertTrue(all("error" in row for row in report["images"]))
        with self.assertRaisesRegex(ValueError, "must be new"):
            compare(self.root / "a", self.root / "b", output)

    def manifest(self) -> dict:
        return {"schemaVersion": 2, "complete": True, "failure": "", "scene": "temporal-lab",
                "frameCount": 3, "width": 1280, "height": 720, "fps": 60, "warmup": 32,
                "container": "png", "debugView": 0, "dynamicResolution": False,
                "ui": {"composited": False}, "display": {"view": "sdr", "transfer": "srgb"},
                "requestedMode": "taa", "frames": [
                    {"ordinal": i, "simulationFrame": 32 + i, "timeSeconds": (32 + i) / 60,
                     "file": f"frame-{i:06}.png", "fallback": 0, "effectiveMode": "taa"}
                    for i in range(3)]}

    def test_sequence_manifest_alignment_fallback_count_and_filenames(self):
        a, b = self.root / "a", self.root / "b"
        a.mkdir()
        b.mkdir()
        original = self.manifest()
        (a / "manifest.json").write_text(json.dumps(original))
        (b / "manifest.json").write_text(json.dumps(original))
        pairs, provenance, extent = sequence_inputs(a, b)
        self.assertEqual(len(pairs), 3)
        self.assertEqual(extent, (1280, 720))
        self.assertEqual(provenance["parentManifest"], original)
        self.assertTrue(all(pair["temporal"] == "taa" for pair in pairs))
        vendor = self.manifest()
        vendor["requestedMode"] = "metalfx"
        for frame in vendor["frames"]:
            frame["effectiveMode"] = "metalfx"
        for root in (a, b):
            (root / "manifest.json").write_text(json.dumps(vendor))
        vendor_pairs, _, _ = sequence_inputs(a, b)
        self.assertTrue(all(applied_profile(VENDOR_PROFILE, pair["temporal"]) == VENDOR_PROFILE
                            for pair in vendor_pairs))
        (a / "manifest.json").write_text(json.dumps(original))
        for mutate in (lambda x: x.update(frameCount=2),
                       lambda x: x.update(complete=False),
                       lambda x: x["frames"][0].update(fallback=1),
                       lambda x: x["frames"][0].update(file="../outside.png"),
                       lambda x: x["frames"][1].update(file="frame-000000.png"),
                       lambda x: x["frames"][0].update(timeSeconds=0),
                       lambda x: x["frames"][0].update(camera={"yaw": 1})):
            data = self.manifest()
            mutate(data)
            (b / "manifest.json").write_text(json.dumps(data))
            with self.assertRaises(ValueError):
                sequence_inputs(a, b)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--parent", type=Path)
    parser.add_argument("--candidate", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--sequence", action="store_true", help="Compare exactly three TemporalLab frames and their manifests")
    parser.add_argument("--profile", choices=PROFILES, default="strict",
                        help="Opt in to vendor-only one-code-value tolerance plus an RGB mean-error budget")
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    if args.selftest:
        result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(ComparisonTests))
        return 0 if result.wasSuccessful() else 1
    if None in (args.parent, args.candidate, args.output):
        parser.error("--parent, --candidate and --output are required unless --selftest is used")
    try:
        return 0 if compare(args.parent, args.candidate, args.output, args.sequence, args.profile) else 1
    except (OSError, ValueError, KeyError, TypeError, ImportError) as error:
        print(f"comparison refused: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
