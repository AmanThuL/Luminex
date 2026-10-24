#!/usr/bin/env python3
"""Validate project-owned C++ file envelopes and public API documentation."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from bisect import bisect_right
from pathlib import Path
from typing import Any


try:
    from .check_module_deps import load_contract, owner_of
except ImportError:
    from check_module_deps import load_contract, owner_of


ROOT = Path(__file__).resolve().parents[1]
SOURCE_ROOTS = ("Source", "Benchmarks")
# Test sources carry no file envelope, wherever they live: the top-level Tests root is already
# outside SOURCE_ROOTS.
EXCLUDED_ROOTS = ()
SOURCE_SUFFIXES = {".h", ".cpp"}
FILE_RULER = "//" + "-" * 118


def project_cpp_files(root: Path) -> list[Path]:
    """Return project-owned headers and implementations, excluding test sources."""
    excluded = tuple((root / name).resolve() for name in EXCLUDED_ROOTS)
    files: list[Path] = []
    for name in SOURCE_ROOTS:
        source_root = root / name
        if source_root.is_dir():
            files.extend(
                path
                for path in source_root.rglob("*")
                if path.is_file()
                and path.suffix in SOURCE_SUFFIXES
                and not any(path.resolve().is_relative_to(base) for base in excluded)
            )
    return sorted(files)


def public_header_files(root: Path, contract: dict | None = None) -> list[Path]:
    """Return headers whose declarations form project API.

    Source visibility follows the module contract.
    """
    source = (root / "Source").rglob("*.h") if (root / "Source").is_dir() else ()
    contract_path = root / "Tools/module_contract.json"
    private: set[str] = set()
    if contract is None and contract_path.exists():
        contract = load_contract(contract_path, root)
    if contract is not None:
        private = {header for unit in contract["units"].values() for header in unit["privateHeaders"]}
    private_identities = set()
    for name in private:
        status = (root / name).stat()
        private_identities.add((status.st_dev, status.st_ino))
    public = []
    for path in [*source]:
        status = path.stat()
        if (status.st_dev, status.st_ino) not in private_identities:
            public.append(path)
    return sorted(public)


def check_file_header(path: Path, text: str) -> list[str]:
    """Validate the exact four-line file envelope at the first physical line."""
    lines = text.splitlines()
    if len(lines) < 4:
        return [f"{path}:1: missing four-line file header envelope"]

    errors: list[str] = []
    if lines[0] != FILE_RULER:
        errors.append(f"{path}:1: missing 120-character file header ruler")
    if lines[1] != f"/// @file {path.name}":
        errors.append(f"{path}:2: expected `/// @file {path.name}`")
    if not lines[2].startswith("/// @brief ") or not lines[2][11:].strip():
        errors.append(f"{path}:3: expected a non-empty `/// @brief` summary")
    if lines[3] != FILE_RULER:
        errors.append(f"{path}:4: missing closing 120-character file header ruler")
    return errors


def _json_objects(text: str) -> list[dict[str, Any]]:
    """Decode Clang's concatenated JSON objects produced by ast-dump-filter."""
    decoder = json.JSONDecoder()
    objects: list[dict[str, Any]] = []
    offset = 0
    while offset < len(text):
        while offset < len(text) and text[offset].isspace():
            offset += 1
        if offset == len(text):
            break
        value, offset = decoder.raw_decode(text, offset)
        if isinstance(value, dict):
            objects.append(value)
    return objects


def _line_for(node: dict[str, Any], line_offsets: list[int]) -> int:
    loc = node.get("loc", {})
    if "line" in loc:
        return int(loc["line"])
    return bisect_right(line_offsets, int(loc.get("offset", 0))) + 1


def _has_comment(node: dict[str, Any]) -> bool:
    return any(child.get("kind") == "FullComment" for child in node.get("inner", []))


def _declaration_has_comment(node: dict[str, Any]) -> bool:
    if _has_comment(node):
        return True
    # Template nodes wrap the declaration that owns the parsed comment.
    return any(
        child.get("kind") != "FullComment" and _has_comment(child)
        for child in node.get("inner", [])
    )


def _is_definition(node: dict[str, Any]) -> bool:
    kind = node.get("kind")
    if kind in {"CXXRecordDecl", "RecordDecl"}:
        return node.get("completeDefinition") is True
    if kind == "EnumDecl":
        return any(child.get("kind") == "EnumConstantDecl" for child in node.get("inner", []))
    return True


