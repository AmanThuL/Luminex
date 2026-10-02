"""Regression tests for exact scene-document hashes with separate geometry."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "Screenshots"))
import parity


class DocumentHashTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.document = self.root / "x.scene.gltf"
        self.animation = self.root / "x.scene.bin"
        self.geometry = self.root / "x.scene.geometry.bin"
        self.animation.write_bytes(b"animation\x00\xff")
        self.geometry.write_bytes(b"geometry\x00\xff")

    def write_document(self, buffers, schema=2):
        data = json.dumps({"extensions": {"LMX_scene": {"schemaVersion": schema}},
                           "buffers": buffers}, indent=2).encode() + b"\n"
        self.document.write_bytes(data)
        return data

    def test_two_buffers_hash_json_then_animation_in_either_order(self):
        for uris in ((self.animation.name, self.geometry.name),
                     (self.geometry.name, self.animation.name)):
            with self.subTest(uris=uris):
                data = self.write_document([{"uri": uri} for uri in uris])
                expected = hashlib.sha256(data + self.animation.read_bytes()).hexdigest()
                self.assertEqual(parity.document_hash(self.document), expected)

    def test_geometry_bytes_are_excluded_from_document_hash(self):
        data = self.write_document([{"uri": self.animation.name}, {"uri": self.geometry.name}])
        expected = hashlib.sha256(data + self.animation.read_bytes()).hexdigest()
        self.geometry.write_bytes(b"changed geometry")
        self.assertEqual(parity.document_hash(self.document), expected)
        self.animation.write_bytes(b"changed animation")
        self.assertNotEqual(parity.document_hash(self.document), expected)

    def test_geometry_only_hashes_json_alone(self):
        data = self.write_document([{"uri": self.geometry.name}])
        self.assertEqual(parity.document_hash(self.document), hashlib.sha256(data).hexdigest())

    def test_third_buffer_is_refused_even_when_every_uri_is_known(self):
        self.write_document([{"uri": self.animation.name}, {"uri": self.geometry.name},
                             {"uri": self.animation.name}])
        with self.assertRaisesRegex(ValueError, "x.scene.gltf.*companion"):
            parity.document_hash(self.document)

    def test_other_uri_is_refused(self):
        for uri in ("other.scene.bin", "other.scene.geometry.bin", "x.bin", "extra.bin",
                    "../x.scene.bin", "./x.scene.geometry.bin", "data:application/octet-stream;base64,AA=="):
            with self.subTest(uri=uri):
                self.write_document([{"uri": uri}])
                with self.assertRaisesRegex(ValueError, "x.scene.gltf.*companion"):
                    parity.document_hash(self.document)

    def test_duplicate_buffers_and_malformed_shapes_are_refused(self):
        for buffers in ([{"uri": self.animation.name}] * 2, [{"uri": self.geometry.name}] * 2,
                        [{"uri": self.animation.name}, {}], [1], {}, None):
            with self.subTest(buffers=buffers):
                self.write_document(buffers)
                with self.assertRaisesRegex(ValueError, "x.scene.gltf.*companion"):
                    parity.document_hash(self.document)

    def test_schema_one_hashes_are_unchanged(self):
        for buffers in ([], [{"uri": self.animation.name}]):
            with self.subTest(buffers=buffers):
                data = self.write_document(buffers, schema=1)
                expected = hashlib.sha256(data + (self.animation.read_bytes() if buffers else b"")).hexdigest()
                self.assertEqual(parity.document_hash(self.document), expected)
        self.document.write_bytes(b'{"extensions":{"LMX_scene":{"schemaVersion":1}}}\n')
        self.assertEqual(parity.document_hash(self.document),
                         hashlib.sha256(self.document.read_bytes()).hexdigest())

    def test_catalog_hashes_keep_the_pinned_values(self):
        tools = Path(__file__).resolve().parents[1]
        reference = json.loads((tools / "Screenshots/reference.json").read_text())
        catalog = tools.parent / "Assets/Scenes"
        for scene, expected in reference["documents"].items():
            with self.subTest(scene=scene):
                self.assertEqual(parity.document_hash(catalog / f"{scene}.scene.gltf"), expected)


if __name__ == "__main__":
    unittest.main()
