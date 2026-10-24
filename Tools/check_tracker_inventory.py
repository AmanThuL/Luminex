#!/usr/bin/env python3
"""Require the Renderer validation cases in a Catch2 tracker test listing."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

REQUIRED_CASES = (
    "the minimal frame runs four frames under the validation tracker",
    "the display read can be culled and restored under the tracker",
)


class TrackerInventoryError(ValueError):
    """The listing cannot establish that both required tracker cases are present."""


def check_inventory(inventory: object) -> int:
    """Validate Catch2 JSON listing versions 1 and 2 and return the number of cases."""
    if not isinstance(inventory, dict):
        raise TrackerInventoryError("test inventory must be an object")
    version = inventory.get("version")
    if type(version) is not int or version not in (1, 2):
        raise TrackerInventoryError(f"unsupported Catch2 inventory version: {version!r}")
    listings = inventory.get("listings")
    if not isinstance(listings, dict):
        raise TrackerInventoryError("test inventory must contain a listings object")
    tests = listings.get("tests")
    if not isinstance(tests, list) or not tests:
        raise TrackerInventoryError("listings.tests must be a nonempty array")
    names: list[str] = []
    for index, case in enumerate(tests):
        if not isinstance(case, dict) or not isinstance(case.get("name"), str) or not case["name"].strip():
            raise TrackerInventoryError(f"listings.tests[{index}] must contain a nonempty name")
        names.append(case["name"])
    for required in REQUIRED_CASES:
        if names.count(required) != 1:
            raise TrackerInventoryError(f"required tracker case missing or duplicated: {required}")
    if len(set(names)) != len(names):
        raise TrackerInventoryError("test inventory contains duplicate case names")
    return len(names)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inventory", type=Path, help="Catch2 --list-tests --reporter json output")
    args = parser.parse_args(argv)
    try:
        inventory = json.loads(args.inventory.read_text(encoding="utf-8"))
        count = check_inventory(inventory)
    except (OSError, UnicodeError, json.JSONDecodeError, TrackerInventoryError) as exc:
        print(f"tracker inventory check failed: {exc}", file=sys.stderr)
        return 1
    print(f"tracker inventory check passed ({count} cases, both required Renderer cases)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