def public_api_findings_from_ast(
    path: Path, text: str, ast_objects: list[dict[str, Any]]
) -> list[str]:
    """Return undocumented API declarations from a Clang JSON AST."""
    target = path.as_posix()
    line_offsets = [index for index, char in enumerate(text) if char == "\n"]
    findings: list[str] = []
    seen: set[tuple[str, int, str]] = set()
    reported_lines: set[int] = set()
    declaration_kinds = {
        "CXXConstructorDecl",
        "CXXConversionDecl",
        "CXXDestructorDecl",
        "CXXMethodDecl",
        "FieldDecl",
        "FunctionDecl",
        "FunctionTemplateDecl",
        "TypeAliasDecl",
        "TypeAliasTemplateDecl",
        "TypedefDecl",
        "VarDecl",
    }

    def report(node: dict[str, Any], inherited_file: str | None) -> None:
        loc = node.get("loc", {})
        node_file = loc.get("file", inherited_file)
        if node_file is None or Path(node_file).as_posix() != target:
            return
        if node.get("isImplicit") or node.get("explicitlyDefaulted"):
            return
        line = _line_for(node, line_offsets)
        key = (str(node.get("kind")), int(loc.get("offset", -1)), str(node.get("name", "")))
        has_comment = (
            _declaration_has_comment(node)
            if str(node.get("kind", "")).endswith("TemplateDecl")
            else _has_comment(node)
        )
        if key in seen or line in reported_lines or has_comment:
            return
        seen.add(key)
        reported_lines.add(line)
        findings.append(f"{path}:{line}: public API declaration has no Doxygen comment")

    def visit(node: dict[str, Any], inherited_file: str | None, visible: bool) -> None:
        loc = node.get("loc", {})
        node_file = loc.get("file", inherited_file)
        kind = node.get("kind")
        children = node.get("inner", [])

        if kind in {"NamespaceDecl", "TranslationUnitDecl", "LinkageSpecDecl"}:
            for child in children:
                visit(child, node_file, visible)
            return

        if kind in {"CXXRecordDecl", "RecordDecl"}:
            if not _is_definition(node):  # Forward declarations are intentionally exempt.
                return
            if visible:
                report(node, node_file)
            access = "public" if node.get("tagUsed") == "struct" else "private"
            for child in children:
                if child.get("kind") == "AccessSpecDecl":
                    access = child.get("access", access)
                elif child.get("kind") not in {"FullComment", "CXXRecordDecl"} or child.get(
                    "completeDefinition"
                ):
                    visit(child, node_file, visible and access in {"public", "protected"})
            return

        if kind == "EnumDecl":
            if not _is_definition(node):
                return
            if visible:
                report(node, node_file)
                for child in children:
                    if child.get("kind") == "EnumConstantDecl":
                        report(child, node_file)
            return

        if kind in declaration_kinds and visible:
            report(node, node_file)
            return

        # Clang may insert template and export wrappers between a namespace and its declaration.
        if kind in {"ClassTemplateDecl", "ClassTemplateSpecializationDecl", "ExportDecl"}:
            if visible and kind.startswith("ClassTemplate"):
                report(node, node_file)
            for child in children:
                if child.get("kind") != "FullComment":
                    visit(child, node_file, visible)

    for ast in ast_objects:
        visit(ast, None, True)
    return sorted(findings)


def _compile_entries(path: Path) -> list[dict[str, Any]]:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise RuntimeError(f"cannot read compilation database {path}: {exc}") from exc


def _root_relative(file: str, root: Path, directory: Path | None = None) -> str:
    path = (directory or root) / file
    try:
        return path.resolve().relative_to(root.resolve()).as_posix()
    except ValueError:
        return path.resolve().as_posix()


def _entry_for_header(
    header: Path, entries: list[dict[str, Any]], root: Path, contract: dict
) -> dict[str, Any]:
    relative = header.relative_to(root)
    unit = owner_of(relative, contract)
    if unit is None:
        raise RuntimeError(f"no unit owns {relative.as_posix()}")
    candidates: list[tuple[str, dict[str, Any]]] = []
    for entry in entries:
        directory = (root / Path(entry.get("directory") or root)).resolve()
        source = _root_relative(entry["file"], root, directory)
        if owner_of(Path(source), contract) == unit:
            candidates.append((source, entry))
    if not candidates:
        raise RuntimeError(f"no compiled source found for unit {unit} to check {relative.as_posix()}")
    # Prefer a counterpart only within the declared owner, then keep selection stable across DB order.
    return min(
        candidates,
        key=lambda item: (Path(item[0]).stem != header.stem, item[0], json.dumps(item[1], sort_keys=True)),
    )[1]


