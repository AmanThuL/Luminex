#!/usr/bin/env python3
"""Check the repository module contract: unit ownership and include edges against the unit table."""

from __future__ import annotations

import argparse
import json
import os
import re
import shlex
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
CONTRACT_PATH = ROOT / "Tools" / "module_contract.json"
ALLOWLIST_PATH = ROOT / "Tools" / "module_allowlist.json"
TARGET_DUMP_SCRIPT = "Tools/xmake_targets.lua"
SCHEMA_VERSION = 1
SOURCE_SUFFIXES = (".h", ".cpp", ".mm")
COMPILED_SUFFIXES = (".cpp", ".mm")
UNIT_LIST_FIELDS = ("paths", "targets", "units", "headers", "forbidHeaders", "privateHeaders", "thirdParty")
EXTERNAL_LIST_FIELDS = ("paths", "targets", "includeRoots")
EXTERNAL_KIND = "external"
EXTERNAL_WILDCARD = "*"
INCLUDE_FLAGS = ("-I", "-isystem", "-iframework")
INCLUDE_PATTERN = re.compile(r'^[ \t]*#[ \t]*include[ \t]*(?:"([^"]+)"|<([^>]+)>)', re.MULTILINE)
THIRD_PARTY_DIR = "ThirdParty"
PACKAGE_DIR = "packages"
ALLOWLIST_FIELDS = {
    "shared-source": ("file", "targets"),
    "include": ("file", "reaches"),
    "target-dep": ("target", "dep"),
    "framework": ("target", "framework"),
    "header": ("file",),
}
LINKED_FRAMEWORK_PATTERN = re.compile(r"/([^/]+)\.framework/")
LINK_ONLY_ALLOWLIST_KINDS = {"framework"}
FOREIGN_ALLOWLIST_KINDS = {"header"}


class ModuleContractError(RuntimeError):
    """The checker could not obtain a trustworthy view of the contract or of the build."""


@dataclass(frozen=True)
class Resolved:
    """One include directive after resolution: a project file, a named package, or a system header."""

    kind: str  # project | external | third-party | system
    unit: str | None
    name: str | None
    path: Path | None


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
    except (OSError, ValueError) as exc:
        raise ModuleContractError(f"cannot read {path}: {exc}") from exc


def canonical_private_header(root: Path, relative: Path) -> bool:
    """Private inventory entries name real directory entries, never case or symlink aliases."""
    current = root
    try:
        for part in relative.parts:
            if part not in {entry.name for entry in current.iterdir()}:
                return False
            current /= part
            if current.is_symlink():
                return False
    except OSError:
        return False
    return True


def under(text: str, base: str) -> bool:
    """True when a repository-relative path names `base` itself or something inside it."""
    return text == base or text.startswith(f"{base}/")


def external_of(path: Path, contract: dict) -> str | None:
    """Name the external component owning a repository-relative path, or None for Luminex's own."""
    text = path.as_posix()
    for name, entry in contract.get("externals", {}).items():
        if any(under(text, entry_path) for entry_path in entry["paths"]):
            return name
    return None


