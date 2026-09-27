#!/usr/bin/env python3
"""Run the pinned Khronos validator on catalog and writer-test scene documents."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


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


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--generated-dir", type=Path, default=DEFAULT_GENERATED)
    parser.add_argument("--validator", type=Path, default=DEFAULT_VALIDATOR)
    args = parser.parse_args(argv)

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
