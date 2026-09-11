"""Regression coverage for alignment, fallback refusal and self-contained reports."""
import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

from PIL import Image
import compare


class ComparisonTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        frame = {"ordinal": 0, "simulationFrame": 2, "timeSeconds": 2 / 60,
                 "file": "frame-000000.bmp", "camera": {"position": [1, 2, 3], "yaw": 0,
                    "pitch": 0, "fovY": 1, "nearZ": 0.1}, "fallback": 0,
                 "renderWidth": 2, "renderHeight": 2, "effectiveScale": 0.5,
                 "jitterIndex": 2, "jitterEnabled": True, "exposureEv": 0,
                 "autoExposure": False, "bloom": True, "bloomThreshold": 1,
                 "bloomIntensity": 0.2, "shadowFilter": "pcf"}
        for mode in compare.MODES:
            directory = self.root / mode
            directory.mkdir()
            Image.new("RGB", (4, 4), (20, 40, 60)).save(directory / frame["file"])
            data = {"schemaVersion": 1, "complete": True, "requestedMode": mode,
                    "scene": "test", "device": "fake", "width": 4, "height": 4,
                    "fps": 60, "warmup": 2, "frameCount": 1, "renderScale": 0.5,
                    "debugView": 0, "cameraTrack": True, "colorSpace": "sRGB LDR",
                    "dynamicResolution": False, "frames": [dict(frame, effectiveMode=mode)]}
            (directory / "manifest.json").write_text(json.dumps(data))

    def mutate(self, change):
        path = self.root / "metalfx" / "manifest.json"
        data = json.loads(path.read_text())
        change(data)
        path.write_text(json.dumps(data))

    def test_aligned_report_embeds_data_and_local_urls(self):
        report = compare.build_report(self.root)
        html = report.read_text()
        self.assertNotIn("/* CAPTURE_DATA */", html)
        self.assertIn("raw/frame-000000.png", html)
        self.assertNotIn("fetch(", html)
        self.assertNotIn("https://", html)
        self.assertIn("not ground truth", html)
        data = json.loads((self.root / "comparison.json").read_text())
        self.assertEqual(data["baseline"], "taa")
        self.assertEqual(len(data["frames"][0]["hashes"]["raw"]["png"]), 64)

    def test_rejects_fallback_even_if_images_exist(self):
        self.mutate(lambda d: d["frames"][0].update(effectiveMode="taa", fallback=2))
        with self.assertRaisesRegex(ValueError, "fallback/substituted"):
            compare.build_report(self.root)
        self.assertFalse((self.root / "index.html").exists())

    def test_rejects_camera_or_jitter_misalignment(self):
        original = json.loads((self.root / "metalfx/manifest.json").read_text())
        for field, value in (("camera", {"position": [9, 9, 9]}), ("jitterIndex", 3)):
            data = copy.deepcopy(original)
            data["frames"][0][field] = value
            (self.root / "metalfx/manifest.json").write_text(json.dumps(data))
            with self.subTest(field=field), self.assertRaisesRegex(ValueError, field):
                compare.validate_manifests(self.root)

    def test_rejects_incomplete_missing_frame_and_clamped_scale(self):
        self.mutate(lambda d: d.update(complete=False))
        with self.assertRaisesRegex(ValueError, "incomplete"):
            compare.validate_manifests(self.root)
        self.mutate(lambda d: d.update(complete=True))
        self.mutate(lambda d: d["frames"][0].update(effectiveScale=0.6))
        with self.assertRaisesRegex(ValueError, "clamped"):
            compare.validate_manifests(self.root)
        self.mutate(lambda d: d["frames"][0].update(effectiveScale=0.5))
        (self.root / "metalfx/frame-000000.bmp").unlink()
        with self.assertRaisesRegex(ValueError, "missing"):
            compare.validate_manifests(self.root)

    @unittest.skipUnless(importlib.util.find_spec("flip_evaluator"), "optional FLIP dependency absent")
    def test_flip_identity_and_relative_difference_metadata(self):
        reference = self.root / "reference.png"
        candidate = self.root / "candidate.png"
        Image.new("RGB", (32, 32), (80, 100, 120)).save(reference)
        Image.new("RGB", (32, 32), (200, 30, 60)).save(candidate)
        identity = compare.flip_difference(reference, reference, self.root / "equal.png", 67)
        changed = compare.flip_difference(reference, candidate, self.root / "changed.png", 67)
        self.assertAlmostEqual(identity["mean"], 0, places=7)
        self.assertAlmostEqual(identity["p95"], 0, places=7)
        self.assertGreater(changed["mean"], 0)
        self.assertGreater(changed["p95"], 0)
        self.assertEqual(changed["parameters"]["ppd"], 67)
        self.assertEqual(changed["parameters"], {"ppd": 67})
        json.dumps(changed, allow_nan=False)

    def test_rejects_time_and_extent_mismatch(self):
        self.mutate(lambda d: d["frames"][0].update(timeSeconds=0.8))
        with self.assertRaisesRegex(ValueError, "time"):
            compare.validate_manifests(self.root)
        self.mutate(lambda d: d["frames"][0].update(timeSeconds=2 / 60))
        Image.new("RGB", (5, 4)).save(self.root / "metalfx/frame-000000.bmp")
        with self.assertRaisesRegex(ValueError, "extent"):
            compare.build_report(self.root)


if __name__ == "__main__":
    unittest.main()