def load_externals(contract: dict, root: Path, claimed: dict[str, str]) -> None:
    """Validate the external components: foreign libraries Luminex consumes but does not police.

    An entry owns repository paths that no unit may claim, declares the include roots its `"*"`
    allowance covers, and maps each consuming unit to the headers it may include.
    """
    externals = contract.setdefault("externals", {})
    if not isinstance(externals, dict):
        raise ModuleContractError("externals must hold an object of components")
    units = contract["units"]
    unit_targets = {target for unit in units.values() for target in unit["targets"]}
    for name, entry in externals.items():
        if not isinstance(entry, dict):
            raise ModuleContractError(f"external {name} must hold an object")
        if entry.get("kind") != EXTERNAL_KIND:
            raise ModuleContractError(f"external {name} must declare kind {EXTERNAL_KIND!r}")
        for field in EXTERNAL_LIST_FIELDS:
            value = entry.setdefault(field, [])
            if not isinstance(value, list) or not all(isinstance(item, str) for item in value):
                raise ModuleContractError(f"external {name} field {field} must hold a list of strings")
        if not entry["paths"]:
            raise ModuleContractError(f"external {name} must own at least one path")
        for entry_path in entry["paths"]:
            if not (root / entry_path).exists():
                raise ModuleContractError(f"external {name} owns {entry_path}, which does not exist")
            owner = claimed.setdefault(entry_path, name)
            if owner != name:
                raise ModuleContractError(f"{entry_path} is owned by both {owner} and {name}")
        for include_root in entry["includeRoots"]:
            if not (root / include_root).is_dir():
                raise ModuleContractError(
                    f"external {name} include root {include_root} must name an existing directory"
                )
            if not any(under(include_root, entry_path) for entry_path in entry["paths"]):
                raise ModuleContractError(
                    f"external {name} include root {include_root} lies outside the component"
                )
        for target in entry["targets"]:
            if target in unit_targets:
                raise ModuleContractError(f"external {name} target {target} is also a unit target")
        consumers = entry.setdefault("consumers", {})
        if not isinstance(consumers, dict):
            raise ModuleContractError(f"external {name} consumers must hold an object")
        for consumer, allowed in consumers.items():
            if consumer not in units:
                raise ModuleContractError(f"external {name} lists unknown consumer {consumer}")
            if not isinstance(allowed, list) or not allowed or not all(
                isinstance(item, str) for item in allowed
            ):
                raise ModuleContractError(
                    f"external {name} consumer {consumer} must list the headers it may include"
                )

    for name, unit in units.items():
        for entry_path in unit["paths"]:
            owner = external_of(Path(entry_path), contract)
            if owner is not None:
                raise ModuleContractError(
                    f"unit {name} owns {entry_path}, which lies inside external component {owner}"
                )


def load_contract(path: Path, root: Path | None = None) -> dict:
    """Load and validate the contract: schema, units, external components, targets, existing paths."""
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
    claimed: dict[str, str] = {}
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
        for header in unit["forbidHeaders"] + unit["privateHeaders"]:
            relative = Path(header)
            if (relative.is_absolute() or ".." in relative.parts or relative.as_posix() != header
                    or relative.suffix != ".h" or not (root / relative).is_file()):
                raise ModuleContractError(
                    f"unit {name} lists {header}, which must name an existing repository header "
                    "using a canonical relative path"
                )
        for entry in unit["paths"]:
            if not (root / entry).exists():
                raise ModuleContractError(f"unit {name} owns {entry}, which does not exist")
            owner = claimed.setdefault(entry, name)
            if owner != name:
                raise ModuleContractError(f"{entry} is owned by both {owner} and {name}")

    for name, unit in units.items():
        if len(unit["privateHeaders"]) != len(set(unit["privateHeaders"])):
            raise ModuleContractError(f"unit {name} lists duplicate private headers")
        for header in unit["privateHeaders"]:
            if not canonical_private_header(root, Path(header)):
                raise ModuleContractError(
                    f"unit {name} lists private header {header} through a case or symlink alias; "
                    "use its canonical repository path"
                )
            if owner_of(Path(header), contract) != name:
                raise ModuleContractError(f"unit {name} does not own private header {header}")

    load_externals(contract, root, claimed)

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
    prefixes = contract.setdefault("thirdPartyPrefixes", {})
    if not isinstance(prefixes, dict) or not all(
        isinstance(key, str) and isinstance(value, str) for key, value in prefixes.items()
    ):
        raise ModuleContractError(f"{path} thirdPartyPrefixes must map a prefix to a package name")
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
        entry.pop("used", None)
    return entries


