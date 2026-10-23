"""Validate the exact source subset exported by the configured build."""

from __future__ import annotations

import hashlib
import re
from collections.abc import Callable, Iterable
from pathlib import Path, PurePosixPath


class PendingSourceError(RuntimeError):
    """The build did not provide a trustworthy pending-source partition."""


def _source_path(root: Path, name: str) -> Path:
    path = PurePosixPath(name)
    if (not name or not path.parts or path.is_absolute() or path.as_posix() != name
            or "\\" in name or any(char in name for char in "*?[]")
            or any(part in (".", "..") for part in path.parts)
            or path.parts[0] not in ("Source", "Tests", "Benchmarks")
            or path.suffix != ".cpp"):
        raise PendingSourceError(f"invalid pending cpp path: {name!r}")
    candidate = root.joinpath(*path.parts)
    if not candidate.is_file():
        raise PendingSourceError(f"pending cpp does not exist: {name}")
    if candidate.resolve() != candidate:
        raise PendingSourceError(f"pending cpp is a symlink or escapes the repository: {name}")
    current = root
    for part in path.parts:
        if part not in {child.name for child in current.iterdir()}:
            raise PendingSourceError(f"pending cpp is not a canonical repository path: {name}")
        current /= part
    return candidate


def validate_pending_sources(
    root: Path,
    targets: dict[str, dict],
    *,
    owner: Callable[[Path], object],
    compiled: Iterable[str | Path] = (),
) -> frozenset[str]:
    """Return only declared, current and uncompiled cpp paths; never infer exclusions.

    The caller supplies its existing ownership rule and any compilation-database entries.
    Target memberships are checked independently, including targets outside that owner's unit.
    """
    root = root.resolve()
    producers = [name for name, target in targets.items() if "pending" in target]
    if any(name != "Render" for name in producers):
        raise PendingSourceError("only Render may export pending source metadata")
    metadata = targets.get("Render", {}).get("pending")
    port = root / "xmake" / "port.lua"
    if ((port.exists() or port.is_symlink())
            and (not port.is_file() or port.resolve() != port)):
        raise PendingSourceError("xmake/port.lua must be a regular repository file")
    if not port.is_file():
        if producers and metadata != {}:
            raise PendingSourceError("pending source metadata remains without xmake/port.lua")
        return frozenset()
    if producers != ["Render"] or not isinstance(metadata, dict):
        raise PendingSourceError("xmake/port.lua requires Render.pending metadata")
    if set(metadata) != {"version", "portSha256", "cpp"}:
        raise PendingSourceError("Render.pending must contain version, portSha256 and cpp only")
    if type(metadata["version"]) is not int or metadata["version"] != 1:
        raise PendingSourceError("unsupported pending source metadata version")
    fingerprint = metadata["portSha256"]
    if not isinstance(fingerprint, str) or re.fullmatch(r"[0-9a-f]{64}", fingerprint) is None:
        raise PendingSourceError("pending source metadata has an invalid portSha256")
    if fingerprint != hashlib.sha256(port.read_bytes()).hexdigest():
        raise PendingSourceError("pending source metadata is stale; regenerate the target dump")
    names = metadata["cpp"]
    if not isinstance(names, list) or any(not isinstance(name, str) for name in names):
        raise PendingSourceError("pending cpp must be a list of repository-relative paths")
    if names != sorted(set(names)):
        raise PendingSourceError("pending cpp paths must be sorted and unique")
    paths: dict[str, Path] = {}
    for name in names:
        path = _source_path(root, name)
        if owner(Path(name)) is None:
            raise PendingSourceError(f"no unit owns pending cpp: {name}")
        paths[name] = path

    compiled_paths = {
        (root / Path(name)).resolve()
        for name in [*compiled, *(file for target in targets.values() for file in target.get("files", []))]
    }
    overlap = [name for name, path in paths.items() if path in compiled_paths]
    if overlap:
        raise PendingSourceError("pending cpp is still compiled: " + ", ".join(overlap))
    return frozenset(paths)
