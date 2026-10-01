"""Checks proposal sidecars against the scene document pair on disk."""

import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from urllib.parse import unquote


REPOSITORY = Path(__file__).resolve().parents[2]
CLIENT = REPOSITORY / "Tools/Session/lmx_session.py"
TEMPORAL_LAB_LOADED_PAIR_HASH = "4eda44f54b3000353e2cb783bb49892af67760f878b7204b130500e3ef884114"


class SidecarTests(unittest.TestCase):
    def run_client(self, *args):
        return subprocess.run([sys.executable, str(CLIENT), *map(str, args)],
                              text=True, capture_output=True, check=False)

    def test_sidecar_hashes_raw_bytes_and_rebases_evidence(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            document = root / "scene with spaces.scene.gltf"
            buffer = root / "motion data.bin"
            buffer.write_bytes(b"\x00\xff\x07\n")
            gltf = b'{\n  "buffers": [{"uri": "motion%20data.bin", "byteLength": 4}]\n}\n'
            document.write_bytes(gltf)
            evidence = root / "captures/frame.png"
            evidence.parent.mkdir()
            evidence.write_bytes(b"frame")

            result = self.run_client("sidecar", document, "--actor", "Lighting client",
                                     "--summary", "Moved lamp", "--evidence", evidence)

            self.assertEqual(result.returncode, 0, result.stderr)
            sidecar = root / "scene with spaces.scene.proposal.json"
            self.assertTrue(sidecar.is_file())
            self.assertEqual(json.loads(sidecar.read_text()), {
                "schema": 1,
                "actor": "Lighting client",
                "summary": "Moved lamp",
                "evidence": ["captures/frame.png"],
                "documentSha256": hashlib.sha256(gltf + buffer.read_bytes()).hexdigest(),
            })

    def test_temporal_lab_pair_uses_buffer_uri(self):
        document = REPOSITORY / "Assets/Scenes/temporal-lab.scene.gltf"
        gltf = document.read_bytes()
        uri = json.loads(gltf)["buffers"][0]["uri"]
        buffer = document.parent / unquote(uri)
        expected = hashlib.sha256(gltf + buffer.read_bytes()).hexdigest()
        self.assertEqual(expected, TEMPORAL_LAB_LOADED_PAIR_HASH)
        with tempfile.TemporaryDirectory() as temp:
            copy = Path(temp) / document.name
            copy.write_bytes(gltf)
            (copy.parent / buffer.name).write_bytes(buffer.read_bytes())
            result = self.run_client("sidecar", copy, "--actor", "Scene client",
                                     "--summary", "Check temporal document")
            self.assertEqual(result.returncode, 0, result.stderr)
            sidecar = copy.with_name("temporal-lab.scene.proposal.json")
            self.assertEqual(json.loads(sidecar.read_text())["documentSha256"],
                             TEMPORAL_LAB_LOADED_PAIR_HASH)

    def test_malformed_or_missing_pair_never_writes_sidecar(self):
        cases = (
            (b"{bad json", None, "JSON"),
            (b"[]", None, "object"),
            (b'{"buffers": [{}]}', None, "uri"),
            (b'{"buffers": [{"uri": "missing.bin", "byteLength": 2}]}', None, "missing.bin"),
            (b'{"buffers": [{"uri": "motion.bin", "byteLength": 3}]}', b"ab", "byteLength"),
            (b'{"buffers": [{"uri": "../motion.bin", "byteLength": 2}]}', b"ab", "URI"),
        )
        for gltf, binary, expected_error in cases:
            with self.subTest(gltf=gltf), tempfile.TemporaryDirectory() as temp:
                document = Path(temp) / "sample.scene.gltf"
                document.write_bytes(gltf)
                if binary is not None:
                    (document.parent / "motion.bin").write_bytes(binary)
                result = self.run_client("sidecar", document, "--actor", "Client",
                                         "--summary", "Summary")
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(expected_error, result.stderr)
                self.assertFalse((document.parent / "sample.scene.proposal.json").exists())

        with tempfile.TemporaryDirectory() as temp:
            document = Path(temp) / "absent.scene.gltf"
            result = self.run_client("sidecar", document, "--actor", "Client",
                                     "--summary", "Summary")
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse((document.parent / "absent.scene.proposal.json").exists())

    def test_json_forms_incompatible_with_editor_never_write_sidecar(self):
        cases = (
            ('{"note": 1}'.encode("utf-16"), "UTF-8"),
            (b'{"bad": NaN}', "JSON"),
            (b'{"bad": Infinity}', "JSON"),
            (b'{"buffers": [], "buffers": [{"uri": "motion.bin", "byteLength": 2}]}',
             "duplicate"),
            (b'{"buffers": [{"uri": "first.bin", "uri": "motion.bin", "byteLength": 2}]}',
             "duplicate"),
            (b'{"note": "\\ud800"}', "surrogate"),
            (b'{"\\ud800": "note"}', "surrogate"),
        )
        for gltf, expected_error in cases:
            with self.subTest(gltf=gltf), tempfile.TemporaryDirectory() as temp:
                document = Path(temp) / "sample.scene.gltf"
                document.write_bytes(gltf)
                (document.parent / "motion.bin").write_bytes(b"ab")
                result = self.run_client("sidecar", document, "--actor", "Client",
                                         "--summary", "Summary")
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(expected_error, result.stderr)
                self.assertFalse((document.parent / "sample.scene.proposal.json").exists())

    def test_selftest(self):
        result = self.run_client("--selftest")
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
