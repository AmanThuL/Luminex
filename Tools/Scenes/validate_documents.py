#!/usr/bin/env python3
"""Run the pinned Khronos validator on catalog and writer-test scene documents."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from urllib.parse import unquote_to_bytes


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_GENERATED = ROOT / "build/macosx/arm64/release/test/SceneDocuments"
DEFAULT_VALIDATOR = ROOT / "ThirdParty/glTF-Validator/gltf_validator"


@dataclass(frozen=True)
class Documents:
    catalog: tuple[Path, ...]
    generated: tuple[Path, ...]


@dataclass(frozen=True)
class Report:
    error_count: int
    errors: tuple[str, ...]


def discover_documents(root: Path, generated_dir: Path) -> Documents:
    catalog = tuple(sorted((root / "Assets/Scenes").glob("*.scene.gltf")))
    generated = tuple(sorted(generated_dir.rglob("*.gltf")))
    return Documents(catalog, generated)


def catalog_status(documents: Documents) -> str:
    if not documents.catalog:
        return "Catalog has no documents; catalog validation gate has not run."
    return f"Catalog documents: {len(documents.catalog)}"


def parse_report(data: str) -> Report:
    try:
        payload = json.loads(data)
        issues = payload["issues"]
        count = issues["numErrors"]
        messages = issues["messages"]
        truncated = issues["truncated"]
    except (json.JSONDecodeError, KeyError, TypeError) as error:
        raise ValueError(f"invalid validator JSON report: {error}") from error
    if not isinstance(count, int) or isinstance(count, bool) or count < 0:
        raise ValueError("invalid validator error count")
    if not isinstance(messages, list):
        raise ValueError("invalid validator messages")
    if truncated is not False:
        raise ValueError("validator report is truncated")

    errors: list[str] = []
    for message in messages:
        if not isinstance(message, dict):
            raise ValueError("invalid validator message")
        if message.get("severity") != 0:
            continue
        code = message.get("code")
        detail = message.get("message")
        pointer = message.get("pointer")
        if not isinstance(code, str) or not isinstance(detail, str) or not isinstance(pointer, str):
            raise ValueError("invalid validator error message")
        errors.append(f"{pointer or '/'} {code}: {detail}")
    if len(errors) != count:
        raise ValueError(f"validator reports {count} errors but supplies {len(errors)} error messages")
    return Report(count, tuple(errors))


def validate_one(document: Path, binary: Path) -> list[str]:
    try:
        completed = subprocess.run(
            [str(binary), "--stdout", str(document)], capture_output=True, text=True, check=False
        )
    except OSError as error:
        return [
            f"{document}: cannot start glTF Validator ({error}). The pinned macOS binary needs "
            "Rosetta on Apple Silicon; install it with "
            "`softwareupdate --install-rosetta --agree-to-license`."
        ]

    try:
        report = parse_report(completed.stdout)
    except ValueError as error:
        detail = completed.stderr.strip()
        return [f"{document}: {error}; exit={completed.returncode}; stderr={detail}"]
    errors = [f"{document}: {message}" for message in report.errors]
    if completed.returncode != 0 and not errors:
        errors.append(
            f"{document}: validator exited {completed.returncode} without reported errors; "
            f"stderr={completed.stderr.strip()}"
        )
    return errors



def decode_uri(uri: str) -> str:
    """Decode the relative file URI syntax accepted by the document reader."""
    if re.search(r"%(?![0-9a-fA-F]{2})", uri):
        raise ValueError("malformed percent escape in relative file URI")
    if re.search(r"[^a-zA-Z0-9_.~/%!$&'()*+,;=@-]", uri):
        raise ValueError("relative file URI contains an unescaped or reserved character")
    decoded = unquote_to_bytes(uri).decode("utf-8")
    if not decoded or any(character in decoded for character in ("\0", "\\", ":")):
        raise ValueError("expected a relative file path without NUL, backslash or scheme")
    path = Path(decoded)
    if path.is_absolute() or ".." in path.parts:
        raise ValueError("relative file path must not be absolute or traverse a parent directory")
    return decoded


def content_path(uri: str, document: Path, member: str | None = None) -> Path:
    """Confine an immutable content URI to this document's geometry or texture names."""
    decoded = decode_uri(uri)
    path = Path(decoded)
    geometry = decoded == document.stem + ".geometry.bin"
    image = path.suffix == ".png" and path.parts[0] == document.stem + ".textures"
    if (member == "buffers" and not geometry) or (member == "images" and not image) or (
            not geometry and not image):
        raise ValueError("expected this document's geometry buffer or a PNG below its texture folder")
    return document.parent / path


