#!/usr/bin/env python3
"""Check Slang import boundaries and the flat runtime shader artifact namespace."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TOKENS = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"', re.DOTALL)
IMPORT = re.compile(r"\bimport\b([^;]*)(?:;|$)")
MODULE_NAME = re.compile(r"[A-Za-z_]\w*(?:(?:::|\.)[A-Za-z_]\w*)*")
ENTRY = re.compile(r'\[\s*shader\s*\(')
INCLUDE = re.compile(r"^\s*#\s*include\b", re.MULTILINE)
# The repository's Slang tree, serving App, Tests and the benchmark, and the folders it is laid out
# in: modules shared by more than one pass family in Common/, oracles in Tests/, and one folder per
# family under Passes/. The RHI component owns its own self-contained tree and checker.
TREE = "Shaders"
COMMON = "Common"
TESTS = "Tests"
PASSES = "Passes"


def without_comments(text: str) -> str:
    """Preserve strings and line numbers while masking both comment forms."""
    return TOKENS.sub(
        lambda match: match.group() if match.group().startswith('"') else re.sub(r"[^\n]", " ", match.group()),
        text,
    )


def placement(relative: Path) -> tuple[str, str]:
    """Locate a source by the folder whose rules govern it and, for a pass, its family.

    A source the layout has no place for reports an empty folder.
    """
    parts = relative.parts
    if len(parts) > 1 and parts[0] in (COMMON, TESTS):
        return parts[0], ""
    if len(parts) == 3 and parts[0] == PASSES:
        return PASSES, parts[1]
    return "", ""


def check_shaders(root: Path) -> tuple[list[str], int, int]:
    """Check the repository's Slang tree, which owns one runtime artifact namespace.

    The RHI component's tree is deliberately independent: it compiles into its own target directory,
    so it may own the same output basenames, it may not import this tree's modules, and its own
    checker covers it.
    """
    shaders = root / TREE
    if not shaders.is_dir():
        return [], 0, 0
    return check_tree(root, shaders)


def check_tree(root: Path, shaders: Path) -> tuple[list[str], int, int]:
    files = sorted(shaders.rglob("*.slang"))
    errors: list[str] = []
    names: dict[str, Path] = {}
    # Every source with the folder rules it answers to, its family, and its text with comments and
    # strings masked. Imports resolve only once the whole tree has declared its modules.
    sources: list[tuple[Path, str, str, str, str]] = []
    # Import name to defining file and the family that owns it; an empty family is shared.
    modules: dict[str, tuple[Path, str]] = {}
    for path in files:
        reported = path.relative_to(root)
        relative = path.relative_to(shaders)
        key = path.stem.casefold()
        if key in names:
            errors.append(
                f"{reported}: duplicate shader output basename '{path.stem}' "
                f"also owned by {names[key].relative_to(root)}"
            )
        names[key] = path
        text = without_comments(path.read_text(encoding="utf-8"))
        # Strings cannot contain directives, but a quoted import operand must still be rejected
        # explicitly instead of disappearing while strings are masked. Preserve line numbers.
        masked = re.sub(
            r'"(?:\\.|[^"\\])*"', lambda match: re.sub(r"[^\n]", "?", match.group()), text
        )
        folder, family = placement(relative)
        sources.append((path, folder, family, text, masked))
        if not folder:
            errors.append(f"{reported}: a shader source must live in Common/, Tests/ or Passes/<family>/")
        elif folder == COMMON:
            if ENTRY.search(masked):
                errors.append(f"{reported}: shared modules cannot declare shader entry points")
            modules[".".join(relative.relative_to(folder).with_suffix("").parts)] = (path, "")
        elif folder == PASSES and not ENTRY.search(masked):
            modules[path.stem] = (path, family)

    # The files that define a module; every other file the tree holds is an entry point.
    defined = {path for path, _ in modules.values()}
    imports = 0
    for path, folder, family, text, masked in sources:
        relative = path.relative_to(root)
        for match in INCLUDE.finditer(masked):
            line = text.count("\n", 0, match.start()) + 1
            errors.append(f"{relative}:{line}: use module imports instead of textual includes")
        for match in IMPORT.finditer(masked):
            imports += 1
            line = text.count("\n", 0, match.start()) + 1
            name = match.group(1).strip()
            if not MODULE_NAME.fullmatch(name) or not match.group().endswith(";"):
                errors.append(f"{relative}:{line}: expected a named module import ending in ';'")
                continue
            name = name.replace("::", ".")
            owner = modules.get(name)
            if owner is not None:
                _, home = owner
                # An oracle tests any module; otherwise a family's own module stays inside it.
                if not home or folder == TESTS or home == family:
                    continue
                errors.append(
                    f"{relative}:{line}: import '{name}' is local to "
                    f"{(shaders / PASSES / home).relative_to(root)}; "
                    "only sources in that folder may import it"
                )
                continue
            other = names.get(name.split(".")[-1].casefold())
            if other is None or other in defined:
                errors.append(f"{relative}:{line}: unresolved module import '{name}'")
                continue
            target = other.relative_to(root)
            errors.append(
                f"{relative}:{line}: import '{name}' reaches entry point {target}; "
                "only modules are importable"
            )
    return errors, len(files), imports


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT, help=argparse.SUPPRESS)
    args = parser.parse_args()
    try:
        errors, files, imports = check_shaders(args.root.resolve())
    except (OSError, UnicodeError) as exc:
        print(f"shader import check could not run: {exc}", file=sys.stderr)
        return 2
    if not files:
        errors.append("Shaders: no Slang sources found")
    for error in errors:
        print(f"error: {error}", file=sys.stderr)
    if errors:
        print(f"shader import check failed with {len(errors)} error(s)", file=sys.stderr)
        return 1
    print(f"shader import check passed ({files} shaders, {imports} module imports)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