def run_target_dump(root: Path) -> str:
    try:
        completed = subprocess.run(
            ["xmake", "lua", "-P", str(root), TARGET_DUMP_SCRIPT],
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
    text: str | None = None
    if path is not None:
        if not path.is_file():
            raise ModuleContractError(f"{path} is missing")
        try:
            text = path.read_text(encoding="utf-8")
        except (OSError, ValueError) as exc:
            raise ModuleContractError(f"cannot read {path}: {exc}") from exc
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
            "targetfile": str(root / target["targetfile"]) if target.get("targetfile") else "",
        }
    return targets


def load_compile_commands(path: Path, root: Path | None = None) -> dict[str, dict]:
    """Map each compiled source to its entry; the compilation database supplies include context only."""
    root = (root or path.parent).resolve()
    entries = read_json(path)
    if not isinstance(entries, list):
        raise ModuleContractError(f"{path} must hold a list of compilation entries")
    database: dict[str, dict] = {}
    for entry in entries:
        if not isinstance(entry, dict) or "file" not in entry:
            raise ModuleContractError(f"{path} contains a malformed entry")
        arguments = entry.get("arguments")
        if arguments is None:
            command = entry.get("command", "")
            if not isinstance(command, str):
                raise ModuleContractError(f"{path} contains an entry whose command is not a string")
            try:
                arguments = shlex.split(command)
            except ValueError as exc:
                raise ModuleContractError(f"{path} contains an invalid command: {exc}") from exc
        if not isinstance(arguments, list) or not arguments:
            raise ModuleContractError(f"{path} contains an entry whose arguments are not a list")
        directory = Path(entry.get("directory") or root)
        directory = (root / directory).resolve()
        source = (directory / entry["file"]).resolve()
        key = os.path.relpath(source, root)
        database.setdefault(
            key,
            {"directory": str(directory), "file": str(source), "arguments": [str(item) for item in arguments]},
        )
    return database


def include_dirs(entry: dict) -> list[Path]:
    """The entry's include directories in command order, resolved against its working directory."""
    base = Path(entry.get("directory") or ".")
    arguments = entry.get("arguments", [])
    dirs: list[Path] = []
    index = 0
    while index < len(arguments):
        token = arguments[index]
        value: str | None = None
        if token in INCLUDE_FLAGS:
            value = arguments[index + 1] if index + 1 < len(arguments) else None
            index += 2
        else:
            for flag in INCLUDE_FLAGS:
                if token.startswith(flag) and len(token) > len(flag):
                    value = token[len(flag) :]
                    break
            index += 1
        if not value:
            continue
        directory = Path(value)
        directory = directory if directory.is_absolute() else base / directory
        if directory not in dirs:
            dirs.append(directory)
    return dirs


def parse_includes(text: str) -> list[tuple[str, bool]]:
    """Every include directive in source order as (spec, quoted); a quoted spec carries True."""
    return [(quoted or angled, bool(quoted)) for quoted, angled in INCLUDE_PATTERN.findall(text)]


def third_party_name(path: Path, root: Path) -> str | None:
    """Name the package a resolved header belongs to, or None when it is neither vendored nor packaged."""
    parts = path.parts
    for index, part in enumerate(parts):
        if part == PACKAGE_DIR and index + 3 < len(parts) and len(parts[index + 1]) == 1:
            return parts[index + 2]
    try:
        relative = path.relative_to(root).parts
    except ValueError:
        return None
    if len(relative) > 1 and relative[0] == THIRD_PARTY_DIR:
        return relative[1]
    return None


def third_party_prefix(spec: str, contract: dict) -> str | None:
    """Name the package a contract-declared spec prefix stands for, for a header no root resolves."""
    for prefix, name in contract.get("thirdPartyPrefixes", {}).items():
        if spec.startswith(prefix):
            return name
    return None