def check_content_hashes(document: Path) -> list[str]:
    """Check every declared content hash and require hashes for geometry and images."""
    try:
        payload = json.loads(document.read_text(encoding="utf-8"))
        if not isinstance(payload, dict):
            raise ValueError("expected a document object")
        extensions = payload.get("extensions", {})
        if not isinstance(extensions, dict):
            raise ValueError("/extensions: expected an object")
        lmx = extensions.get("LMX_scene", {})
        if not isinstance(lmx, dict):
            raise ValueError("/extensions/LMX_scene: expected an object")
        hashes = lmx.get("contentHashes", {})
        if not isinstance(hashes, dict):
            raise ValueError("/extensions/LMX_scene/contentHashes: expected an object")
    except (OSError, UnicodeError, ValueError) as error:
        return [f"{document}: {error}"]

    failures: list[str] = []
    references: dict[str, tuple[str, str]] = {}
    for member in ("buffers", "images"):
        entries = payload.get(member, [])
        if not isinstance(entries, list):
            failures.append(f"{document}: /{member}: expected an array")
            continue
        for index, entry in enumerate(entries):
            pointer = f"/{member}/{index}/uri"
            uri = entry.get("uri") if isinstance(entry, dict) else None
            if not isinstance(uri, str):
                failures.append(f"{document}: {pointer}: expected a relative file URI")
                continue
            # Animation bytes retain their existing validator and companion-hash semantics.
            if member == "buffers":
                try:
                    decoded = decode_uri(uri)
                    schema = lmx.get("schemaVersion", 1)
                    legacy_animation = schema == 1 and len(entries) == 1 and Path(decoded).suffix == ".bin"
                    if legacy_animation or (schema == 2 and decoded == document.stem + ".bin"):
                        continue
                except (ValueError, UnicodeError):
                    pass
            try:
                content_path(uri, document, member)
            except (ValueError, UnicodeError) as error:
                failures.append(f"{document}: {pointer}: {error}: '{uri}'")
                continue
            references.setdefault(uri, (pointer, member))

    for uri in dict.fromkeys((*references, *hashes)):
        pointer, member = references.get(uri, ("/extensions/LMX_scene/contentHashes/" +
                                               uri.replace("~", "~0").replace("/", "~1"), None))
        prefix = f"{document}: {pointer}: "
        try:
            path = content_path(uri, document, member)
        except (ValueError, UnicodeError) as error:
            failures.append(f"{prefix}{error}: '{uri}'")
            continue
        digest = hashes.get(uri)
        if digest is None:
            failures.append(f"{prefix}content hash is missing for '{uri}'")
            continue
        if not isinstance(digest, str) or not re.fullmatch(r"[0-9a-f]{64}", digest):
            failures.append(f"{prefix}expected a lowercase SHA-256 for '{uri}'")
            continue
        try:
            actual = hashlib.sha256(path.read_bytes()).hexdigest()
        except OSError as error:
            failures.append(f"{prefix}cannot read '{uri}': {error}")
            continue
        if actual != digest:
            failures.append(f"{prefix}SHA-256 mismatch for '{uri}'")
    return failures


def selftest() -> int:
    """Exercise the hash gate on disposable files without the external validator."""
    with tempfile.TemporaryDirectory(prefix="lmx-document-hashes-") as directory:
        root = Path(directory)
        document = root / "fixture.scene.gltf"
        files = {"fixture.scene.geometry.bin": b"geometry",
                 "fixture.scene.textures/image.png": b"image"}
        hashes = {uri: hashlib.sha256(data).hexdigest() for uri, data in files.items()}
        payload = {"buffers": [{"uri": "fixture.scene.geometry.bin"}],
                   "images": [{"uri": "fixture.scene.textures/image.png"}],
                   "extensions": {"LMX_scene": {"schemaVersion": 2, "contentHashes": hashes}}}
        for uri, data in files.items():
            path = root / uri
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        document.write_text(json.dumps(payload), encoding="utf-8")
        if check_content_hashes(document):
            print("Content hash selftest failed on matching files.", file=sys.stderr)
            return 1
        for uri, data in files.items():
            (root / uri).write_bytes(b"changed")
            if not check_content_hashes(document):
                print("Content hash selftest failed to reject changed bytes.", file=sys.stderr)
                return 1
            (root / uri).unlink()
            if not check_content_hashes(document):
                print("Content hash selftest failed to reject a missing file.", file=sys.stderr)
                return 1
            (root / uri).write_bytes(data)
            digest = hashes.pop(uri)
            document.write_text(json.dumps(payload), encoding="utf-8")
            if not check_content_hashes(document):
                print("Content hash selftest failed to reject a missing hash.", file=sys.stderr)
                return 1
            hashes[uri] = digest
            document.write_text(json.dumps(payload), encoding="utf-8")
    print("Content hash selftest passed.")
    return 0

def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--selftest", action="store_true")
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--generated-dir", type=Path, default=DEFAULT_GENERATED)
    parser.add_argument("--validator", type=Path, default=DEFAULT_VALIDATOR)
    args = parser.parse_args(argv)
    if args.selftest:
        return selftest()

    documents = discover_documents(args.root, args.generated_dir)
    print(catalog_status(documents))
    print(f"Writer-test documents: {len(documents.generated)} in {args.generated_dir}")
    if not documents.catalog:
        return 1
    if not documents.generated:
        state = "missing" if not args.generated_dir.is_dir() else "empty"
        print(
            f"writer-test validation did not run: {args.generated_dir} is {state}; "
            "run Tests/unit and check the SceneDocuments output path.",
            file=sys.stderr,
        )
        return 1
    if not args.validator.is_file():
        print(f"Pinned glTF Validator is missing: {args.validator}; run `xmake setup`.", file=sys.stderr)
        return 1

    failures: list[str] = []
    for document in documents.catalog:
        failures.extend(check_content_hashes(document))
    for document in (*documents.catalog, *documents.generated):
        failures.extend(validate_one(document, args.validator))
    for failure in failures:
        print(failure, file=sys.stderr)
    if failures:
        print(f"glTF validation failed for {len(failures)} error(s).", file=sys.stderr)
        return 1
    print(f"glTF validation passed for {len(documents.catalog) + len(documents.generated)} documents.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
