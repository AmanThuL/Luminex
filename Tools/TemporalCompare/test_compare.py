"""Regression coverage for alignment, fallback refusal and self-contained reports."""
import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

from PIL import Image, PngImagePlugin
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

    def upgrade(self, mode, container="png"):
        path = self.root / mode / "manifest.json"
        data = json.loads(path.read_text())
        data.pop("colorSpace")
        data.update(schemaVersion=2, container=container, ui={"composited": False},
                    display={"view": "sdr", "transfer": "srgb", "primaries": "bt709",
                             "toneMap": "pbr-neutral", "referenceWhite": 1.0, "peakWhite": 1.0,
                             "bitsPerChannel": 8, "opaque": True})
        original = path.parent / data["frames"][0]["file"]
        filename = "saved-image." + container
        metadata = PngImagePlugin.PngInfo()
        metadata.add_text("lmx:display", json.dumps(data["display"]))
        metadata.add_text("lmx:frame", '{"scene":"test"}')
        with Image.open(original) as img:
            img.save(path.parent / filename, pnginfo=metadata)
        data["frames"][0]["file"] = filename
        path.write_text(json.dumps(data))
        return path.parent / filename

    def test_v2_png_report_preserves_capture_bytes_and_text(self):
        for mode in compare.MODES:
            self.upgrade(mode)
        png = self.root / "raw/saved-image.png"
        before = png.read_bytes()
        compare.build_report(self.root)
        compare.build_report(self.root)
        self.assertEqual(before, png.read_bytes())
        with Image.open(png) as img:
            self.assertEqual(json.loads(img.info["lmx:display"])["view"], "sdr")
            self.assertIn("lmx:frame", img.info)
        data = json.loads((self.root / "comparison.json").read_text())
        self.assertEqual(data["settings"]["display"]["transfer"], "srgb")
        self.assertEqual(data["frames"][0]["images"]["raw"], "raw/saved-image.png")
        self.assertEqual(data["frames"][0]["hashes"]["raw"], {"png": compare.sha256(png)})

    def test_mixed_v1_bmp_v2_bmp_and_v2_png_report(self):
        self.upgrade("taa", "bmp")
        self.upgrade("metalfx", "png")
        compare.build_report(self.root)
        data = json.loads((self.root / "comparison.json").read_text())
        self.assertIn("bmp", data["frames"][0]["hashes"]["taa"])
        self.assertIn("png", data["frames"][0]["hashes"]["metalfx"])

    def test_v2_refuses_view_transfer_and_domain_mismatch(self):
        for mode in compare.MODES:
            self.upgrade(mode)
        for field, invalid in (("view", "edr"), ("transfer", "linear")):
            self.mutate(lambda d: d["display"].update({field: invalid}))
            with self.subTest(field=field), self.assertRaisesRegex(ValueError, "view=.*transfer="):
                compare.validate_manifests(self.root)
            self.mutate(lambda d: d["display"].update(view="sdr", transfer="srgb"))
        self.mutate(lambda d: d["display"].update(primaries="bt2020"))
        with self.assertRaisesRegex(ValueError, "display domain differs"):
            compare.validate_manifests(self.root)

    def test_v2_refuses_container_and_ui_mismatch(self):
        self.upgrade("metalfx")
        self.mutate(lambda d: d.update(container="bmp"))
        with self.assertRaisesRegex(ValueError, "container"):
            compare.validate_manifests(self.root)
        self.mutate(lambda d: d.update(container="png", ui={"composited": True}))
        with self.assertRaisesRegex(ValueError, "composited UI"):
            compare.validate_manifests(self.root)

    def test_refuses_reusing_one_image_for_distinct_frames(self):
        for schema_version in (1, 2):
            with self.subTest(schema_version=schema_version):
                for mode in compare.MODES:
                    if schema_version == 2:
                        self.upgrade(mode)
                    path = self.root / mode / "manifest.json"
                    data = json.loads(path.read_text())
                    data["frameCount"] = 2
                    second = dict(data["frames"][0], ordinal=1, simulationFrame=3,
                                  timeSeconds=3 / 60, jitterIndex=3)
                    data["frames"] = [data["frames"][0], second]
                    path.write_text(json.dumps(data))
                with self.assertRaisesRegex(ValueError, "duplicate frame filename"):
                    compare.validate_manifests(self.root)

    def test_refuses_frame_paths_outside_capture(self):
        self.mutate(lambda d: d["frames"][0].update(file="../taa/frame-000000.bmp"))
        with self.assertRaisesRegex(ValueError, "invalid frame filename"):
            compare.validate_manifests(self.root)

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