def owner_of(path: Path, contract: dict) -> str | None:
    """Return the unit owning a repository-relative path; the longest matching entry wins."""
    text = path.as_posix()
    best: tuple[int, str] | None = None
    for name, unit in contract["units"].items():
        for entry in unit["paths"]:
            if not under(text, entry):
                continue
            depth = len(entry.split("/"))
            if best is None or depth > best[0]:
                best = (depth, name)
    return best[1] if best else None


def project_files(root: Path, contract: dict) -> list[Path]:
    """Every header and source under the contract's roots, repository-relative and sorted.

    Files inside an external component are excluded wherever a root happens to reach them: the
    component's insides are its own concern, not Luminex's.
    """
    files: set[Path] = set()
    for name in contract["roots"]:
        base = root / name
        if not base.is_dir():
            continue
        for path in base.rglob("*"):
            if not path.is_file() or path.suffix not in SOURCE_SUFFIXES:
                continue
            relative = path.relative_to(root)
            if external_of(relative, contract) is None:
                files.add(relative)
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


def allow(entry: dict) -> bool:
    """Record that an entry suppressed a violation, so an entry that suppresses nothing is visible."""
    entry["used"] = True
    return True


def shared_source_allowed(file: str, names: list[str], allowlist: list[dict]) -> bool:
    return any(
        entry["kind"] == "shared-source"
        and entry["file"] == file
        and sorted(entry["targets"]) == sorted(names)
        and allow(entry)
        for entry in allowlist
    )


def external_header_allowed(entry: dict, allowed: list[str], reached: str) -> bool:
    """Match one component header against a consumer's allowance.

    `"*"` covers every header under the entry's declared include roots and nothing else, so the
    component's private files stay unreachable. Every other item is a header spelling as an
    `#include` writes it, matched against the tail of the repository path it resolved to.
    """
    for name in allowed:
        if name == EXTERNAL_WILDCARD:
            if any(under(reached, include_root) for include_root in entry["includeRoots"]):
                return True
            continue
        if reached == name or reached.endswith(f"/{name}"):
            return True
    return False


def include_allowed(file: str, reaches: str, allowlist: list[dict]) -> bool:
    return any(
        entry["kind"] == "include" and entry["file"] == file and entry["reaches"] == reaches and allow(entry)
        for entry in allowlist
    )


def check_allowlist_use(path: Path, allowlist: list[dict], errors: list[str], link: bool = False) -> None:
    """An entry that suppressed nothing is stale debt, and the convention makes that an error.

    A link-only kind can only be used while `--link` runs its check, so it is exempt here when
    `link` is False: an unused `framework` entry is not stale debt on a run that never looked.
    A foreign kind is never used by this checker at all — another checker owns marking it used —
    so it is exempt unconditionally.
    """
    for entry in allowlist:
        if entry.get("used"):
            continue
        if entry["kind"] in FOREIGN_ALLOWLIST_KINDS:
            continue
        if not link and entry["kind"] in LINK_ONLY_ALLOWLIST_KINDS:
            continue
        subject = entry.get("file") or entry.get("target") or ""
        errors.append(f"{path.as_posix()}: unused entry {entry['kind']} {subject} ({entry['until']})")


def check_ownership(
    files: list[Path],
    targets: dict[str, dict],
    contract: dict,
    allowlist: list[dict],
    errors: list[str],
) -> None:
    """Reconcile the ownership map with target membership: one unit and one target per source."""
    owners = compiling_targets(targets, contract)
    for path in sorted(set(files) | {Path(file) for file in owners}):
        text = path.as_posix()
        if external_of(path, contract) is not None:
            continue  # a foreign component builds its own sources into its own targets
        unit = owner_of(path, contract)
        if unit is None:
            errors.append(f"{text}: no unit owns this file; add it to Tools/module_contract.json")
            continue
        names = owners.get(text, [])
        allowed = contract["units"][unit]["targets"]
        if path.suffix in COMPILED_SUFFIXES and not names:
            errors.append(f"{text}: no target compiles this source")
            continue
        if len(names) > 1:
            if not shared_source_allowed(text, names, allowlist):
                errors.append(f"{text}: compiled by {', '.join(names)}; a source belongs to one target")
            if not set(names).intersection(allowed):
                errors.append(f"{text}: no compiling target belongs to unit {unit}")
            continue
        if names and names[0] not in allowed:
            expected = ", ".join(allowed) or "no target"
            errors.append(f"{text}: unit {unit} builds as {expected}, but target {names[0]} compiles it")


