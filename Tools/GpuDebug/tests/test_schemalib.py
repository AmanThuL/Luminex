"""Unit tests for schemalib.py.

Run: python3 -m unittest discover -s Tools/GpuDebug/tests -v
"""
import json
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))

import schemalib  # noqa: E402

# Representative version-1 capture schema.
SCHEMA_EXAMPLE = {
    "version": 1,
    "context": {
        "sceneName": "…",
        "frameIndex": 120,
        "cameraPos": [1.0, 2.0, 3.0],
        "boundingSphere": [1.0, 2.0, 3.0, 4.0],
        "light0Direction": [0.0, -1.0, 0.0],
        "light0Strength": [1.0, 1.0, 1.0],
        "ambient": [0.1, 0.1, 0.1],
        "shadowFilter": "PCF",
    },
    "resources": [
        {"label": "lmx.render.shadowMap", "kind": "texture2d", "format": "D32Float",
         "width": 2048, "height": 2048, "mipLevels": 1},
        {"label": "lmx.device.frameData.0.page.0", "kind": "buffer", "sizeBytes": 262144},
    ],
    "uniformStructs": [
        {"name": "PassUniforms", "slot": 2, "sizeBytes": 288,
         "fields": [{"name": "viewProj", "offsetBytes": 0, "type": "float4x4"}]},
    ],
    "frameDataUploads": [
        {"pageLabel": "lmx.device.frameData.0.page.1", "slot": 2, "pageOffset": 1024,
         "sizeBytes": 288, "alignmentBytes": 256, "gpuAddress": 4294967296},
    ],
}


def _write_json(directory: pathlib.Path, name: str, data: dict) -> pathlib.Path:
    path = directory / name
    path.write_text(json.dumps(data))
    return path


