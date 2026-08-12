"""End-to-end tests for the gputrace_dump.py CLI.

Runs the CLI as a subprocess so these tests exercise user-visible argument parsing, exit codes,
stdout, and stderr. A valid bundle writes all manifest buckets and exits 0; a missing bundle
exits 2 with the capture-command hint on stderr.

Run: python3 -m unittest discover -s Tools/GpuDebug/tests -v
"""
import json
import pathlib
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

_TOOLS_DIR = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(_TOOLS_DIR))

import bundlelib  # noqa: E402
import gputrace_dump  # noqa: E402
import schemalib  # noqa: E402

from test_bundlelib import BundleFixture, _texture_header  # noqa: E402

_CLI = _TOOLS_DIR / "gputrace_dump.py"

_SCHEMA = {
    "version": 1,
    "context": {"sceneName": "Test", "frameIndex": 1, "cameraPos": [0, 0, 0],
               "boundingSphere": [0, 0, 0, 1], "light0Direction": [0, -1, 0],
               "light0Strength": [1, 1, 1], "ambient": [0.1, 0.1, 0.1], "shadowFilter": "PCF"},
    "resources": [
        {"label": "lmx.test.tex", "kind": "texture2d", "format": "BGRA8Unorm",
         "width": 4, "height": 2, "mipLevels": 1},
        {"label": "lmx.test.missing", "kind": "texture2d", "format": "BGRA8Unorm",
         "width": 8, "height": 8, "mipLevels": 1},
    ],
    "uniformStructs": [],
    "frameDataUploads": [],
}


def _run_cli(*args):
    return subprocess.run([sys.executable, str(_CLI), *args], capture_output=True, text=True)