def project_file_identities(root: Path, contract: dict) -> dict[tuple[int, int], Resolved]:
    """Canonicalise project include identities so aliases cannot hide transitive private reach.

    A private inventory path takes precedence over every alias of the same file. Otherwise prefer
    the real repository entry over a symlink, retaining deterministic order for hard-linked peers.
    External package and system resolution is unchanged.
    """
    private = {header for unit in contract["units"].values()
               for header in unit.get("privateHeaders", [])}
    canonical_root = root.resolve()
    files = project_files(root, contract)
    files.sort(key=lambda path: (
        path.as_posix() not in private,
        (canonical_root / path).resolve() != canonical_root / path,
        path.as_posix(),
    ))
    identities: dict[tuple[int, int], Resolved] = {}
    for path in files:
        unit = owner_of(path, contract)
        if unit is None:
            continue
        try:
            status = (root / path).stat()
        except OSError as exc:
            raise ModuleContractError(f"cannot inspect {path.as_posix()}: {exc}") from exc
        identities.setdefault((status.st_dev, status.st_ino), Resolved("project", unit, None, path))
    return identities


def resolve_include(
    includer: Path, spec: str, quoted: bool, dirs: list[Path], root: Path, contract: dict,
    identities: dict[tuple[int, int], Resolved] | None = None,
) -> Resolved:
    """Resolve one directive: next to the includer for a quoted spec, then the include directories."""
    candidates = [root / includer.parent / spec] if quoted else []
    candidates.extend(directory / spec for directory in dirs)
    for candidate in candidates:
        path = Path(os.path.normpath(candidate))
        if not path.is_file():
            continue
        if identities is not None:
            status = path.stat()
            if canonical := identities.get((status.st_dev, status.st_ino)):
                return canonical
        try:
            relative = Path(os.path.relpath(path, root))
        except ValueError:
            relative = None
        if relative is not None and not relative.as_posix().startswith(".."):
            component = external_of(relative, contract)
            if component is not None:
                return Resolved("external", None, component, relative)
            unit = owner_of(relative, contract)
            if unit is not None:
                return Resolved("project", unit, None, relative)
        name = third_party_name(path, root)
        if name is not None:
            return Resolved("third-party", None, name, None)
        prefixed = third_party_prefix(spec, contract)
        if prefixed is not None:
            return Resolved("third-party", None, prefixed, None)
        return Resolved("system", None, None, None)
    if not quoted:
        prefixed = third_party_prefix(spec, contract)
        if prefixed is not None:
            return Resolved("third-party", None, prefixed, None)
    return Resolved("system", None, None, None)


def context_for(
    path: Path, unit: str, targets: dict[str, dict], compile_db: dict[str, dict], contract: dict
) -> list[Path]:
    """Include directories for a file: its own command, else the first compiled source of its unit."""
    text = path.as_posix()
    if text in compile_db:
        return include_dirs(compile_db[text])
    for name in sorted(compile_db):
        if owner_of(Path(name), contract) == unit:
            return include_dirs(compile_db[name])
    for name in contract["units"][unit]["targets"]:
        for file in targets.get(name, {}).get("files", []):
            if file in compile_db:
                return include_dirs(compile_db[file])
    raise ModuleContractError(f"no compilation context for {text}; regenerate compile_commands.json")


