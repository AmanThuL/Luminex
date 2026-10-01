#!/usr/bin/env python3
"""Write proposal sidecars for scene documents.

The bridge command client is added with the command bridge slice.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import sys
import tempfile
from urllib.parse import unquote_to_bytes


_URI_CHARACTERS = re.compile(r"[A-Za-z0-9._~!$&'()*+,;=@/-]*\Z")
_HEX_ESCAPE = re.compile(r"%[0-9A-Fa-f]{2}")


def _buffer_path(document: Path, uri: str) -> Path:
    """Decode the relative external buffer URI accepted by scene documents."""
    if not uri or not isinstance(uri, str):
        raise ValueError("buffer uri must be a nonempty string")
    unescaped = _HEX_ESCAPE.sub("", uri)
    if "%" in unescaped or not _URI_CHARACTERS.fullmatch(unescaped):
        raise ValueError("buffer URI contains an invalid escape or character")
    try:
        decoded = unquote_to_bytes(uri).decode("utf-8")
    except UnicodeDecodeError as error:
        raise ValueError("buffer URI is not UTF-8") from error
    path = Path(decoded)
    if (not decoded or path.is_absolute() or ".." in path.parts or
            any(character in decoded for character in ("\x00", "\\", ":")) or
            path.suffix != ".bin"):
        raise ValueError("buffer URI must name a relative external .bin without parent traversal")
    return document.parent / path


def document_hash(document: Path) -> str:
    """Match sceneDocumentHash over raw glTF bytes followed by referenced buffer bytes."""
    try:
        gltf = document.read_bytes()
    except OSError as error:
        raise ValueError(f"cannot read document '{document}': {error.strerror}") from error
    try:
        text = gltf.decode("utf-8")
    except UnicodeDecodeError as error:
        raise ValueError(f"glTF JSON must be UTF-8: {error}") from error

    def reject_constant(value: str):
        raise ValueError(f"invalid JSON constant {value}")

    def unique_object(pairs: list[tuple[str, object]]) -> dict:
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError(f"duplicate JSON key {key!r}")
            result[key] = value
        return result

    try:
        parsed = json.loads(text, parse_constant=reject_constant, object_pairs_hook=unique_object)
    except (json.JSONDecodeError, ValueError) as error:
        raise ValueError(f"invalid glTF JSON: {error}") from error

    def validate_strings(value):
        if isinstance(value, str):
            try:
                value.encode("utf-8")
            except UnicodeEncodeError as error:
                raise ValueError("invalid JSON surrogate escape") from error
        elif isinstance(value, dict):
            for key, item in value.items():
                validate_strings(key)
                validate_strings(item)
        elif isinstance(value, list):
            for item in value:
                validate_strings(item)

    validate_strings(parsed)
    if not isinstance(parsed, dict):
        raise ValueError("glTF JSON must be an object")

    buffers = parsed.get("buffers")
    if buffers is None and "buffers" not in parsed:
        return hashlib.sha256(gltf).hexdigest()
    if not isinstance(buffers, list) or len(buffers) != 1 or not isinstance(buffers[0], dict):
        raise ValueError("buffers must contain exactly one external buffer")
    entry = buffers[0]
    if not isinstance(entry.get("uri"), str):
        raise ValueError("buffer uri must be a string")
    buffer = _buffer_path(document, entry["uri"])
    length = entry.get("byteLength")
    if isinstance(length, bool) or not isinstance(length, int) or length <= 0:
        raise ValueError("buffer byteLength must be a positive integer")
    try:
        binary = buffer.read_bytes()
    except OSError as error:
        raise ValueError(f"cannot read buffer '{buffer}': {error.strerror}") from error
    if len(binary) != length:
        raise ValueError(f"buffer byteLength is {len(binary)}, expected {length}")
    digest = hashlib.sha256(gltf)
    digest.update(binary)
    return digest.hexdigest()


def sidecar_path(document: Path) -> Path:
    """Use the same name as the Session sidecar reader."""
    suffix = ".scene.gltf"
    if document.name.endswith(suffix):
        return document.with_name(document.name[:-len(suffix)] + ".scene.proposal.json")
    return document.with_name(document.name + ".proposal.json")


def write_sidecar(document: Path, actor: str, summary: str, evidence: list[Path]) -> Path:
    """Validate the pair, then atomically publish its sidecar beside the glTF."""
    destination = sidecar_path(document)
    digest = document_hash(document)
    relative_evidence = [os.path.relpath(path.resolve(), destination.parent.resolve())
                         for path in evidence]
    payload = {
        "schema": 1,
        "actor": actor,
        "summary": summary,
        "evidence": relative_evidence,
        "documentSha256": digest,
    }
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", dir=destination.parent,
                                         prefix=".lmx-sidecar-", suffix=".tmp", delete=False) as output:
            temporary = Path(output.name)
            json.dump(payload, output, ensure_ascii=False, indent=2)
            output.write("\n")
        os.replace(temporary, destination)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
    return destination


def selftest() -> None:
    """Exercise the public sidecar path with a small external-buffer document."""
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        document = root / "check.scene.gltf"
        gltf = b'{"buffers":[{"uri":"other.bin","byteLength":2}]}'
        binary = b"\x00\xff"
        document.write_bytes(gltf)
        (root / "other.bin").write_bytes(binary)
        sidecar = write_sidecar(document, "Selftest", "Check pair", [])
        expected = hashlib.sha256(gltf + binary).hexdigest()
        assert json.loads(sidecar.read_text(encoding="utf-8"))["documentSha256"] == expected
        print("sidecar selftest passed")


def main(argv: list[str] | None = None) -> int:
    """Parse the sidecar command and report document errors without a traceback."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--selftest", action="store_true", help="run a local sidecar check")
    commands = parser.add_subparsers(dest="command")
    sidecar = commands.add_parser("sidecar", help="write a scene proposal sidecar")
    sidecar.add_argument("document", type=Path)
    sidecar.add_argument("--actor", required=True)
    sidecar.add_argument("--summary", required=True)
    sidecar.add_argument("--evidence", action="append", type=Path, default=[])
    args = parser.parse_args(argv)
    if args.selftest:
        selftest()
        return 0
    if args.command != "sidecar":
        parser.error("choose sidecar or --selftest")
    try:
        output = write_sidecar(args.document, args.actor, args.summary, args.evidence)
    except (ValueError, OSError) as error:
        print(f"sidecar: {error}", file=sys.stderr)
        return 1
    print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
