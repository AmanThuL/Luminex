#!/usr/bin/env python3
"""Check that portability checkpoint A (ADR 0009) is split, undivided, across Tests and RHITests."""

from __future__ import annotations

import argparse
import subprocess
import sys
import xml.etree.ElementTree as ElementTree
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
UNION_INVENTORY = ROOT / "Tests" / "checkpoint-a.inventory"
RHI_INVENTORY = ROOT / "RHI" / "Tests" / "checkpoint-a.inventory"
DEFAULT_TESTS = ROOT / "build" / "macosx" / "arm64" / "release" / "test" / "Tests"
DEFAULT_RHI_TESTS = ROOT / "build" / "macosx" / "arm64" / "release" / "rhi-test" / "RHITests"
TAG = "[checkpoint-a]"


class CheckpointAError(RuntimeError):
    """The checker could not obtain a trustworthy view of the tagged cases."""


def read_inventory(path: Path) -> list[str]:
    try:
        return [line for line in path.read_text(encoding="utf-8").splitlines() if line]
    except OSError as exc:
        raise CheckpointAError(f"cannot read {path}: {exc}") from exc


def parse_case_names(xml: str) -> list[str]:
    """Read Catch2's `--list-tests --reporter xml` output; a text listing wraps long names."""
    try:
        root = ElementTree.fromstring(xml)
    except ElementTree.ParseError as exc:
        raise CheckpointAError(f"cannot parse test listing: {exc}") from exc
    return [name.text or "" for name in root.iter("Name")]


def list_tagged_cases(binary: Path) -> list[str]:
    """Run one test binary from its own build directory and return its `[checkpoint-a]` cases."""
    if not binary.is_file():
        raise CheckpointAError(f"{binary} is missing; build it first")
    try:
        completed = subprocess.run(
            [str(binary.resolve()), "--list-tests", TAG, "--reporter", "xml"],
            cwd=binary.parent,
            capture_output=True,
            text=True,
            check=False,
        )
    except OSError as exc:
        raise CheckpointAError(f"cannot run {binary}: {exc}") from exc
    if completed.returncode != 0:
        raise CheckpointAError(
            f"{binary} exited {completed.returncode} listing {TAG}: {completed.stderr.strip()}"
        )
    return parse_case_names(completed.stdout)


def check_split(
    tests: list[str], rhi_tests: list[str], union_inventory: list[str], rhi_inventory: list[str]
) -> list[str]:
    """Compare the two binaries' tagged case names against the frozen inventory files.

    The binaries' tagged sets must be disjoint, their union must equal the frozen union exactly,
    and RHITests' tagged set must equal the RHI inventory exactly.
    """
    errors: list[str] = []

    shared = sorted(set(tests) & set(rhi_tests))
    for name in shared:
        errors.append(f"tagged {TAG} in both Tests and RHITests: {name!r}")

    combined = set(tests) | set(rhi_tests)
    frozen = set(union_inventory)
    for name in sorted(frozen - combined):
        errors.append(f"in {UNION_INVENTORY.name} but not tagged {TAG} by either binary: {name!r}")
    for name in sorted(combined - frozen):
        errors.append(f"tagged {TAG} but not in {UNION_INVENTORY.name}: {name!r}")

    rhi_frozen = set(rhi_inventory)
    rhi_actual = set(rhi_tests)
    for name in sorted(rhi_frozen - rhi_actual):
        errors.append(f"in {RHI_INVENTORY.name} but not tagged {TAG} by RHITests: {name!r}")
    for name in sorted(rhi_actual - rhi_frozen):
        errors.append(f"tagged {TAG} by RHITests but not in {RHI_INVENTORY.name}: {name!r}")

    return errors


def parse_args(argv: list[str] | None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tests", type=Path, default=DEFAULT_TESTS, help="Tests binary")
    parser.add_argument("--rhi-tests", type=Path, default=DEFAULT_RHI_TESTS, help="RHITests binary")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        tests = list_tagged_cases(args.tests)
        rhi_tests = list_tagged_cases(args.rhi_tests)
        union_inventory = read_inventory(UNION_INVENTORY)
        rhi_inventory = read_inventory(RHI_INVENTORY)
    except CheckpointAError as exc:
        print(f"checkpoint A check could not run: {exc}", file=sys.stderr)
        return 2

    errors = check_split(tests, rhi_tests, union_inventory, rhi_inventory)
    if errors:
        for error in errors:
            print(f"error: {error}", file=sys.stderr)
        print(f"checkpoint A check failed with {len(errors)} error(s)", file=sys.stderr)
        return 1
    print(f"checkpoint A check passed ({len(union_inventory)} cases, {len(rhi_inventory)} RHI)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