def _ast_for_header(
    header: Path, entries: list[dict[str, Any]], root: Path, contract: dict | None = None
) -> tuple[list[dict[str, Any]], str | None]:
    if contract is None:
        contract = load_contract(root / "Tools/module_contract.json", root)
    try:
        entry = _entry_for_header(header, entries, root, contract)
    except RuntimeError as exc:
        return [], str(exc)
    directory = (root / Path(entry.get("directory") or root)).resolve()
    arguments = list(entry.get("arguments", []))
    if not arguments:
        return [], "compilation database entry has no argument vector"
    source = _root_relative(entry["file"], root, directory)
    command: list[str] = []
    skip = False
    for argument in arguments:
        if skip:
            skip = False
            continue
        if argument == "-o":
            skip = True
        elif argument != "-c" and (argument.startswith("-") or _root_relative(argument, root, directory) != source):
            command.append(argument)
    command.extend(
        [
            "-x",
            "c++",
            header.relative_to(root).as_posix() if directory == root else str(header),
            "-fsyntax-only",
            "-fparse-all-comments",
            "-Wdocumentation",
            "-Werror=documentation",
            "-Xclang",
            "-ast-dump=json",
            "-Xclang",
            "-ast-dump-filter=lmx",
        ]
    )
    result = subprocess.run(command, cwd=directory, capture_output=True, text=True, check=False)
    if result.returncode:
        return [], result.stderr.strip() or "Clang failed without diagnostics"
    try:
        objects = _json_objects(result.stdout)
    except json.JSONDecodeError as exc:
        return [], f"cannot decode Clang AST: {exc}"
    if directory != root:
        def normalize_files(value: Any) -> None:
            if isinstance(value, dict):
                for key, child in value.items():
                    if key == "file" and isinstance(child, str):
                        value[key] = _root_relative(child, root, directory)
                    else:
                        normalize_files(child)
            elif isinstance(value, list):
                for child in value:
                    normalize_files(child)
        normalize_files(objects)
    return objects, None


def compiler_public_api_findings(root: Path, compile_commands: Path) -> list[str]:
    """Compile exported headers and report missing or malformed public API documentation."""
    entries = _compile_entries(compile_commands)
    contract = load_contract(root / "Tools/module_contract.json", root)
    findings: list[str] = []
    for header in public_header_files(root, contract):
        relative = header.relative_to(root)
        ast, error = _ast_for_header(header, entries, root, contract)
        if error:
            findings.append(f"{relative}: public-header documentation compile failed:\n{error}")
            continue
        text = header.read_text(encoding="utf-8")
        findings.extend(public_api_findings_from_ast(relative, text, ast))
    return findings


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT, help=argparse.SUPPRESS)
    parser.add_argument(
        "--compile-commands", type=Path, default=Path("compile_commands.json"), help=argparse.SUPPRESS
    )
    parser.add_argument(
        "--public-api-docs",
        choices=("off", "warn", "error"),
        default="error",
        help="handling for undocumented or malformed public API declarations",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    root = args.root.resolve()
    files = project_cpp_files(root)
    errors: list[str] = []
    warnings: list[str] = []
    for path in files:
        relative = path.relative_to(root)
        try:
            text = path.read_text(encoding="utf-8")
        except (OSError, UnicodeError) as exc:
            errors.append(f"{relative}: cannot read source: {exc}")
            continue
        errors.extend(check_file_header(relative, text))

    if args.public_api_docs != "off":
        compile_commands = args.compile_commands
        if not compile_commands.is_absolute():
            compile_commands = root / compile_commands
        try:
            findings = compiler_public_api_findings(root, compile_commands)
        except RuntimeError as exc:
            findings = [str(exc)]
        (warnings if args.public_api_docs == "warn" else errors).extend(findings)

    for warning in warnings:
        print(f"warning: {warning}", file=sys.stderr)
    if errors:
        for error in errors:
            print(f"error: {error}", file=sys.stderr)
        print(f"C++ comment check failed with {len(errors)} error(s)", file=sys.stderr)
        return 1
    print(f"C++ comment check passed ({len(files)} owned files checked)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
