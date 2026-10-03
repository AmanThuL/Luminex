from __future__ import annotations

import hashlib
import io
import json
import subprocess
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
from unittest import mock

from Tools.Scenes import validate_documents


def report(errors: list[dict[str, object]], warnings: int = 0) -> str:
    return json.dumps({
        "validatorVersion": "2.0.0-dev.3.10",
        "issues": {
            "numErrors": len(errors),
            "numWarnings": warnings,
            "messages": errors,
            "truncated": False,
        },
    })


class ValidateDocumentsTests(unittest.TestCase):
    def test_parses_error_code_pointer_and_message(self) -> None:
        data = report([{
            "code": "UNRESOLVED_REFERENCE",
            "message": "Buffer could not be loaded.",
            "severity": 0,
            "pointer": "/buffers/0/uri",
        }])
        result = validate_documents.parse_report(data)
        self.assertEqual(result.error_count, 1)
        self.assertEqual(result.errors,
                         ("/buffers/0/uri UNRESOLVED_REFERENCE: Buffer could not be loaded.",))

    def test_rejects_malformed_or_truncated_report(self) -> None:
        with self.assertRaises(ValueError):
            validate_documents.parse_report("not JSON")
        with self.assertRaises(ValueError):
            validate_documents.parse_report('{"issues":{"numErrors":0}}')
        truncated = json.loads(report([]))
        truncated["issues"]["truncated"] = True
        with self.assertRaisesRegex(ValueError, "truncated"):
            validate_documents.parse_report(json.dumps(truncated))

    def test_validator_errors_are_preserved_even_when_process_fails(self) -> None:
        output = report([{
            "code": "INVALID_JSON",
            "message": "Invalid JSON data.",
            "severity": 0,
            "pointer": "",
        }])
        completed = subprocess.CompletedProcess([], 1, output, "validator diagnostic")
        with mock.patch.object(validate_documents.subprocess, "run", return_value=completed):
            failures = validate_documents.validate_one(Path("bad.scene.gltf"), Path("validator"))
        self.assertIn("INVALID_JSON: Invalid JSON data.", failures[0])

    def test_nonzero_exit_without_report_is_failure(self) -> None:
        completed = subprocess.CompletedProcess([], 2, "", "validator crashed")
        with mock.patch.object(validate_documents.subprocess, "run", return_value=completed):
            failures = validate_documents.validate_one(Path("bad.scene.gltf"), Path("validator"))
        self.assertIn("validator crashed", failures[0])

    def test_launch_failure_names_rosetta(self) -> None:
        with mock.patch.object(validate_documents.subprocess, "run", side_effect=OSError("Bad CPU type")):
            failures = validate_documents.validate_one(Path("scene.gltf"), Path("validator"))
        self.assertIn("Rosetta", failures[0])
        self.assertIn("softwareupdate --install-rosetta", failures[0])

    def test_discovers_catalog_and_generated_documents_recursively(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            catalog = root / "Assets/Scenes"
            generated = root / "build/test/SceneDocuments/nested"
            catalog.mkdir(parents=True)
            generated.mkdir(parents=True)
            (catalog / "one.scene.gltf").write_text("{}")
            (catalog / "other.gltf").write_text("{}")
            (generated / "two.gltf").write_text("{}")
            (generated / "not-a-document.bin").write_bytes(b"")
            docs = validate_documents.discover_documents(root, root / "build/test/SceneDocuments")
            self.assertEqual([p.name for p in docs.catalog], ["one.scene.gltf"])
            self.assertEqual([p.name for p in docs.generated], ["two.gltf"])

    def test_empty_catalog_is_not_a_passing_gate(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            docs = validate_documents.discover_documents(root, root / "SceneDocuments")
            self.assertEqual(docs.catalog, ())
            self.assertIn("catalog", validate_documents.catalog_status(docs).lower())
            self.assertIn("no documents", validate_documents.catalog_status(docs).lower())

    def test_main_rejects_missing_writer_output_directory(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            catalog = root / "Assets/Scenes"
            catalog.mkdir(parents=True)
            (catalog / "catalog.scene.gltf").write_text('{"asset":{"version":"2.0"}}')
            output = io.StringIO()
            with redirect_stdout(output), redirect_stderr(output):
                result = validate_documents.main([
                    "--root", str(root), "--generated-dir", str(root / "SceneDocuments")
                ])
            self.assertEqual(result, 1)
            self.assertIn("writer-test validation did not run", output.getvalue())

    def test_main_rejects_empty_writer_output_directory(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            catalog = root / "Assets/Scenes"
            catalog.mkdir(parents=True)
            (catalog / "catalog.scene.gltf").write_text('{"asset":{"version":"2.0"}}')
            generated = root / "SceneDocuments"
            generated.mkdir()
            output = io.StringIO()
            with redirect_stdout(output), redirect_stderr(output):
                result = validate_documents.main([
                    "--root", str(root), "--generated-dir", str(generated)
                ])
            self.assertEqual(result, 1)
            self.assertIn("writer-test validation did not run", output.getvalue())

    def test_main_validates_catalog_and_writer_outputs(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            catalog = root / "Assets/Scenes"
            catalog.mkdir(parents=True)
            catalog_document = catalog / "catalog.scene.gltf"
            catalog_document.write_text('{"asset":{"version":"2.0"}}')
            generated = root / "SceneDocuments"
            generated.mkdir()
            writer_document = generated / "writer.scene.gltf"
            writer_document.write_text('{"asset":{"version":"2.0"}}')
            binary = root / "gltf_validator"
            binary.touch()
            output = io.StringIO()
            with (
                mock.patch.object(validate_documents, "validate_one", return_value=[]) as validate,
                redirect_stdout(output),
                redirect_stderr(output),
            ):
                result = validate_documents.main([
                    "--root", str(root), "--generated-dir", str(generated),
                    "--validator", str(binary),
                ])
            self.assertEqual(result, 0)
            self.assertEqual([call.args[0] for call in validate.call_args_list],
                             [catalog_document, writer_document])
            self.assertIn("passed for 2 documents", output.getvalue())


    def content_fixture(self, root: Path) -> tuple[Path, dict[str, object]]:
        document = root / "fixture.scene.gltf"
        files = {"fixture.scene.geometry.bin": b"geometry",
                 "fixture.scene.textures/image.png": b"image"}
        for uri, data in files.items():
            path = root / uri
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        payload = {
            "asset": {"version": "2.0"},
            "buffers": [{"uri": "fixture.scene%2ebin", "byteLength": 4},
                        {"uri": "fixture.scene.geometry.bin", "byteLength": 8}],
            "images": [{"uri": "fixture.scene.textures/image.png"}],
            "extensions": {"LMX_scene": {"schemaVersion": 2, "contentHashes": {
                uri: hashlib.sha256(data).hexdigest() for uri, data in files.items()
            }}},
        }
        document.write_text(json.dumps(payload))
        return document, payload

    def test_content_hashes_check_geometry_and_images_without_animation(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            document, _ = self.content_fixture(Path(directory))
            self.assertEqual(validate_documents.check_content_hashes(document), [])

    def test_content_hash_missing_entry_file_and_changed_bytes_name_reference(self) -> None:
        for kind in ("geometry", "image"):
            for failure in ("entry", "file", "bytes"):
                with self.subTest(kind=kind, failure=failure), tempfile.TemporaryDirectory() as directory:
                    root = Path(directory)
                    document, payload = self.content_fixture(root)
                    uri = ("fixture.scene.geometry.bin" if kind == "geometry" else
                           "fixture.scene.textures/image.png")
                    if failure == "entry":
                        del payload["extensions"]["LMX_scene"]["contentHashes"][uri]
                        document.write_text(json.dumps(payload))
                    elif failure == "file":
                        (root / uri).unlink()
                    else:
                        (root / uri).write_bytes(b"changed")
                    errors = validate_documents.check_content_hashes(document)
                    self.assertEqual(len(errors), 1)
                    self.assertIn(str(document), errors[0])
                    self.assertIn("/buffers/1/uri" if kind == "geometry" else "/images/0/uri", errors[0])
                    self.assertIn(uri, errors[0])

    def test_checks_extra_hash_entries_and_rejects_invalid_sha256(self) -> None:
        for digest in ("0" * 64, "A" * 64, 42, "bad"):
            with self.subTest(digest=digest), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                document, payload = self.content_fixture(root)
                uri = "fixture.scene.textures/unused.png"
                (root / uri).write_bytes(b"unused")
                payload["extensions"]["LMX_scene"]["contentHashes"][uri] = digest
                document.write_text(json.dumps(payload))
                errors = validate_documents.check_content_hashes(document)
                self.assertEqual(len(errors), 1)
                self.assertIn("/extensions/LMX_scene/contentHashes/", errors[0])
                self.assertIn(uri, errors[0])

    def test_content_uris_are_decoded_and_confined_to_document_content(self) -> None:
        for uri in ("../foreign.png", "%2e%2e/foreign.png", "/absolute.png", "data:image/png,x",
                    "fixture.scene.textures/%00.png", "fixture.scene.textures/%ZZ.png",
                    "other.scene.geometry.bin", "other.textures/a.png", "fixture.scene.textures/a.dds"):
            with self.subTest(uri=uri), tempfile.TemporaryDirectory() as directory:
                document, payload = self.content_fixture(Path(directory))
                payload["images"][0]["uri"] = uri
                payload["extensions"]["LMX_scene"]["contentHashes"][uri] = "0" * 64
                document.write_text(json.dumps(payload))
                errors = validate_documents.check_content_hashes(document)
                self.assertTrue(any("/images/0/uri" in error for error in errors))
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            document, payload = self.content_fixture(root)
            uri = "fixture.scene.textures/image.png"
            digest = payload["extensions"]["LMX_scene"]["contentHashes"].pop(uri)
            encoded = uri.replace("image", "%69mage")
            payload["images"][0]["uri"] = encoded
            payload["extensions"]["LMX_scene"]["contentHashes"][encoded] = digest
            document.write_text(json.dumps(payload))
            self.assertEqual(validate_documents.check_content_hashes(document), [])

    def test_content_reference_kind_cannot_be_swapped(self) -> None:
        for member, uri in (("images", "fixture.scene.geometry.bin"),
                            ("buffers", "fixture.scene.textures/image.png")):
            with self.subTest(member=member), tempfile.TemporaryDirectory() as directory:
                document, payload = self.content_fixture(Path(directory))
                payload[member] = [{"uri": uri}]
                document.write_text(json.dumps(payload))
                errors = validate_documents.check_content_hashes(document)
                self.assertTrue(any(f"/{member}/0/uri" in error for error in errors))

    def test_content_hashes_preserve_legacy_and_reject_malformed_inputs(self) -> None:
        for payload in ({"asset": {"version": "2.0"}}, {"extensions": {"LMX_scene": {
                "schemaVersion": 1}}, "buffers": [{"uri": "fixture.scene.bin"}]}):
            with tempfile.TemporaryDirectory() as directory:
                document = Path(directory) / "fixture.scene.gltf"
                document.write_text(json.dumps(payload))
                self.assertEqual(validate_documents.check_content_hashes(document), [])
        for schema, uri in ((schema, uri) for schema in (None, 1, 2) for uri in
                            ("motion%20data.bin", "nested/padded-name.bin", "fixture.scene.geometry.bin")):
            with self.subTest(schema=schema, uri=uri), tempfile.TemporaryDirectory() as directory:
                document = Path(directory) / "fixture.scene.gltf"
                lmx = {"schemaVersion": schema} if schema is not None else {}
                document.write_text(json.dumps({"extensions": {"LMX_scene": lmx},
                                                "buffers": [{"uri": uri, "byteLength": 4}]}))
                errors = validate_documents.check_content_hashes(document)
                self.assertEqual(bool(errors), schema == 2)
                if errors:
                    self.assertIn("/buffers/0/uri", errors[0])
        for data in ("not JSON", "[]", '{"images":[{}]}', '{"buffers":[{"uri":7}]}',
                     '{"extensions":{"LMX_scene":{"contentHashes":[]}}}'):
            with self.subTest(data=data), tempfile.TemporaryDirectory() as directory:
                document = Path(directory) / "fixture.scene.gltf"
                document.write_text(data)
                self.assertTrue(validate_documents.check_content_hashes(document))

    def test_selftest_uses_temporary_content_without_validator_or_catalog(self) -> None:
        with mock.patch.object(validate_documents.subprocess, "run") as run, redirect_stdout(io.StringIO()):
            self.assertEqual(validate_documents.main(["--selftest", "--root", "/nonexistent"]), 0)
        run.assert_not_called()

    def test_main_preserves_khronos_run_and_fails_catalog_hash_mutation(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            catalog = root / "Assets/Scenes"
            catalog.mkdir(parents=True)
            document, _ = self.content_fixture(catalog)
            (catalog / "fixture.scene.geometry.bin").write_bytes(b"changed")
            generated = root / "SceneDocuments"
            generated.mkdir()
            (generated / "writer.gltf").write_text("{}")
            binary = root / "validator"
            binary.touch()
            output = io.StringIO()
            with mock.patch.object(validate_documents, "validate_one", return_value=[]) as validate, \
                    redirect_stdout(output), redirect_stderr(output):
                result = validate_documents.main(["--root", str(root), "--generated-dir", str(generated),
                                                  "--validator", str(binary)])
            self.assertEqual(result, 1)
            self.assertEqual(validate.call_count, 2)
            self.assertIn(str(document), output.getvalue())
            self.assertIn("SHA-256 mismatch", output.getvalue())


if __name__ == "__main__":
    unittest.main()