def check_includes(
    files: list[Path],
    targets: dict[str, dict],
    compile_db: dict[str, dict],
    contract: dict,
    allowlist: list[dict],
    errors: list[str],
    root: Path,
) -> None:
    """Charge every direct package include, every transitively reached unit and every external
    component header against the unit table and the external consumer entries."""
    contexts: dict[str, list[Path]] = {}
    resolved: dict[str, list[Resolved]] = {}
    identities = project_file_identities(root, contract)

    def includes_of(path: Path) -> list[Resolved]:
        text = path.as_posix()
        if text in resolved:
            return resolved[text]
        unit = owner_of(path, contract)
        if unit is None:
            return []
        if text in compile_db:
            dirs = include_dirs(compile_db[text])
        elif unit in contexts:
            dirs = contexts[unit]
        else:
            dirs = contexts.setdefault(unit, context_for(path, unit, targets, compile_db, contract))
        try:
            body = (root / path).read_text(encoding="utf-8", errors="replace")
        except OSError as exc:
            raise ModuleContractError(f"cannot read {path.as_posix()}: {exc}") from exc
        resolved[text] = [
            resolve_include(path, spec, quoted, dirs, root, contract, identities)
            for spec, quoted in parse_includes(body)
        ]
        return resolved[text]

    for path in files:
        unit = owner_of(path, contract)
        if unit is None:
            continue
        text = path.as_posix()
        row = contract["units"][unit]
        status = (root / path).stat()
        canonical = identities.get((status.st_dev, status.st_ino))
        if canonical is not None and canonical.unit != unit and canonical.path is not None:
            private = contract["units"][canonical.unit].get("privateHeaders", [])
            if canonical.path.as_posix() in private:
                errors.append(
                    f"{text}: {unit} aliases private header {canonical.path.as_posix()} "
                    f"owned by {canonical.unit}"
                )
        reported_packages: set[str] = set()
        for include in includes_of(path):
            if include.kind != "third-party" or include.name in row["thirdParty"]:
                continue
            name = include.name or ""
            if include_allowed(text, name, allowlist) or name in reported_packages:
                continue
            reported_packages.add(name)
            errors.append(f"{text}: {unit} includes {name} directly")
        reported: set[str] = set()
        components: set[str] = set()
        chains: dict[str, list[str]] = {text: [text]}
        queue = [path]
        while queue:
            current = queue.pop(0)
            for include in includes_of(current):
                if include.kind == "external" and include.path is not None:
                    reached = include.path.as_posix()
                    if reached in chains:
                        continue
                    # The component is foreign: charge the edge, but never walk into it, so a
                    # consumer does not inherit reach from what a component header includes.
                    chains[reached] = chains[current.as_posix()] + [reached]
                    name = include.name or ""
                    entry = contract["externals"][name]
                    allowed = entry["consumers"].get(unit)
                    if allowed is None:
                        if name in components or include_allowed(text, name, allowlist):
                            components.add(name)
                            continue
                        components.add(name)
                        errors.append(
                            f"{text}: {unit} is not a consumer of {name}, reaching {reached} via "
                            f"{' -> '.join(chains[reached])}"
                        )
                    elif not external_header_allowed(entry, allowed, reached):
                        if include_allowed(text, name, allowlist):
                            continue
                        errors.append(
                            f"{text}: {unit} may not include {name} header {reached} via "
                            f"{' -> '.join(chains[reached])}"
                        )
                    continue
                if include.kind != "project" or include.path is None:
                    continue
                reached = include.path.as_posix()
                if reached in chains:
                    continue
                chains[reached] = chains[current.as_posix()] + [reached]
                queue.append(include.path)
                if reached in row.get("forbidHeaders", []):
                    errors.append(
                        f"{text}: {unit} reaches forbidden header {reached} via "
                        f"{' -> '.join(chains[reached])}"
                    )
                owner_row = contract["units"].get(include.unit, {})
                if include.unit != unit and reached in owner_row.get("privateHeaders", []):
                    errors.append(
                        f"{text}: {unit} reaches private header {reached} owned by {include.unit} via "
                        f"{' -> '.join(chains[reached])}"
                    )
                if include.unit == unit or include.unit in row["units"] or include.unit in reported:
                    continue
                if any(reached == name or reached.endswith(f"/{name}") for name in row["headers"]):
                    continue
                if include_allowed(text, include.unit or "", allowlist):
                    reported.add(include.unit or "")
                    continue
                reported.add(include.unit or "")
                errors.append(f"{text}: {unit} reaches {include.unit} via {' -> '.join(chains[reached])}")


