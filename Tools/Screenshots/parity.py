#!/usr/bin/env python3
"""Render fixed SDR BMP references and report strict SHA-256 parity without updating them."""
from __future__ import annotations

import argparse
import hashlib
import contextlib
import io
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_reference(path: Path) -> dict:
    reference = json.loads(path.read_text())
    if reference.get("schemaVersion") != 1:
        raise ValueError("unsupported reference schema")
    provenance = reference["provenance"]
    if (provenance["width"], provenance["height"], provenance["frames"], provenance["container"]) != (1280, 720, 32, "bmp"):
        raise ValueError("reference must retain 1280x720, 32-frame BMP captures")
    images = reference["images"]
    expected = {(scene, mode, scale) for scene in ("sponza", "damaged-helmet", "material-lab")
                for mode, scale in (("off", 1), ("taa", 1), ("taa", 0.5), ("metalfx", 1), ("metalfx", 0.5))}
    actual = {(row["scene"], row["temporal"], row["renderScale"]) for row in images}
    if len(images) != 15 or actual != expected or len({row["name"] for row in images}) != 15:
        raise ValueError("reference must contain all fifteen distinct scene/mode/scale cases")
    for row in images:
        if not re.fullmatch(r"[a-z0-9.-]+", row["name"]) or not re.fullmatch(r"[0-9a-f]{64}", row["sha256"]):
            raise ValueError("invalid reference name or SHA-256")
    return reference


def command_for(app: Path, image: dict, output: Path) -> list[str]:
    return [str(app), "--scene", image["scene"], "--temporal", image["temporal"],
            "--render-scale", format(image["renderScale"], "g"), "--frames", "32",
            "--screenshot", str(output)]


def inspect_image(path: Path, expected_hash: str) -> dict:
    with path.open("rb") as stream:
        header = stream.read(26)
    if len(header) < 26 or header[:2] != b"BM":
        raise ValueError(f"{path.name}: invalid BMP")
    width, height = struct.unpack_from("<ii", header, 18)
    if (width, abs(height)) != (1280, 720):
        raise ValueError(f"{path.name}: expected 1280x720, got {width}x{height}")
    actual = sha256(path)
    return {"actualSha256": actual, "expectedSha256": expected_hash, "match": actual == expected_hash}


def shader_hashes(app: Path) -> dict:
    result = {p.name: sha256(p) for p in sorted((app.parent / "Shaders").glob("*")) if p.is_file()}
    if not result:
        raise ValueError("no runtime shaders found beside App")
    return result


def run(app: Path, output: Path, reference_path: Path) -> bool:
    reference = load_reference(reference_path)
    app, output = app.resolve(), output.resolve()
    if not app.is_file():
        raise ValueError(f"App binary missing: {app}")
    if output.exists() and any(output.iterdir()):
        raise ValueError("output directory must be new or empty")
    app_hash, shaders = sha256(app), shader_hashes(app)
    output.mkdir(parents=True, exist_ok=True)
    environment = os.environ.copy()
    environment.pop("LMX_SCREENSHOT_NO_BLOOM", None)
    environment["MTL_DEBUG_LAYER"] = "1"
    report = {"schemaVersion": 1, "referenceSha256": sha256(reference_path),
              "reference": reference, "appSha256": app_hash, "shaderSha256": shaders,
              "workingDirectory": str(app.parent), "environment": reference["provenance"]["environment"],
              "complete": False, "allMatched": False, "images": []}
    report_path = output / "parity.json"
    print(f"Reference: {reference['provenance']['device']}, macOS {reference['provenance']['macOS']}, "
          f"Xcode {reference['provenance']['xcode']}, {reference['provenance']['buildMode']}")
    print("Result  Image                              Actual SHA-256")
    for image in reference["images"]:
        destination = output / (image["name"] + ".bmp")
        command = command_for(app, image, destination)
        record = {"name": image["name"], "command": command, "match": False,
                  "expectedSha256": image["sha256"]}
        with (output / (image["name"] + ".log")).open("w") as log:
            result = subprocess.run(command, cwd=app.parent, env=environment, stdout=log, stderr=subprocess.STDOUT)
        record["returnCode"] = result.returncode
        if result.returncode:
            record["error"] = "capture failed; see image log"
        else:
            try:
                record.update(inspect_image(destination, image["sha256"]))
            except (OSError, ValueError) as error:
                record["error"] = str(error)
        report["images"].append(record)
        report_path.write_text(json.dumps(report, indent=2) + "\n")
        status = "PASS" if record["match"] else "FAIL"
        print(f"{status:6}  {image['name']:34} {record.get('actualSha256', record.get('error'))}", flush=True)
        if sha256(app) != app_hash or shader_hashes(app) != shaders:
            raise ValueError("App or runtime shaders changed during parity capture; run refused")
    report["complete"] = True
    report["allMatched"] = all(row["match"] for row in report["images"])
    report_path.write_text(json.dumps(report, indent=2) + "\n")
    print(f"{sum(row['match'] for row in report['images'])}/15 byte-identical; report: {report_path}")
    return report["allMatched"]


