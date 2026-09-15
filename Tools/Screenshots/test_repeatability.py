#!/usr/bin/env python3
"""Synthetic image and schema tests for the independent visibility interval verifier."""
from __future__ import annotations

import copy
import json
import struct
from pathlib import Path
import tempfile
import unittest

from PIL import Image
from compare import image_difference
from parity import load_reference
from repeatability import (REFERENCE, EXTENT, PROFILE, case_settings, extrema, freeze,
                           interval_difference, load_frozen, validate_cases, verify, ceiling_difference)


class RepeatabilityTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)

    def strict_span(self, low, high):
        a, b = self.root / "low.png", self.root / "high.png"
        low.save(a)
        high.save(b)
        return image_difference(a, b, low.size, "strict")

    def test_stable_pixel_and_one_beyond_interval_fail_without_slack(self):
        image = Image.new("RGB", (2000, 1), (40, 80, 120))
        low, high = extrema([image] * 9)
        changed = image.copy()
        changed.putpixel((13, 0), (41, 80, 120))
        result = interval_difference(changed, low, high)
        self.assertEqual(result, dict(outsidePixels=1, outsideChannels=1, maxExcess=1, inside=False))
        high.putpixel((13, 0), (42, 80, 120))
        self.assertTrue(interval_difference(changed, low, high)["inside"])
        changed.putpixel((13, 0), (43, 80, 120))
        self.assertEqual(interval_difference(changed, low, high)["maxExcess"], 1)
        changed.putpixel((13, 0), (39, 79, 119))
        self.assertEqual(interval_difference(changed, low, high)["outsideChannels"], 3)

    def test_spatial_movement_is_not_an_aggregate_error_budget(self):
        base = Image.new("RGB", (2000, 1), (40, 80, 120))
        noisy = base.copy()
        noisy.putpixel((10, 0), (44, 80, 120))
        low, high = extrema([base, noisy])
        moved = base.copy()
        moved.putpixel((11, 0), (44, 80, 120))
        self.assertTrue(self.strict_span(base, moved)["pass"])
        self.assertTrue(self.strict_span(low, high)["pass"])
        self.assertFalse(interval_difference(moved, low, high)["inside"])

    def test_full_control_interval_is_inclusive_and_one_beyond_is_exclusive(self):
        low = Image.new("RGB", (2000, 1), (10, 20, 30))
        high = low.copy()
        high.putpixel((0, 0), (12, 23, 34))
        self.assertTrue(interval_difference(low, low, high)["inside"])
        self.assertTrue(interval_difference(high, low, high)["inside"])
        interior = low.copy()
        interior.putpixel((0, 0), (11, 22, 33))
        self.assertTrue(interval_difference(interior, low, high)["inside"])
        interior.putpixel((0, 0), (11, 22, 35))
        self.assertFalse(interval_difference(interior, low, high)["inside"])
        with self.assertRaisesRegex(ValueError, "inverted"):
            interval_difference(low, high, low)

    def test_widespread_noise_and_cumulative_disjoint_span_fail_strict(self):
        base = Image.new("RGB", (2000, 1), (40, 80, 120))
        uniform = Image.new("RGB", base.size, (41, 81, 121))
        self.assertFalse(self.strict_span(*extrema([base, uniform]))["pass"])
        samples = [base]
        for run in range(8):
            noisy = base.copy()
            for index in range(run * 3, run * 3 + 3):
                noisy.putpixel((index, 0), (41, 80, 120))
            self.assertTrue(self.strict_span(base, noisy)["pass"])
            samples.append(noisy)
        # Each control varies only three pixels; the frozen union varies 24 > strict 20.
        result = self.strict_span(*extrema(samples))
        self.assertEqual(result["differingPixels"], 24)
        self.assertFalse(result["pass"])

    def test_vendor_ceiling_is_scoped_and_does_not_expand_any_pixel_interval(self):
        base = Image.new("RGB", (2000, 1), (40, 80, 120))
        low_amplitude = base.copy()
        for index in range(600):
            low_amplitude.putpixel((index, 0), (41, 80, 120))
        a, b = self.root / "base.png", self.root / "low-amplitude.png"
        base.save(a)
        low_amplitude.save(b)
        self.assertFalse(ceiling_difference(a, b, base.size, "taa")["pass"])
        self.assertFalse(ceiling_difference(a, b, base.size, "off")["pass"])
        vendor = ceiling_difference(a, b, base.size, "metalfx")
        self.assertTrue(vendor["pass"])
        self.assertFalse(vendor["strictPass"])
        self.assertEqual(vendor["meanAbsoluteDelta"], 0.1)
        low, high = extrema([base, low_amplitude])
        outside = low_amplitude.copy()
        outside.putpixel((600, 0), (41, 80, 120))
        self.assertEqual(interval_difference(outside, low, high)["outsidePixels"], 1)
        self.assertFalse(interval_difference(outside, low, high)["inside"])
        outside.save(b)
        # Existing mean-error ceiling is unchanged: the 601st one-code channel exceeds it.
        self.assertFalse(ceiling_difference(a, b, base.size, "metalfx")["pass"])

    def test_case_schema_rejects_missing_duplicate_settings_order_and_reuse(self):
        reference = load_reference(REFERENCE)
        calibration = []
        for number, case in enumerate(reference["images"]):
            paths = []
            for run in range(9):
                path = self.root / f"case{number}-run{run}.png"
                path.touch()
                paths.append(str(path))
            calibration.append(dict(name=case["name"], inputs=paths))
        self.assertEqual(len(validate_cases(calibration, reference)), 15)
        bad_cases = []
        bad_cases.append(calibration[:-1])
        duplicate = copy.deepcopy(calibration)
        duplicate[-1] = duplicate[0]
        bad_cases.append(duplicate)
        reused = copy.deepcopy(calibration)
        reused[0]["inputs"][1] = reused[0]["inputs"][0]
        bad_cases.append(reused)
        settings = copy.deepcopy(calibration)
        settings[0]["settings"] = {**case_settings(reference["images"][0]), "frames": 1}
        bad_cases.append(settings)
        bad_cases.extend([{}, [None] * 15])
        for bad in bad_cases:
            with self.subTest(bad=type(bad).__name__), self.assertRaises(ValueError):
                validate_cases(bad, reference)
        holdout = [dict(name=case["name"], inputs=[dict(mode=mode, path=case["inputs"][i])
                   for i, mode in enumerate(("off", "cull", "cull", "off"))]) for case in calibration]
        self.assertEqual(len(validate_cases(holdout, reference, True)), 15)
        with self.assertRaisesRegex(ValueError, "distinct paths"):
            validate_cases(holdout, reference, True, {Path(calibration[0]["inputs"][0])})
        holdout[0]["inputs"][1]["mode"] = "off"
        with self.assertRaisesRegex(ValueError, "ABBA"):
            validate_cases(holdout, reference, True)

    def test_full_freeze_verify_and_immutable_hashes(self):
        reference_path = self.root / "reference.json"
        original_reference = REFERENCE.read_bytes()
        reference_path.write_bytes(original_reference)
        reference = load_reference(reference_path)
        calibration, holdout = [], []
        image = Image.new("RGB", EXTENT, (40, 80, 120))
        for number, case in enumerate(reference["images"]):
            inputs, held = [], []
            for run in range(9):
                path = self.root / f"calibration-{number}-{run}.bmp"
                image.save(path)
                inputs.append(str(path))
            for run, mode in enumerate(("off", "cull", "cull", "off")):
                suffix = ".bmp"
                path = self.root / f"holdout-{number}-{run}{suffix}"
                image.save(path)
                held.append(dict(mode=mode, path=str(path)))
            calibration.append(dict(name=case["name"], inputs=inputs))
            holdout.append(dict(name=case["name"], inputs=held))
        vendor_index = next(index for index, case in enumerate(reference["images"]) if case["temporal"] == "metalfx")
        vendor_control = image.copy()
        for y in range(100):
            for x in range(100):
                vendor_control.putpixel((x, y), (41, 80, 120))
        vendor_control.save(Path(calibration[vendor_index]["inputs"][1]))
        for index in (1, 2):
            vendor_control.save(Path(holdout[vendor_index]["inputs"][index]["path"]))
        temporal_off = next(index for index, case in enumerate(reference["images"]) if case["temporal"] == "off")
        changed_control = Path(calibration[temporal_off]["inputs"][-1])
        one_code = image.copy()
        one_code.putpixel((0, 0), (41, 80, 120))
        one_code.save(changed_control)
        invalid = freeze(calibration, self.root / "invalid-off", reference_path)
        invalid_case = invalid["images"][temporal_off]
        self.assertTrue(invalid_case["strictSpan"]["pass"])
        self.assertEqual(invalid_case["strictSpan"]["differingPixels"], 1)
        self.assertFalse(invalid["allValid"])
        with self.assertRaisesRegex(ValueError, "invalid calibration"):
            load_frozen(self.root / "invalid-off" / "envelope.json")
        image.save(changed_control)
        frozen = self.root / "frozen"
        report = freeze(calibration, frozen, reference_path)
        self.assertTrue(report["allValid"])
        self.assertEqual(report["profile"], PROFILE)
        self.assertEqual(sum(row["ceilingSpan"]["appliedProfile"] == "vendor-quantization-v1" for row in report["images"]), 6)
        self.assertFalse(report["images"][vendor_index]["strictSpan"]["pass"])
        self.assertTrue(report["images"][vendor_index]["ceilingPass"])
        verified = verify(frozen / "envelope.json", holdout, self.root / "verified.json")
        self.assertTrue(verified["allPassed"])
        self.assertTrue(verified["allTemporalOffExact"])
        self.assertTrue(verified["allPairsWithinCeiling"])
        self.assertFalse(verified["allPairsStrict"])
        self.assertTrue(verified["images"][vendor_index]["pass"])
        with self.assertRaisesRegex(ValueError, "must be new"):
            freeze(calibration, frozen, reference_path)
        with self.assertRaisesRegex(ValueError, "must be new"):
            verify(frozen / "envelope.json", holdout, self.root / "verified.json")
        document = (frozen / "envelope.json").read_bytes()
        malformed = json.loads(document)
        malformed["profile"] = "visibility-null-envelope-v1"
        (frozen / "envelope.json").write_text(json.dumps(malformed))
        with self.assertRaisesRegex(ValueError, "profile"):
            load_frozen(frozen / "envelope.json")
        (frozen / "envelope.json").write_bytes(document)
        malformed = json.loads(document)
        malformed["vendorThresholds"]["meanAbsoluteDelta"] = 0.2
        (frozen / "envelope.json").write_text(json.dumps(malformed))
        with self.assertRaisesRegex(ValueError, "thresholds"):
            load_frozen(frozen / "envelope.json")
        (frozen / "envelope.json").write_bytes(document)
        reference_path.write_bytes(original_reference + b"\n")
        with self.assertRaisesRegex(ValueError, "reference hash"):
            load_frozen(frozen / "envelope.json")
        reference_path.write_bytes(original_reference)
        raw = Path(calibration[0]["inputs"][0])
        original_raw = raw.read_bytes()
        altered = image.copy()
        altered.putpixel((0, 0), (41, 80, 120))
        altered.save(raw)
        with self.assertRaisesRegex(ValueError, "input changed"):
            load_frozen(frozen / "envelope.json")
        raw.write_bytes(original_raw)
        minimum = Path(report["images"][0]["minimum"]["path"])
        altered.save(minimum)
        with self.assertRaisesRegex(ValueError, "envelope hash"):
            load_frozen(frozen / "envelope.json")

    def test_opaque_and_extent_checks(self):
        from repeatability import read_image
        bad = self.root / "bad.png"
        Image.new("RGBA", (10, 10), (1, 2, 3, 254)).save(bad)
        with self.assertRaisesRegex(ValueError, "opaque"):
            read_image(bad, (10, 10))
        with self.assertRaisesRegex(ValueError, "extent"):
            read_image(bad)
        # Pillow itself discards BI_RGB32's fourth byte; the verifier must inspect raw BGRA.
        bmp = self.root / "alpha.bmp"
        Image.new("RGBA", (10, 10), (1, 2, 3, 255)).save(bmp)
        self.assertEqual(read_image(bmp, (10, 10))[1], "BMP")
        data = bytearray(bmp.read_bytes())
        offset = struct.unpack_from("<I", data, 10)[0]
        data[offset + 3] = 254
        bmp.write_bytes(data)
        with Image.open(bmp) as decoded:
            self.assertEqual(decoded.mode, "RGB")
        with self.assertRaisesRegex(ValueError, "opaque BGRA"):
            read_image(bmp, (10, 10))
        with self.assertRaisesRegex(ValueError, "absolute"):
            from repeatability import input_path
            input_path("relative.png")


if __name__ == "__main__":
    unittest.main()
