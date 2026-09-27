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
import shutil
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
    schema = reference.get("schemaVersion")
    if schema not in (1, 2):
        raise ValueError("unsupported reference schema")
    provenance = reference["provenance"]
    if (provenance["width"], provenance["height"], provenance["frames"], provenance["container"]) != (1280, 720, 32, "bmp"):
        raise ValueError("reference must retain 1280x720, 32-frame BMP captures")
    images = reference["images"]
    scenes = ("sponza", "damaged-helmet", "material-lab") if schema == 1 else (
        "sponza", "material-lab", "temporal-lab")
    expected = {(scene, mode, scale) for scene in scenes
                for mode, scale in (("off", 1), ("taa", 1), ("taa", 0.5), ("metalfx", 1), ("metalfx", 0.5))}
    actual = {(row["scene"], row["temporal"], row["renderScale"]) for row in images}
    if len(images) != 15 or actual != expected or len({row["name"] for row in images}) != 15:
        raise ValueError("reference must contain all fifteen distinct scene/mode/scale cases")
    for row in images:
        if not re.fullmatch(r"[a-z0-9.-]+", row["name"]) or not re.fullmatch(r"[0-9a-f]{64}", row["sha256"]):
            raise ValueError("invalid reference name or SHA-256")
    if schema == 2:
        documents = reference.get("documents")
        if not isinstance(documents, dict) or set(documents) != set(scenes) or any(
                not isinstance(value, str) or not re.fullmatch(r"[0-9a-f]{64}", value)
                for value in documents.values()):
            raise ValueError("schema 2 requires three scene document SHA-256 values")
    return reference


def document_hash(path: Path) -> str:
    """Match sceneDocumentHash: SHA-256 of JSON bytes followed by referenced buffer bytes."""
    data = path.read_bytes()
    parsed = json.loads(data)
    if not isinstance(parsed, dict):
        raise ValueError(f"{path.name}: expected a JSON object")
    buffers = parsed.get("buffers", [])
    if not isinstance(buffers, list) or len(buffers) > 1 or (
            buffers and (not isinstance(buffers[0], dict)
                         or buffers[0].get("uri") != path.with_suffix(".bin").name)):
        raise ValueError(f"{path.name}: expected at most one matching .bin companion")
    digest = hashlib.sha256(data)
    if buffers:
        digest.update(path.with_suffix(".bin").read_bytes())
    return digest.hexdigest()


def runtime_catalog_document(app: Path, scene: str) -> Path:
    """Match findRepositoryAsset's eight-level, nearest-first search from App's CWD."""
    directory = app.parent
    for _ in range(8):
        candidate = directory / "Assets/Scenes" / f"{scene}.scene.gltf"
        if candidate.exists():
            return candidate
        if directory.parent == directory:
            break
        directory = directory.parent
    raise ValueError(f"{scene}: catalog document was not found from App working directory {app.parent}")


def verify_documents(reference: dict, documents: Path, app: Path | None = None) -> dict[str, str]:
    if reference["schemaVersion"] == 1:
        return {}
    observed = {}
    for scene, expected in reference["documents"].items():
        try:
            path = runtime_catalog_document(app, scene) if app else documents / f"{scene}.scene.gltf"
            actual = document_hash(path)
        except (OSError, ValueError, KeyError, TypeError) as error:
            raise ValueError(f"{scene}: document drift or missing companion: {error}") from error
        if actual != expected:
            raise ValueError(f"{scene}: document drift: expected {expected}, got {actual}")
        observed[scene] = actual
    return observed


def document_root_for(app: Path, explicit: Path | None) -> Path:
    if explicit is not None:
        return explicit.resolve()
    if (len(app.parents) < 5 or app.parent.name not in ("release", "debug")
            or app.parents[1].name != "arm64" or app.parents[2].name != "macosx"
            or app.parents[3].name != "build"):
        raise ValueError(f"cannot locate scene documents for App at {app}; pass --documents")
    return app.parents[4] / "Assets/Scenes"