def dependency_closure(name: str, targets: dict[str, dict]) -> set[str]:
    """Every target reachable from `name` through the dump's direct deps, `name` itself excluded."""
    seen: set[str] = set()
    queue = list(targets.get(name, {}).get("deps", []))
    while queue:
        dep = queue.pop(0)
        if dep in seen:
            continue
        seen.add(dep)
        queue.extend(targets.get(dep, {}).get("deps", []))
    return seen


def target_dep_allowed(target: str, dep: str, allowlist: list[dict]) -> bool:
    return any(
        entry["kind"] == "target-dep" and entry["target"] == target and entry["dep"] == dep and allow(entry)
        for entry in allowlist
    )


def framework_allowed(target: str, framework: str, allowlist: list[dict]) -> bool:
    return any(
        entry["kind"] == "framework" and entry["target"] == target and entry["framework"] == framework and allow(entry)
        for entry in allowlist
    )


def check_target_closure(
    targets: dict[str, dict], contract: dict, allowlist: list[dict], errors: list[str]
) -> None:
    """A target's transitive dependency closure minus its allowed deps; unknown allowed names are tolerated."""
    for name in sorted(set(targets) - set(contract["targets"]) - set(contract.get("thirdPartyTargets", []))):
        errors.append(f"{name}: no target contract; declare its allowed dependencies")
    for name, entry in contract["targets"].items():
        if name not in targets:
            continue
        allowed = set(entry.get("deps", []))
        for dep in sorted(dependency_closure(name, targets)):
            if dep in allowed or dep not in targets:
                continue
            if target_dep_allowed(name, dep, allowlist):
                continue
            errors.append(f"{name}: depends on {dep} outside its allowed set")


def linked_frameworks(targetfile: Path, run=subprocess.run) -> list[str]:
    """The frameworks a linked binary or archive names, in `otool -L` order, by their basename."""
    completed = run(["otool", "-L", str(targetfile)], capture_output=True, text=True, check=False)
    if completed.returncode != 0:
        raise ModuleContractError(f"otool -L {targetfile} failed: {completed.stderr.strip()}")
    names: list[str] = []
    for line in completed.stdout.splitlines()[1:]:  # the first line names the binary itself
        match = LINKED_FRAMEWORK_PATTERN.search(line)
        if match and match.group(1) not in names:
            names.append(match.group(1))
    return names


def undefined_symbols(archive: Path, run=subprocess.run) -> list[str]:
    """Every undefined symbol `nm -u` reports, demangled through `c++filt`, in the same order."""
    completed = run(["nm", "-u", str(archive)], capture_output=True, text=True, check=False)
    if completed.returncode != 0:
        raise ModuleContractError(f"nm -u {archive} failed: {completed.stderr.strip()}")
    mangled: list[str] = []
    for line in completed.stdout.splitlines():
        stripped = line.strip()
        if not stripped or stripped.endswith(":"):
            continue
        mangled.append(stripped.split()[-1])
    if not mangled:
        return []
    demangled = run(["c++filt"], input="\n".join(mangled), capture_output=True, text=True, check=False)
    if demangled.returncode != 0:
        raise ModuleContractError(f"c++filt failed: {demangled.stderr.strip()}")
    return demangled.stdout.splitlines()


