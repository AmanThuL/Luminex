#!/usr/bin/env python3
"""Check the repository module contract: unit ownership against xmake target membership."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
CONTRACT_PATH = ROOT / "Tools" / "module_contract.json"
ALLOWLIST_PATH = ROOT / "Tools" / "module_allowlist.json"
TARGET_DUMP_SCRIPT = "Tools/xmake_targets.lua"
SCHEMA_VERSION = 1
SOURCE_SUFFIXES = (".h", ".cpp", ".mm")
COMPILED_SUFFIXES = (".cpp", ".mm")
UNIT_LIST_FIELDS = ("paths", "targets", "units", "headers", "thirdParty")
ALLOWLIST_FIELDS = {
    "shared-source": ("file", "targets"),
    "include": ("file", "reaches"),
    "target-dep": ("target", "dep"),
    "framework": ("target", "framework"),
    "header": ("file",),
}


class ModuleContractError(RuntimeError):
    """The checker could not obtain a trustworthy view of the contract or of the build."""


def as_list(value: Any) -> list[Any]:
    """Normalise an xmake field: a single value arrives bare and an empty table as an object."""
    if value is None:
        return []
    if isinstance(value, list):
        return value
    if isinstance(value, dict):
        if value:
            raise ModuleContractError(f"expected a list, found a mapping: {value!r}")
        return []
    return [value]


def read_json(path: Path) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError as exc:
        raise ModuleContractError(f"{path} is missing") from exc
    except (OSError, json.JSONDecodeError) as exc:
        raise ModuleContractError(f"cannot read {path}: {exc}") from exc


def load_contract(path: Path, root: Path | None = None) -> dict:
    """Load and validate the contract: schema, unit references, target section, existing paths."""
    root = root or path.resolve().parents[1]
    contract = read_json(path)
    if not isinstance(contract, dict):
        raise ModuleContractError(f"{path} must hold an object")
    if contract.get("schemaVersion") != SCHEMA_VERSION:
        raise ModuleContractError(
            f"{path} has schemaVersion {contract.get('schemaVersion')!r}, expected {SCHEMA_VERSION}"
        )

    units = contract.get("units")
    if not isinstance(units, dict) or not units:
        raise ModuleContractError(f"{path} must list at least one unit")
    for name, unit in units.items():
        if not isinstance(unit, dict):
            raise ModuleContractError(f"unit {name} must hold an object")
        for field in UNIT_LIST_FIELDS:
            value = unit.setdefault(field, [])
            if not isinstance(value, list) or not all(isinstance(item, str) for item in value):
                raise ModuleContractError(f"unit {name} field {field} must hold a list of strings")
        if not unit["paths"]:
            raise ModuleContractError(f"unit {name} must own at least one path")
        for reference in unit["units"]:
            if reference not in units:
                raise ModuleContractError(f"unit {name} depends on unknown unit {reference}")
        for entry in unit["paths"]:
            if not (root / entry).exists():
                raise ModuleContractError(f"unit {name} owns {entry}, which does not exist")

    roots = contract.setdefault("roots", [])
    if not isinstance(roots, list) or not roots:
        raise ModuleContractError(f"{path} must list the roots the checker walks")
    budgets = contract.setdefault("budgets", {})
    if not isinstance(budgets, dict) or not all(isinstance(v, int) for v in budgets.values()):
        raise ModuleContractError(f"{path} budgets must map a name to a line count")
    targets = contract.setdefault("targets", {})
    if not isinstance(targets, dict):
        raise ModuleContractError(f"{path} targets must hold an object")
    for name, target in targets.items():
        if not isinstance(target, dict):
            raise ModuleContractError(f"target {name} must hold an object")
        for field in ("deps", "frameworks"):
            if field in target and not isinstance(target[field], list):
                raise ModuleContractError(f"target {name} field {field} must hold a list")
    contract.setdefault("thirdPartyTargets", [])
    return contract


def load_allowlist(path: Path) -> list[dict]:
    """Load the debt allowlist; every entry names one edge, a reason and the slice that removes it."""
    document = read_json(path)
    if not isinstance(document, dict) or document.get("schemaVersion") != SCHEMA_VERSION:
        raise ModuleContractError(f"{path} must hold an object with schemaVersion {SCHEMA_VERSION}")
    entries = document.get("entries", [])
    if not isinstance(entries, list):
        raise ModuleContractError(f"{path} entries must hold a list")
    for index, entry in enumerate(entries):
        if not isinstance(entry, dict):
            raise ModuleContractError(f"{path} entry {index} must hold an object")
        kind = entry.get("kind")
        if kind not in ALLOWLIST_FIELDS:
            raise ModuleContractError(f"{path} entry {index} has unknown kind {kind!r}")
        for field in (*ALLOWLIST_FIELDS[kind], "reason", "until"):
            if not entry.get(field):
                raise ModuleContractError(f"{path} entry {index} ({kind}) is missing {field}")
    return entries


def run_target_dump(root: Path) -> str:
    try:
        completed = subprocess.run(
            ["xmake", "lua", TARGET_DUMP_SCRIPT],
            cwd=root,
            capture_output=True,
            text=True,
            check=False,
        )
    except OSError as exc:
        raise ModuleContractError(f"cannot run xmake: {exc}") from exc
    if completed.returncode != 0:
        raise ModuleContractError(f"xmake lua {TARGET_DUMP_SCRIPT} failed: {completed.stderr.strip()}")
    return completed.stdout


def load_targets(path: Path | None, root: Path) -> dict[str, dict]:
    """Read the target dump from a file or from xmake, taking the last line of output as the object."""
    text = path.read_text(encoding="utf-8") if path and path.is_file() else None
    if text is None and path is not None:
        raise ModuleContractError(f"{path} is missing")
    if text is None:
        text = run_target_dump(root)
    lines = [line for line in text.splitlines() if line.strip()]
    if not lines:
        raise ModuleContractError("the target dump is empty")
    try:
        dump = json.loads(lines[-1])
    except json.JSONDecodeError as exc:
        raise ModuleContractError(f"the target dump is not valid JSON: {exc}") from exc
    if not isinstance(dump, dict):
        raise ModuleContractError("the target dump must hold an object of targets")

    targets: dict[str, dict] = {}
    for name, target in dump.items():
        if not isinstance(target, dict):
            raise ModuleContractError(f"target {name} in the dump must hold an object")
        targets[name] = {
            "kind": target.get("kind", ""),
            "files": [str(item) for item in as_list(target.get("files"))],
            "deps": [str(item) for item in as_list(target.get("deps"))],
            "packages": [str(item) for item in as_list(target.get("packages"))],
            "frameworks": [str(item) for item in as_list(target.get("frameworks"))],
            "targetfile": target.get("targetfile", ""),
        }
    return targets


def load_compile_commands(path: Path) -> dict[str, list[str]]:
    """Map each compiled source to its command line; the compilation database supplies context only."""
    entries = read_json(path)
    if not isinstance(entries, list):
        raise ModuleContractError(f"{path} must hold a list of compilation entries")
    commands: dict[str, list[str]] = {}
    for entry in entries:
        if not isinstance(entry, dict) or "file" not in entry:
            raise ModuleContractError(f"{path} contains a malformed entry")
        commands.setdefault(str(entry["file"]), list(entry.get("arguments", [])))
    return commands


def owner_of(path: Path, contract: dict) -> str | None:
    """Return the unit owning a repository-relative path; the longest matching entry wins."""
    text = path.as_posix()
    best: tuple[int, str] | None = None
    for name, unit in contract["units"].items():
        for entry in unit["paths"]:
            if text != entry and not text.startswith(f"{entry}/"):
                continue
            depth = len(entry.split("/"))
            if best is None or depth > best[0]:
                best = (depth, name)
    return best[1] if best else None


def project_files(root: Path, contract: dict) -> list[Path]:
    """Every header and source under the contract's roots, repository-relative and sorted."""
    files: set[Path] = set()
    for name in contract["roots"]:
        base = root / name
        if not base.is_dir():
            continue
        for path in base.rglob("*"):
            if path.is_file() and path.suffix in SOURCE_SUFFIXES:
                files.add(path.relative_to(root))
    return sorted(files, key=lambda path: path.as_posix())