class GputraceDumpCliTests(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.tmp_path = pathlib.Path(self._tmp.name)
        self.bundle = self.tmp_path / "test.gputrace"
        self.bundle.mkdir()
        self.schema_path = self.tmp_path / "test.gputrace.schema.json"
        self.schema_path.write_text(json.dumps(_SCHEMA))

    def _build_bundle(self):
        fx = BundleFixture(self.bundle)
        fx.write_metadata()
        fx.write_texture_blob(1, width=4, height=2)
        fx.add_labelled_resource("lmx.test.tex", "MTLTexture-1-0-mipmap0-slice0")
        # An orphan blob with no label record at all -- exercises unmatchedBlobs.
        fx.write_texture_blob(9, width=1, height=1)
        fx.flush_device_resources()
        return fx

    def test_writes_manifest_with_all_buckets_and_exits_0(self):
        self._build_bundle()
        out_dir = self.tmp_path / "out"

        result = _run_cli(str(self.bundle), "--schema", str(self.schema_path), "--out",
                          str(out_dir))

        self.assertEqual(result.returncode, 0, msg=result.stderr)
        manifest = json.loads((out_dir / "manifest.json").read_text())
        self.assertEqual(manifest["version"], 1)
        for bucket in ("matched", "matchedUndecodable", "unmatchedBlobs", "missingResources"):
            self.assertIn(bucket, manifest["resources"])
        self.assertEqual(len(manifest["resources"]["matched"]), 1)
        self.assertEqual(manifest["resources"]["matched"][0]["resource"]["label"],
                         "lmx.test.tex")
        missing_labels = {r["label"] for r in manifest["resources"]["missingResources"]}
        self.assertIn("lmx.test.missing", missing_labels)
        unmatched_names = {b["path"] for b in manifest["resources"]["unmatchedBlobs"]}
        self.assertIn("MTLTexture-9-0-mipmap0-slice0", unmatched_names)
        # write_texture_blob's default pixel format (80 == BGRA8Unorm) is decodable, so the
        # matched "lmx.test.tex" texture produces one image + a PNG on disk next to the manifest.
        self.assertEqual(len(manifest["images"]), 1)
        image = manifest["images"][0]
        self.assertEqual(image["label"], "lmx.test.tex")
        self.assertEqual(image["file"], "tex.png")
        self.assertEqual((image["width"], image["height"]), (4, 2))
        self.assertTrue((out_dir / "tex.png").exists())
        # The anomaly stage runs last and reads the finished manifest, so this synthetic bundle --
        # which has none of the engine's real resources and records no uniform uploads -- is
        # expected to light up. Asserting the exact (severity, check, subject) list rather than a
        # count is what makes the wiring verifiable: it pins that the checks saw the manifest's
        # own contents, and that the output is severity-ordered (errors, then warnings, then info).
        self.assertEqual(
            [(a["severity"], a["check"], a["subject"]) for a in manifest["anomalies"]],
            [("error", "missing-expected-resource", "lmx.render.sceneColor"),
             ("error", "missing-expected-resource", "lmx.render.shadowMap"),
             ("error", "no-pass-uniforms-upload", "PassUniforms"),
             ("warning", "flat-image", "lmx.test.tex"),
             ("info", "unmatched-blobs", "bundle")])
        self.assertIn("anomalies: 3 error, 1 warning, 1 info", result.stdout)
        self.assertEqual(manifest["capture"]["sceneName"], "Test")

    def test_bc1_texture_is_matched_undecodable_not_matched(self):
        fx = BundleFixture(self.bundle)
        fx.write_texture_blob(1, width=4, height=4, pixel_format=131)  # BC1_RGBA_sRGB
        fx.add_labelled_resource("lmx.test.bc1", "MTLTexture-1-0-mipmap0-slice0")
        fx.flush_device_resources()
        schema = {**_SCHEMA, "resources": [
            {"label": "lmx.test.bc1", "kind": "texture2d", "format": "BC1_RGBA_sRGB",
             "width": 4, "height": 4, "mipLevels": 1},
        ]}
        self.schema_path.write_text(json.dumps(schema))
        out_dir = self.tmp_path / "out"

        result = _run_cli(str(self.bundle), "--schema", str(self.schema_path), "--out",
                          str(out_dir))

        self.assertEqual(result.returncode, 0, msg=result.stderr)
        manifest = json.loads((out_dir / "manifest.json").read_text())
        self.assertEqual(manifest["resources"]["matched"], [])
        self.assertEqual(len(manifest["resources"]["matchedUndecodable"]), 1)
        entry = manifest["resources"]["matchedUndecodable"][0]
        self.assertEqual(entry["resource"]["label"], "lmx.test.bc1")
        self.assertIn("out of scope", entry["reason"])
        self.assertEqual(manifest["images"], [])

    def test_geometry_inconsistent_header_degrades_to_matched_undecodable(self):
        # bundlelib.join() validates width/height and headerSize+bytesPerImage
        # against the blob FILE size, but never checks bytesPerRow -- an internally inconsistent
        # header (bytesPerRow smaller than the tight row) can reach an unguarded ValueError
        # deep in decodelib and crash the whole CLI before manifest.json was written for anything.
        # decode_texture's geometry sanity check must now catch this as a normal DecodeError,
        # bucketed like any other undecodable texture: exit 0, manifest written, nothing lost.
        width, height, bad_bytes_per_row = 4, 4, 8  # tight row is width*4 = 16; 8 is inconsistent
        payload = b"\x00" * (bad_bytes_per_row * height)
        header = bundlelib.BLOB_HEADER_MAGIC
        header += struct.pack("<II", bundlelib.EXPECTED_HEADER_VERSION, bundlelib.BLOB_HEADER_SIZE)
        header += struct.pack("<QQQQQQQ", 1, 80, width, height, 1, bad_bytes_per_row, len(payload))
        header += b"\x00" * (bundlelib.BLOB_HEADER_SIZE - len(header))
        (self.bundle / "MTLTexture-5-0-mipmap0-slice0").write_bytes(header + payload)
        fx = BundleFixture(self.bundle)
        fx.add_labelled_resource("lmx.test.badrow", "MTLTexture-5-0-mipmap0-slice0")
        fx.flush_device_resources()
        schema = {**_SCHEMA, "resources": [
            {"label": "lmx.test.badrow", "kind": "texture2d", "format": "BGRA8Unorm",
             "width": width, "height": height, "mipLevels": 1},
        ]}
        self.schema_path.write_text(json.dumps(schema))
        out_dir = self.tmp_path / "out"

        result = _run_cli(str(self.bundle), "--schema", str(self.schema_path), "--out",
                          str(out_dir))

        self.assertEqual(result.returncode, 0, msg=result.stderr)
        manifest = json.loads((out_dir / "manifest.json").read_text())
        self.assertEqual(manifest["resources"]["matched"], [])
        self.assertEqual(len(manifest["resources"]["matchedUndecodable"]), 1)
        entry = manifest["resources"]["matchedUndecodable"][0]
        self.assertEqual(entry["resource"]["label"], "lmx.test.badrow")
        self.assertIn("bytesPerRow", entry["reason"])
        self.assertEqual(manifest["images"], [])

    def test_bundle_header_version_recorded(self):
        self._build_bundle()
        out_dir = self.tmp_path / "out"

        result = _run_cli(str(self.bundle), "--schema", str(self.schema_path), "--out",
                          str(out_dir))

        self.assertEqual(result.returncode, 0, msg=result.stderr)
        manifest = json.loads((out_dir / "manifest.json").read_text())
        self.assertEqual(manifest["bundleHeaderVersion"], bundlelib.EXPECTED_HEADER_VERSION)
        self.assertNotIn("notes", manifest)

    def test_unexpected_header_version_notes_but_still_exits_0(self):
        fx = BundleFixture(self.bundle)
        bad_header = _texture_header(4, 2, version=0x00010001)
        (self.bundle / "MTLTexture-1-0-mipmap0-slice0").write_bytes(bad_header)
        fx.add_labelled_resource("lmx.test.tex", "MTLTexture-1-0-mipmap0-slice0")
        fx.flush_device_resources()
        out_dir = self.tmp_path / "out"

        result = _run_cli(str(self.bundle), "--schema", str(self.schema_path), "--out",
                          str(out_dir))

        self.assertEqual(result.returncode, 0, msg=result.stderr)
        self.assertIn("0x00010001", result.stderr)
        manifest = json.loads((out_dir / "manifest.json").read_text())
        self.assertEqual(manifest["bundleHeaderVersion"], 0x00010001)
        self.assertTrue(any("0x00010001" in note for note in manifest["notes"]))

    def test_nonexistent_bundle_exits_2_with_run_hint_on_stderr(self):
        missing = self.tmp_path / "nope.gputrace"

        result = _run_cli(str(missing))

        self.assertEqual(result.returncode, 2)
        self.assertIn("LMX_CAPTURE_PATH", result.stderr)

    def test_default_schema_and_out_paths(self):
        self._build_bundle()

        result = _run_cli(str(self.bundle))

        self.assertEqual(result.returncode, 0, msg=result.stderr)
        default_out = self.tmp_path / "test-dump" / "manifest.json"
        self.assertTrue(default_out.exists())


class DecodeImagesUnexpectedExceptionTests(unittest.TestCase):
    """decodelib's own geometry check cannot anticipate every
    malformed blob a real capture might contain. _decode_images must degrade a resource whose
    decode raises *anything* -- not just decodelib.DecodeError -- to matched_undecodable, so one
    bad blob can't lose the whole manifest for every other resource. Exercised at the
    _decode_images unit level (not the subprocess CLI) since the point is to prove decodelib
    raising something decode_texture's own checks didn't foresee still can't escape -- easiest to
    force with a mock rather than construct a genuinely unforeseen blob."""

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.tmp_path = pathlib.Path(self._tmp.name)

    def test_unexpected_exception_degrades_resource_to_matched_undecodable(self):
        blob_path = self.tmp_path / "MTLTexture-1-0-mipmap0-slice0"
        blob_path.write_bytes(b"\x00" * 512)  # contents irrelevant -- decode_texture is mocked
        blob = bundlelib.Blob(path=blob_path, size_bytes=512)
        resource = schemalib.Resource(label="lmx.test.broken", kind="texture2d",
                                      format="BGRA8Unorm", width=4, height=4, mip_levels=1,
                                      size_bytes=None)
        join_result = bundlelib.JoinResult(matched=[(resource, blob)], matched_undecodable=[],
                                           unmatched_blobs=[], missing_resources=[])
        out_dir = self.tmp_path / "out"
        out_dir.mkdir()

        with mock.patch.object(gputrace_dump.decodelib, "decode_texture",
                               side_effect=RuntimeError("boom")):
            images = gputrace_dump._decode_images(join_result, out_dir)

        self.assertEqual(images, [])
        self.assertEqual(join_result.matched, [])
        self.assertEqual(len(join_result.matched_undecodable), 1)
        undecodable_resource, reason = join_result.matched_undecodable[0]
        self.assertEqual(undecodable_resource.label, "lmx.test.broken")
        self.assertIn("boom", reason)
        self.assertIn("RuntimeError", reason)
        # No PNG written for the failed resource, and no crash reached this line.
        self.assertEqual(list(out_dir.iterdir()), [])


class UniformsJsonCliTests(unittest.TestCase):
    """manifest.json references uniforms.json, and uniforms.json is
    written on every run -- empty uploads (with an explanatory pageAttribution) when there's
    nothing to decode or the page-attribution policy declines, decoded uploads when it succeeds.
    """

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.tmp_path = pathlib.Path(self._tmp.name)
        self.bundle = self.tmp_path / "test.gputrace"
        self.bundle.mkdir()
        self.schema_path = self.tmp_path / "test.gputrace.schema.json"

    def _flush_unrelated_bundle(self):
        fx = BundleFixture(self.bundle)
        fx.add_labelled_resource("lmx.unrelated", None)
        fx.flush_device_resources()

    def test_manifest_gains_uniforms_key_and_no_uploads_case_is_unremarkable(self):
        self.schema_path.write_text(json.dumps(_SCHEMA))  # uniformStructs/frameDataUploads: []
        self._flush_unrelated_bundle()
        out_dir = self.tmp_path / "out"

        result = _run_cli(str(self.bundle), "--schema", str(self.schema_path), "--out",
                          str(out_dir))

        self.assertEqual(result.returncode, 0, msg=result.stderr)
        manifest = json.loads((out_dir / "manifest.json").read_text())
        self.assertEqual(manifest["uniforms"], "uniforms.json")
        self.assertNotIn("notes", manifest)  # "no uploads this capture" isn't a failure
        uniforms = json.loads((out_dir / "uniforms.json").read_text())
        self.assertEqual(uniforms["version"], 1)
        self.assertEqual(uniforms["uploads"], [])
        self.assertIn("no frame-data uploads", uniforms["pageAttribution"])

    def test_single_page_blob_decodes_pass_uniforms_end_to_end(self):
        page_bytes = bytearray(4096)
        # eyePos lives at struct offset 128; this upload starts at page offset 512, so its
        # absolute position is 512 + 128 = 640.
        page_bytes[640:652] = struct.pack("<3f", 1.5, 2.5, 3.5)
        (self.bundle / "MTLBuffer-13-0").write_bytes(bytes(page_bytes))
        self._flush_unrelated_bundle()
        schema = {
            **_SCHEMA,
            "resources": [
                {"label": "lmx.device.frameData.0.page.0", "kind": "buffer", "sizeBytes": 4096},
            ],
            "uniformStructs": [
                {"name": "PassUniforms", "slot": 2, "sizeBytes": 288,
                 "fields": [{"name": "eyePos", "offsetBytes": 128, "type": "float3"}]},
            ],
            "frameDataUploads": [
                {"pageLabel": "lmx.device.frameData.0.page.0", "slot": 2, "pageOffset": 512,
                 "sizeBytes": 288, "alignmentBytes": 256, "gpuAddress": 4096},
            ],
        }
        self.schema_path.write_text(json.dumps(schema))
        out_dir = self.tmp_path / "out"

        result = _run_cli(str(self.bundle), "--schema", str(self.schema_path), "--out",
                          str(out_dir))

        self.assertEqual(result.returncode, 0, msg=result.stderr)
        uniforms = json.loads((out_dir / "uniforms.json").read_text())
        self.assertEqual(len(uniforms["uploads"]), 1)
        upload = uniforms["uploads"][0]
        self.assertEqual(upload["structName"], "PassUniforms")
        self.assertEqual(upload["pageLabel"], "lmx.device.frameData.0.page.0")
        self.assertEqual(upload["alignmentBytes"], 256)
        self.assertNotIn("gpuAddress", upload)  # non-deterministic across runs; not surfaced
        self.assertEqual(upload["values"]["eyePos"], [1.5, 2.5, 3.5])
        self.assertTrue(upload["finite"])
        self.assertIn("single page-sized blob", uniforms["pageAttribution"])
        manifest = json.loads((out_dir / "manifest.json").read_text())
        self.assertNotIn("notes", manifest)  # clean attribution shouldn't add a manifest note

    def test_ambiguous_page_attribution_writes_empty_uploads_and_manifest_note(self):
        (self.bundle / "MTLBuffer-1-0").write_bytes(b"\x00" * 4096)
        (self.bundle / "MTLBuffer-2-0").write_bytes(b"\x00" * 4096)  # 2 candidates: not 1 or 3
        self._flush_unrelated_bundle()
        schema = {
            **_SCHEMA,
            "resources": [
                {"label": f"lmx.device.frameData.0.page.{i}", "kind": "buffer", "sizeBytes": 4096}
                for i in range(3)
            ],
            "uniformStructs": [],
            "frameDataUploads": [
                {"pageLabel": "lmx.device.frameData.0.page.0", "slot": 2, "pageOffset": 0,
                 "sizeBytes": 80, "alignmentBytes": 256, "gpuAddress": 0},
            ],
        }
        self.schema_path.write_text(json.dumps(schema))
        out_dir = self.tmp_path / "out"

        result = _run_cli(str(self.bundle), "--schema", str(self.schema_path), "--out",
                          str(out_dir))

        self.assertEqual(result.returncode, 0, msg=result.stderr)
        uniforms = json.loads((out_dir / "uniforms.json").read_text())
        self.assertEqual(uniforms["uploads"], [])
        self.assertTrue(uniforms["pageAttribution"])
        manifest = json.loads((out_dir / "manifest.json").read_text())
        self.assertEqual(manifest["notes"], [uniforms["pageAttribution"]])


if __name__ == "__main__":
    unittest.main()