def check_link(
    targets: dict[str, dict], contract: dict, allowlist: list[dict], errors: list[str], run=subprocess.run
) -> None:
    """Check each target's linked frameworks and undefined symbols against its contract entry."""
    for name, entry in contract["targets"].items():
        if "frameworks" not in entry and not entry.get("forbidUndefined"):
            continue
        if name not in targets:
            continue
        targetfile = targets[name].get("targetfile") or ""
        path = Path(targetfile) if targetfile else None
        if path is None or not path.is_file():
            raise ModuleContractError(f"{name} has no built target file; run xmake build {name}")

        if "frameworks" in entry:
            allowed_frameworks = set(entry["frameworks"])
            for framework in linked_frameworks(path, run=run):
                if framework in allowed_frameworks:
                    continue
                if framework_allowed(name, framework, allowlist):
                    continue
                errors.append(f"{name}: links {framework} outside its allowed set")

        prefix = entry.get("forbidUndefined")
        if prefix:
            for symbol in undefined_symbols(path, run=run):
                if symbol.startswith(prefix):
                    errors.append(f"{name}: undefined symbol {symbol} references {prefix}")


def report_budgets(files: list[Path], contract: dict, root: Path) -> list[str]:
    """Line-count review candidates: informational, never errors. A Tests directory uses the tests
    budget, whether it is the repository suite or a component's own suite such as RHI/Tests."""
    budgets = contract.get("budgets", {})
    lines: list[str] = []
    for path in files:
        category = "tests" if "Tests" in path.parts[:-1] else "production"
        budget = budgets.get(category)
        if budget is None:
            continue
        try:
            with (root / path).open("r", encoding="utf-8", errors="replace") as handle:
                count = sum(1 for _ in handle)
        except OSError as exc:
            raise ModuleContractError(f"cannot read {path.as_posix()}: {exc}") from exc
        if count > budget:
            lines.append(f"review candidate: {path.as_posix()} ({count} > {budget} lines)")
    return lines


def display_path(path: Path, root: Path) -> Path:
    """Report a path inside the repository relative to it, so messages do not carry a home directory."""
    try:
        return path.resolve().relative_to(root)
    except ValueError:
        return path


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
    parser.add_argument(
        "--link", action="store_true", help="also check linked frameworks and undefined symbols"
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    root = args.root.resolve()
    contract_path = args.contract or root / "Tools" / "module_contract.json"
    allowlist_path = args.allowlist or root / "Tools" / "module_allowlist.json"
    compile_commands_path = args.compile_commands or root / "compile_commands.json"

    errors: list[str] = []
    try:
        contract = load_contract(contract_path, root)
        allowlist = load_allowlist(allowlist_path)
        targets = load_targets(args.targets, root)
        if not compile_commands_path.is_file():
            raise ModuleContractError(
                f"{compile_commands_path} is missing; run xmake project -k compile_commands"
            )
        compile_db = load_compile_commands(compile_commands_path, root)
        files = project_files(root, contract)
        check_ownership(files, targets, contract, allowlist, errors)
        check_includes(files, targets, compile_db, contract, allowlist, errors, root)
        check_target_closure(targets, contract, allowlist, errors)
        if args.link:
            check_link(targets, contract, allowlist, errors)
        check_allowlist_use(display_path(allowlist_path, root), allowlist, errors, link=args.link)
        budget_lines = report_budgets(files, contract, root)
    except (ModuleContractError, OSError) as exc:
        print(f"module policy could not run: {exc}", file=sys.stderr)
        return 2

    for line in budget_lines:
        print(line)

    if errors:
        for error in errors:
            print(f"error: {error}", file=sys.stderr)
        print(f"module policy failed with {len(errors)} error(s)", file=sys.stderr)
        return 1
    components = len(contract["externals"])
    print(
        f"module policy passed ({len(files)} files, {len(contract['units'])} units, "
        f"{components} external component{'' if components == 1 else 's'})"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
