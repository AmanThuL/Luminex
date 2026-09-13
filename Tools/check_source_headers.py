#!/usr/bin/env python3
"""Compile every Source header as the sole include of an empty translation unit.

The convention in docs/conventions/modules.md requires every project header to compile
standalone. RHI's public surface already gets the stricter dependency-free check in
check_rhi_headers.py; this checker covers the remaining headers under Source/, each
compiled with the real command line of a source file from its owning unit's target so
the header sees the include directories and defines that target actually uses.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from Tools import check_module_deps
SOURCE_DIR = "Source"
DROP_FLAGS = ("-c",)
DROP_FLAGS_WITH_VALUE = ("-o",)


def source_headers(root: Path) -> list[Path]:
    """Every header under Source/, repository-relative and sorted."""
    base = root / SOURCE_DIR
    return sorted(
        (path.relative_to(root) for path in base.rglob("*.h") if path.is_file()),
        key=lambda path: path.as_posix(),
    )


def syntax_command(entry: dict) -> list[str]:
    """The entry's command with -c, -o <obj> and the source dropped, syntax-only added.

    -I, -isystem, -std, -isysroot, -target and every other flag the entry carries are
    kept unchanged, so the header sees the exact include directories and defines its
    owning target compiles with.
    """
    arguments = entry["arguments"]
    source = entry.get("file")
    command: list[str] = []
    skip_next = False
    for token in arguments:
        if skip_next:
            skip_next = False
            continue
        if token in DROP_FLAGS:
            continue
        if token in DROP_FLAGS_WITH_VALUE:
            skip_next = True
            continue
        if source is not None and token == source:
            continue
        command.append(token)
    command.extend(["-fsyntax-only", "-x", "c++", "-"])
    return command


def command_for(header: Path, contract: dict, targets: dict, compile_db: dict) -> list[str]:
    """The command line to check `header` with: a compiled source of its owning target."""
    unit = check_module_deps.owner_of(header, contract)
    if unit is None:
        raise check_module_deps.ModuleContractError(f"no unit owns {header.as_posix()}")
    target_names = contract["units"][unit]["targets"]
    files: set[str] = set()
    for name in target_names:
        files.update(targets.get(name, {}).get("files", []))
    source = min((file for file in compile_db if file in files), default=None)
    if source is None:
        expected = ", ".join(target_names) or "no target"
        raise check_module_deps.ModuleContractError(
            f"no compiled source found for {expected} to check {header.as_posix()}"
        )
    entry = dict(compile_db[source])
    entry["file"] = source
    return syntax_command(entry)


def check_header(header: Path, command: list[str], include_root: Path, run=subprocess.run) -> str | None:
    """Compile `header` as the sole include of an empty translation unit; None on success."""
    include = header.relative_to(SOURCE_DIR).as_posix()
    result = run(
        command,
        cwd=include_root,
        input=f'#include "{include}"\n',
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if result.returncode:
        return result.stderr.strip()
    return None


def header_allowed(header: Path, allowlist: list[dict]) -> bool:
    text = header.as_posix()
    for entry in allowlist:
        if entry["kind"] == "header" and entry["file"] == text:
            entry["used"] = True
            return True
    return False


def check_header_allowlist_use(path: Path, allowlist: list[dict], errors: list[str]) -> None:
    """A `header` allowlist entry that suppressed nothing is stale debt.

    Mirrors check_module_deps.check_allowlist_use for the `header` kind only: that
    function would also flag every other kind's entries as unused, since this checker
    never marks them used.
    """
    for entry in allowlist:
        if entry["kind"] != "header" or entry.get("used"):
            continue
        errors.append(f"{path.as_posix()}: unused entry header {entry['file']} ({entry['until']})")


def parse_args(argv: list[str] | None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT, help="repository root to check")
    parser.add_argument("--contract", type=Path, default=None, help="module contract JSON")
    parser.add_argument("--allowlist", type=Path, default=None, help="module allowlist JSON")
    parser.add_argument(
        "--targets", type=Path, default=None, help="target dump JSON; xmake produces it when absent"
    )
    parser.add_argument(
        "--compile-commands", type=Path, default=None, help="compilation database to compile headers with"
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    root = args.root.resolve()
    contract_path = args.contract or root / "Tools" / "module_contract.json"
    allowlist_path = args.allowlist or root / "Tools" / "module_allowlist.json"
    compile_commands_path = args.compile_commands or root / "compile_commands.json"

    errors: list[str] = []
    headers: list[Path] = []
    try:
        contract = check_module_deps.load_contract(contract_path, root)
        allowlist = check_module_deps.load_allowlist(allowlist_path)
        if not compile_commands_path.is_file():
            raise check_module_deps.ModuleContractError(
                f"{compile_commands_path} is missing; run xmake project -k compile_commands first"
            )
        compile_db = check_module_deps.load_compile_commands(compile_commands_path)
        targets = check_module_deps.load_targets(args.targets, root)

        headers = source_headers(root)
        for header in headers:
            if header_allowed(header, allowlist):
                continue
            command = command_for(header, contract, targets, compile_db)
            diagnostic = check_header(header, command, root)
            if diagnostic is not None:
                errors.append(f"{header.as_posix()}: standalone include failed\n{diagnostic}")
        check_header_allowlist_use(check_module_deps.display_path(allowlist_path, root), allowlist, errors)
    except check_module_deps.ModuleContractError as exc:
        print(f"source header check could not run: {exc}", file=sys.stderr)
        return 2

    if errors:
        for error in errors:
            print(f"error: {error}", file=sys.stderr)
        print(f"source header check failed ({len(errors)} error(s))", file=sys.stderr)
        return 1
    print(f"source header check passed ({len(headers)} headers compiled standalone)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