class LoadSchemaTests(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.tmp_path = pathlib.Path(self._tmp.name)

    def test_round_trips_the_schema_example(self):
        path = _write_json(self.tmp_path, "schema.json", SCHEMA_EXAMPLE)

        schema = schemalib.load_schema(path)

        self.assertEqual(schema.context["sceneName"], "…")
        self.assertEqual(schema.context["frameIndex"], 120)
        self.assertEqual(len(schema.resources), 2)
        self.assertEqual(len(schema.uniform_structs), 1)
        self.assertEqual(len(schema.frame_data_uploads), 1)

    def test_texture_resource_carries_geometry(self):
        path = _write_json(self.tmp_path, "schema.json", SCHEMA_EXAMPLE)
        schema = schemalib.load_schema(path)

        texture = next(r for r in schema.resources if r.kind == "texture2d")

        self.assertEqual(texture.label, "lmx.render.shadowMap")
        self.assertEqual(texture.format, "D32Float")
        self.assertEqual(texture.width, 2048)
        self.assertEqual(texture.height, 2048)
        self.assertEqual(texture.mip_levels, 1)
        self.assertIsNone(texture.size_bytes)

    def test_buffer_resource_omits_texture_only_keys(self):
        # CaptureSchema.cpp's renderJson() never emits format/width/height/mipLevels for
        # buffers -- schemalib must not require them, and the fields must come back None.
        path = _write_json(self.tmp_path, "schema.json", SCHEMA_EXAMPLE)
        schema = schemalib.load_schema(path)

        buf = next(r for r in schema.resources if r.kind == "buffer")

        self.assertEqual(buf.label, "lmx.device.frameData.0.page.0")
        self.assertEqual(buf.size_bytes, 262144)
        self.assertIsNone(buf.format)
        self.assertIsNone(buf.width)
        self.assertIsNone(buf.height)
        self.assertIsNone(buf.mip_levels)

    def test_uniform_struct_fields(self):
        path = _write_json(self.tmp_path, "schema.json", SCHEMA_EXAMPLE)
        schema = schemalib.load_schema(path)

        struct = schema.uniform_structs[0]
        self.assertEqual(struct.name, "PassUniforms")
        self.assertEqual(struct.slot, 2)
        self.assertEqual(struct.size_bytes, 288)
        self.assertEqual(struct.fields[0].name, "viewProj")
        self.assertEqual(struct.fields[0].offset_bytes, 0)
        self.assertEqual(struct.fields[0].type, "float4x4")

    def test_frame_data_upload_fields(self):
        path = _write_json(self.tmp_path, "schema.json", SCHEMA_EXAMPLE)
        schema = schemalib.load_schema(path)

        upload = schema.frame_data_uploads[0]
        self.assertEqual(upload.page_label, "lmx.device.frameData.0.page.1")
        self.assertEqual(upload.slot, 2)
        self.assertEqual(upload.page_offset, 1024)
        self.assertEqual(upload.size_bytes, 288)
        self.assertEqual(upload.alignment_bytes, 256)
        self.assertEqual(upload.gpu_address, 4294967296)

    def test_missing_version_raises_schema_error(self):
        data = dict(SCHEMA_EXAMPLE)
        del data["version"]
        path = _write_json(self.tmp_path, "schema.json", data)

        with self.assertRaises(schemalib.SchemaError) as ctx:
            schemalib.load_schema(path)
        self.assertIn("version", str(ctx.exception))

    def test_unsupported_version_raises_schema_error(self):
        data = dict(SCHEMA_EXAMPLE)
        data["version"] = 2
        path = _write_json(self.tmp_path, "schema.json", data)

        with self.assertRaises(schemalib.SchemaError) as ctx:
            schemalib.load_schema(path)
        self.assertIn("2", str(ctx.exception))

    def test_missing_required_key_names_it_in_the_error(self):
        data = dict(SCHEMA_EXAMPLE)
        del data["resources"]
        path = _write_json(self.tmp_path, "schema.json", data)

        with self.assertRaises(schemalib.SchemaError) as ctx:
            schemalib.load_schema(path)
        self.assertIn("resources", str(ctx.exception))

    def test_missing_file_raises_schema_error(self):
        with self.assertRaises(schemalib.SchemaError):
            schemalib.load_schema(self.tmp_path / "does-not-exist.json")

    def test_missing_sidecar_error_carries_the_run_command(self):
        # Every fatal path must carry the command that produces a fresh capture artifact.
        with self.assertRaises(schemalib.SchemaError) as ctx:
            schemalib.load_schema(self.tmp_path / "does-not-exist.gputrace.schema.json")
        message = str(ctx.exception)
        self.assertIn(str(self.tmp_path / "does-not-exist.gputrace.schema.json"), message)
        self.assertIn("LMX_CAPTURE_PATH", message)
        self.assertIn("MTL_CAPTURE_ENABLED=1", message)

    def test_every_schema_error_path_carries_the_run_command(self):
        # All five SchemaError raise sites in schemalib.py route through _error(), which appends
        # RUN_COMMAND_HINT -- pin that for the structural-error paths too, not just "file
        # missing", so a future raise site can't quietly skip the hint.
        cases = {
            "missing version": {k: v for k, v in SCHEMA_EXAMPLE.items() if k != "version"},
            "wrong version": {**SCHEMA_EXAMPLE, "version": 2},
            "missing resources": {k: v for k, v in SCHEMA_EXAMPLE.items() if k != "resources"},
        }
        for name, data in cases.items():
            with self.subTest(name):
                path = _write_json(self.tmp_path, f"{name}.json", data)
                with self.assertRaises(schemalib.SchemaError) as ctx:
                    schemalib.load_schema(path)
                self.assertIn("LMX_CAPTURE_PATH", str(ctx.exception))

    def test_context_tolerates_null_floats(self):
        # A non-finite context float round-trips through the sidecar as JSON null
        # (CaptureSchema.cpp's appendFloat) -- load_schema must not choke on it.
        data = dict(SCHEMA_EXAMPLE)
        data["context"] = dict(SCHEMA_EXAMPLE["context"])
        data["context"]["cameraPos"] = [1.0, None, 3.0]
        path = _write_json(self.tmp_path, "schema.json", data)

        schema = schemalib.load_schema(path)

        self.assertEqual(schema.context["cameraPos"], [1.0, None, 3.0])


if __name__ == "__main__":
    unittest.main()