def compiling_targets(targets: dict[str, dict], contract: dict) -> dict[str, list[str]]:
    """Map each compiled project source to the targets that build it, skipping vendored targets."""
    skipped = set(contract.get("thirdPartyTargets", []))
    owners: dict[str, list[str]] = {}
    for name, target in sorted(targets.items()):
        if name in skipped:
            continue
        for file in target["files"]:
            if file.endswith(COMPILED_SUFFIXES):
                owners.setdefault(file, []).append(name)
    return owners


def shared_source_allowed(file: str, names: list[str], allowlist: list[dict]) -> bool:
    return any(
        entry["kind"] == "shared-source"
        and entry["file"] == file
        and sorted(entry["targets"]) == sorted(names)
        for entry in allowlist
    )


def check_ownership(
    files: list[Path],
    targets: dict[str, dict],
    contract: dict,
    allowlist: list[dict],
    errors: list[str],
) -> None:
    """Reconcile the ownership map with target membership: one unit and one target per source."""
    owners = compiling_targets(targets, contract)
    for path in files:
        text = path.as_posix()
        unit = owner_of(path, contract)
        if unit is None:
            errors.append(f"{text}: no unit owns this file; add it to Tools/module_contract.json")
            continue
        names = owners.get(text, [])
        if len(names) > 1:
            if not shared_source_allowed(text, names, allowlist):
                errors.append(f"{text}: compiled by {', '.join(names)}; a source belongs to one target")
            continue
        allowed = contract["units"][unit]["targets"]
        if names and names[0] not in allowed:
            expected = ", ".join(allowed) or "no target"
            errors.append(f"{text}: unit {unit} builds as {expected}, but target {names[0]} compiles it")


def parse_args(argv: list[str] | None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT, help="repository root to check")
    parser.add_argument("--contract", type=Path, default=None, help="module contract JSON")
    parser.add_argument("--allowlist", type=Path, default=None, help="module allowlist JSON")
    parser.add_argument(
        "--targets", type=Path, default=None, help="target dump JSON; xmake produces it when absent"
    )
    parser.add_argument(
        "--compile-commands", type=Path, default=None, help="compilation database for include context"
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    root = args.root.resolve()
    contract_path = args.contract or root / "Tools" / "module_contract.json"
    allowlist_path = args.allowlist or root / "Tools" / "module_allowlist.json"

    errors: list[str] = []
    try:
        contract = load_contract(contract_path, root)
        allowlist = load_allowlist(allowlist_path)
        targets = load_targets(args.targets, root)
        if args.compile_commands is not None:
            load_compile_commands(args.compile_commands)
        files = project_files(root, contract)
        check_ownership(files, targets, contract, allowlist, errors)
    except ModuleContractError as exc:
        print(f"module policy could not run: {exc}", file=sys.stderr)
        return 2

    if errors:
        for error in errors:
            print(f"error: {error}", file=sys.stderr)
        print(f"module policy failed with {len(errors)} error(s)", file=sys.stderr)
        return 1
    print(f"module policy passed ({len(files)} files, {len(contract['units'])} units)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