class ParityTests(unittest.TestCase):
    def test_reference_and_exact_commands(self):
        reference = load_reference(Path(__file__).with_name("reference.json"))
        vendor = next(row for row in reference["images"] if row["name"] == "damaged-helmet-vendor-0.5")
        self.assertEqual(command_for(Path("/build/App"), vendor, Path("/out/image.bmp")),
                         ["/build/App", "--scene", "damaged-helmet", "--temporal", "metalfx",
                          "--render-scale", "0.5", "--frames", "32", "--screenshot", "/out/image.bmp"])
        self.assertIn("within-version drift", reference["evidenceLimit"])

    def test_hash_mismatch_and_wrong_extent_fail(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "image.bmp"
            header = bytearray(26)
            header[:2] = b"BM"
            struct.pack_into("<ii", header, 18, 1280, -720)
            path.write_bytes(header)
            digest = sha256(path)
            self.assertTrue(inspect_image(path, digest)["match"])
            path.write_bytes(header + b"changed")
            self.assertFalse(inspect_image(path, digest)["match"])
            struct.pack_into("<ii", header, 18, 64, 64)
            path.write_bytes(header)
            with self.assertRaisesRegex(ValueError, "expected 1280x720"):
                inspect_image(path, digest)

    def test_runner_reports_all_mismatches_and_capture_failures(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            app = root / "App"
            app.write_bytes(b"fake executable")
            (root / "Shaders").mkdir()
            (root / "Shaders/display.metallib").write_bytes(b"fake shader")
            header = bytearray(26)
            header[:2] = b"BM"
            struct.pack_into("<ii", header, 18, 1280, 720)

            def capture(command, **kwargs):
                self.assertEqual(kwargs["cwd"], root.resolve())
                self.assertEqual(kwargs["env"]["MTL_DEBUG_LAYER"], "1")
                self.assertNotIn("LMX_SCREENSHOT_NO_BLOOM", kwargs["env"])
                Path(command[-1]).write_bytes(header)
                return subprocess.CompletedProcess(command, 1 if "--temporal" in command and "off" in command else 0)

            with mock.patch.object(subprocess, "run", side_effect=capture), contextlib.redirect_stdout(io.StringIO()):
                self.assertFalse(run(app, root / "output", Path(__file__).with_name("reference.json")))
            report = json.loads((root / "output/parity.json").read_text())
            self.assertTrue(report["complete"])
            self.assertFalse(report["allMatched"])
            self.assertEqual(len(report["images"]), 15)
            self.assertEqual(sum("error" in row for row in report["images"]), 3)
            self.assertEqual(sum("actualSha256" in row for row in report["images"]), 12)

    def test_rejects_missing_or_duplicate_reference_cases(self):
        reference = load_reference(Path(__file__).with_name("reference.json"))
        reference["images"][-1] = reference["images"][0]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "reference.json"
            path.write_text(json.dumps(reference))
            with self.assertRaisesRegex(ValueError, "fifteen distinct"):
                load_reference(path)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--reference", type=Path, default=Path(__file__).with_name("reference.json"))
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    if args.selftest:
        result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(ParityTests))
        return 0 if result.wasSuccessful() else 1
    if args.app is None or args.output is None:
        parser.error("--app and --output are required unless --selftest is used")
    try:
        return 0 if run(args.app, args.output, args.reference) else 1
    except (OSError, ValueError, KeyError) as error:
        print(f"parity refused: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