def command_for(app: Path, image: dict, output: Path, documents: Path | None = None) -> list[str]:
    scene = str(documents / (image["scene"] + ".scene.gltf")) if documents else image["scene"]
    return [str(app), "--scene", scene, "--temporal", image["temporal"],
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


def run(app: Path, output: Path, reference_path: Path, documents: Path | None = None) -> bool:
    reference = load_reference(reference_path)
    app, output = app.resolve(), output.resolve()
    if not app.is_file():
        raise ValueError(f"App binary missing: {app}")
    # Resolve the catalog beside the target binary, not beside this script: they may
    # come from different checkouts during parent/candidate comparisons.
    document_root = document_root_for(app, documents) if reference["schemaVersion"] == 2 else None
    catalog_app = app if documents is None else None
    checked_documents = verify_documents(reference, document_root, catalog_app) if document_root else {}
    if documents is not None:
        documents = documents.resolve()
        for scene in {row["scene"] for row in reference["images"]}:
            if not (documents / (scene + ".scene.gltf")).is_file():
                raise ValueError(f"scene document missing: {documents / (scene + '.scene.gltf')}")
    if output.exists() and any(output.iterdir()):
        raise ValueError("output directory must be new or empty")
    app_hash, shaders = sha256(app), shader_hashes(app)
    output.mkdir(parents=True, exist_ok=True)
    environment = os.environ.copy()
    environment.pop("LMX_SCREENSHOT_NO_BLOOM", None)
    environment["MTL_DEBUG_LAYER"] = "1"
    report = {"schemaVersion": 1, "referenceSha256": sha256(reference_path),
              "reference": reference, "appSha256": app_hash, "shaderSha256": shaders,
              "documentSha256": checked_documents,
              "workingDirectory": str(app.parent), "environment": reference["provenance"]["environment"],
              "complete": False, "allMatched": False, "images": []}
    report_path = output / "parity.json"
    print(f"Reference: {reference['provenance']['device']}, macOS {reference['provenance']['macOS']}, "
          f"Xcode {reference['provenance']['xcode']}, {reference['provenance']['buildMode']}")
    print("Result  Image                              Actual SHA-256")
    for image in reference["images"]:
        destination = output / (image["name"] + ".bmp")
        command = command_for(app, image, destination, documents)
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
        if checked_documents and verify_documents(reference, document_root, catalog_app) != checked_documents:
            raise ValueError("scene documents changed during parity capture; run refused")
    report["complete"] = True
    report["allMatched"] = all(row["match"] for row in report["images"])
    report_path.write_text(json.dumps(report, indent=2) + "\n")
    print(f"{sum(row['match'] for row in report['images'])}/15 byte-identical; report: {report_path}")
    return report["allMatched"]


class ParityTests(unittest.TestCase):
    def test_reference_and_exact_commands(self):
        reference = load_reference(Path(__file__).with_name("reference.json"))
        self.assertEqual(reference["schemaVersion"], 2)
        self.assertEqual(set(reference["documents"]), {"sponza", "material-lab", "temporal-lab"})
        vendor = next(row for row in reference["images"] if row["name"] == "temporal-lab-vendor-0.5")
        self.assertEqual(command_for(Path("/build/App"), vendor, Path("/out/image.bmp")),
                         ["/build/App", "--scene", "temporal-lab", "--temporal", "metalfx",
                          "--render-scale", "0.5", "--frames", "32", "--screenshot", "/out/image.bmp"])
        mapped = command_for(Path("/build/App"), vendor, Path("/out/image.bmp"),
                             Path("/frozen/documents"))
        self.assertEqual(mapped[2], "/frozen/documents/temporal-lab.scene.gltf")

    def test_schema_two_checks_document_and_buffer_before_image(self):
        reference = load_reference(Path(__file__).with_name("reference.json"))
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for scene in reference["documents"]:
                for suffix in ("gltf", "bin"):
                    source = Path(__file__).resolve().parents[2] / "Assets/Scenes" / f"{scene}.scene.{suffix}"
                    shutil.copyfile(source, root / source.name)
            self.assertEqual(verify_documents(reference, root), reference["documents"])
            (root / "material-lab.scene.gltf").write_bytes(
                (root / "material-lab.scene.gltf").read_bytes() + b" ")
            with self.assertRaisesRegex(ValueError, "material-lab.*drift"):
                verify_documents(reference, root)
            with mock.patch.object(subprocess, "run") as capture:
                app = root / "App"
                app.write_bytes(b"app")
                (root / "Shaders").mkdir()
                (root / "Shaders/test.metallib").write_bytes(b"shader")
                with self.assertRaisesRegex(ValueError, "material-lab.*drift"):
                    run(app, root / "output", Path(__file__).with_name("reference.json"), root)
                capture.assert_not_called()
                self.assertFalse((root / "output").exists())
            shutil.copyfile(Path(__file__).resolve().parents[2] / "Assets/Scenes/material-lab.scene.gltf",
                            root / "material-lab.scene.gltf")
            (root / "temporal-lab.scene.bin").write_bytes(
                (root / "temporal-lab.scene.bin").read_bytes() + b"changed")
            with self.assertRaisesRegex(ValueError, "temporal-lab.*drift"):
                verify_documents(reference, root)

    def test_default_documents_belong_to_target_app_checkout(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            app = root / "parent/build/macosx/arm64/release/App"
            app.parent.mkdir(parents=True)
            app.write_bytes(b"parent")
            # The script checkout has valid candidate documents, but this App's
            # checkout has none. No image process may start.
            with mock.patch.object(subprocess, "run") as capture:
                with self.assertRaisesRegex(ValueError, "sponza.*document drift"):
                    run(app, root / "output", Path(__file__).with_name("reference.json"))
                capture.assert_not_called()
                self.assertFalse((root / "output").exists())
            with self.assertRaisesRegex(ValueError, "pass --documents"):
                document_root_for(root / "App", None)

    def test_default_preflight_rejects_nearer_catalog_document(self):
        reference_path = Path(__file__).with_name("reference.json")
        reference = load_reference(reference_path)
        with tempfile.TemporaryDirectory() as directory:
            checkout = Path(directory)
            app = checkout / "build/macosx/arm64/release/App"
            app.parent.mkdir(parents=True)
            app.write_bytes(b"app")
            (app.parent / "Shaders").mkdir()
            (app.parent / "Shaders/test.metallib").write_bytes(b"shader")
            catalog = checkout / "Assets/Scenes"
            catalog.mkdir(parents=True)
            for scene in reference["documents"]:
                for suffix in ("gltf", "bin"):
                    source = Path(__file__).resolve().parents[2] / "Assets/Scenes" / f"{scene}.scene.{suffix}"
                    shutil.copyfile(source, catalog / source.name)
            nearer = app.parent / "Assets/Scenes"
            nearer.mkdir(parents=True)
            (nearer / "sponza.scene.gltf").write_bytes(b"{}")
            output = checkout / "output"
            with mock.patch.object(subprocess, "run") as capture:
                with self.assertRaisesRegex(ValueError, "sponza.*document drift"):
                    run(app, output, reference_path)
                capture.assert_not_called()
            self.assertFalse(output.exists())
            # A valid nearer document is the runtime input, even if the root copy drifts.
            source = Path(__file__).resolve().parents[2] / "Assets/Scenes/sponza.scene.gltf"
            shutil.copyfile(source, nearer / source.name)
            source = source.with_suffix(".bin")
            shutil.copyfile(source, nearer / source.name)
            (catalog / "sponza.scene.gltf").write_bytes(b"[]")
            self.assertEqual(verify_documents(reference, catalog, app), reference["documents"])

            # ExistingPath also selects a directory; App will not skip it for the root file.
            shadow = nearer / "sponza.scene.gltf"
            shadow.unlink()
            shadow.mkdir()
            shutil.copyfile(Path(__file__).resolve().parents[2] / "Assets/Scenes/sponza.scene.gltf",
                            catalog / "sponza.scene.gltf")
            with mock.patch.object(subprocess, "run") as capture:
                with self.assertRaisesRegex(ValueError, "sponza.*document drift"):
                    run(app, output, reference_path)
                capture.assert_not_called()
            self.assertFalse(output.exists())

    def test_malformed_document_shapes_refuse_with_scene_name(self):
        reference_path = Path(__file__).with_name("reference.json")
        reference = load_reference(reference_path)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            app = root / "App"
            app.write_bytes(b"app")
            (root / "Shaders").mkdir()
            (root / "Shaders/test.metallib").write_bytes(b"shader")
            documents = root / "documents"
            documents.mkdir()
            for scene in reference["documents"]:
                for suffix in ("gltf", "bin"):
                    source = Path(__file__).resolve().parents[2] / "Assets/Scenes" / f"{scene}.scene.{suffix}"
                    shutil.copyfile(source, documents / source.name)
            for content in (b"[]", b'{"buffers":[1]}'):
                with self.subTest(content=content):
                    (documents / "sponza.scene.gltf").write_bytes(content)
                    output = root / "output"
                    with mock.patch.object(subprocess, "run") as capture:
                        with self.assertRaisesRegex(ValueError, "sponza.*document drift"):
                            run(app, output, reference_path, documents)
                        capture.assert_not_called()
                    self.assertFalse(output.exists())

    def test_legacy_reference_remains_usable_with_explicit_documents(self):
        original = Path(__file__).with_name("reference.json")
        legacy = json.loads(original.read_text())
        legacy["schemaVersion"] = 1
        legacy.pop("documents")
        legacy["images"] = [row for row in legacy["images"] if row["scene"] != "temporal-lab"]
        retired = ["damaged-helmet-off", "damaged-helmet-taa-1", "damaged-helmet-taa-0.5",
                   "damaged-helmet-vendor-1", "damaged-helmet-vendor-0.5"]
        modes = [("off", 1), ("taa", 1), ("taa", 0.5), ("metalfx", 1), ("metalfx", 0.5)]
        legacy["images"].extend({"name": name, "scene": "damaged-helmet", "temporal": mode,
                                 "renderScale": scale, "sha256": "a" * 64}
                                for name, (mode, scale) in zip(retired, modes))
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "original-reference.json"
            path.write_text(json.dumps(legacy))
            self.assertEqual(load_reference(path)["schemaVersion"], 1)
            row = next(row for row in legacy["images"] if row["name"] == "damaged-helmet-off")
            self.assertEqual(command_for(Path("/build/App"), row, root / "out.bmp", root)[2],
                             str(root / "damaged-helmet.scene.gltf"))

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
            documents = root / "documents"
            documents.mkdir()
            for scene in ("sponza", "material-lab", "temporal-lab"):
                for suffix in ("gltf", "bin"):
                    source = Path(__file__).resolve().parents[2] / "Assets/Scenes" / f"{scene}.scene.{suffix}"
                    shutil.copyfile(source, documents / source.name)
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
                self.assertFalse(run(app, root / "output", Path(__file__).with_name("reference.json"), documents))
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
            reference = load_reference(Path(__file__).with_name("reference.json"))
            reference["documents"].pop("temporal-lab")
            path.write_text(json.dumps(reference))
            with self.assertRaisesRegex(ValueError, "three scene document"):
                load_reference(path)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--reference", type=Path, default=Path(__file__).with_name("reference.json"))
    parser.add_argument("--documents", type=Path,
                        help="map reference scene ids to this frozen document directory")
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    if args.selftest:
        result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(ParityTests))
        return 0 if result.wasSuccessful() else 1
    if args.app is None or args.output is None:
        parser.error("--app and --output are required unless --selftest is used")
    try:
        return 0 if run(args.app, args.output, args.reference, args.documents) else 1
    except (OSError, ValueError, KeyError) as error:
        print(f"parity refused: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
