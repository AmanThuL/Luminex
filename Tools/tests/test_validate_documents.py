from __future__ import annotations

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


if __name__ == "__main__":
    unittest.main()
