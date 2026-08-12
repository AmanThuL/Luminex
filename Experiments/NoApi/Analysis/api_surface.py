#!/usr/bin/env python3
"""M5.1 API-surface counting (spec section 9's API-surface dimension, plan Stage 4 item 4).

Counts three primary metrics over the interface subset each side actually needs for the scored
workloads (the representative graph and S-BIND), under identical, mechanical, regex/grep-based
rules applied to both sides. Public-header line count is printed as auxiliary evidence only, per
the spec ("never decisive alone").

COUNTING RULES (identical for both sides; the only difference is which files are "the adapter" and
which are "the public header"):

  Incumbent (maintained RHI):
    adapter files = Experiments/NoApi/Bench/RhiAdapter.h, RhiAdapter.cpp
    public header = RHI/Include/RHI/RHI.h, RHI/Include/RHI/Validate.h

  Prototype (address-first):
    adapter files = Experiments/NoApi/Bench/NoApiAdapter.h, NoApiAdapter.cpp
    public header = every Experiments/NoApi/Include/NoApi/*.h

  1. CONCEPTS (public types actually used): a public header declares a type with a line matching
     `class NAME`, `struct NAME`, `enum class NAME`, or `using NAME =` at namespace scope. A type
     counts if its NAME also appears as a whole-word token somewhere in the adapter files. This is
     intentionally syntactic (no C++ parsing): it counts declared-and-used names, not usage
     *kind* (return type vs. parameter vs. local variable all count the same way, which is the
     conservative, over-inclusive reading -- it never undercounts a type the adapter depends on).

  2. OPERATIONS (public entry points actually called): a public header declares an operation with a
     line matching a free-function or method declaration -- `RETURN_TYPE NAME(...)` at namespace
     scope, or `virtual RETURN_TYPE NAME(...)` inside a class/struct body -- ending in `;`. An
     operation counts if `NAME(` (a call, not a declaration) appears in the adapter files at least
     once. Declarations occurring only inside the adapter's own class bodies (RhiAdapter's private
     `encode*`/`bind*Counted` helpers, NoApiAdapter's private `encode*`) are excluded by
     construction: they live in the adapter's own header, never the public one, so they cannot
     satisfy "declared in the public header."

  3. CALLER CEREMONY (calls and arguments per workload operation): for every operation counted in
     (2), every call site `NAME(...)` found in the adapter's .cpp file is one call site; the
     argument count at that site is the number of top-level (paren/bracket/brace-balanced) commas
     inside the call's parentheses, plus one for a non-empty argument list. Reported per operation
     and summed. This underestimates true ceremony wherever a caller factors repeated arguments
     into a local (both sides do this at least once), which is disclosed here rather than
     corrected: correcting it would require semantic analysis this script deliberately does not do.

Both counting passes are pure regex/string scanning (Tools/*.py convention: stdlib only), so the
same script run twice over an unchanged tree reproduces the same numbers exactly.

Usage: python3 api_surface.py [--repo-root PATH]
"""
import argparse
import re
import sys
from pathlib import Path

_TYPE_DECL_RE = re.compile(
    r"^\s*(?:class|struct)\s+(\w+)\b|^\s*enum\s+class\s+(\w+)\b|^\s*using\s+(\w+)\s*=")
_FREE_FUNC_DECL_RE = re.compile(
    r"^\s*(?:\[\[nodiscard\]\]\s*)?(?:inline\s+)?[\w:<>,&*\s]+?\b(\w+)\s*\([^;]*\)\s*(?:const\s*)?;$")
_VIRTUAL_METHOD_DECL_RE = re.compile(
    r"^\s*virtual\s+[\w:<>,&*\s]+?\b(\w+)\s*\([^;]*\)\s*(?:const\s*)?(?:=\s*0\s*)?;$")
_WORD_RE = re.compile(r"\b\w+\b")
_CALL_RE = re.compile(r"\b(\w+)\s*\(")

# Identifiers that match the call-site regex but are C++ control keywords, not operations.
_KEYWORDS = {"if", "for", "while", "switch", "return", "sizeof", "static_cast", "std", "static_assert"}


def read_lines(paths: list[Path]) -> list[str]:
    lines = []
    for path in paths:
        lines.extend(path.read_text().splitlines())
    return lines


def logical_lines(header_lines: list[str]) -> list[str]:
    """Joins a multi-line declaration (return type, name, and default-valued parameters routinely
    span several physical lines in this codebase's formatting) into one logical line ending at the
    first top-level `;` or `{`, so the single-physical-line regexes below see a whole declaration
    rather than a fragment. Still pure text joining, not a C++ parser: a `;` inside a string or
    default-argument initializer would end a logical line early, which does not occur in these
    headers' declarations.
    """
    joined: list[str] = []
    buffer = ""
    for raw in header_lines:
        line = raw.split("//", 1)[0]
        stripped = line.strip()
        if not stripped:
            continue
        buffer = f"{buffer} {stripped}" if buffer else stripped
        if buffer.endswith(("{", ";", "}")):
            joined.append(buffer)
            buffer = ""
    if buffer:
        joined.append(buffer)
    return joined


def declared_types(header_lines: list[str]) -> set[str]:
    names = set()
    for line in logical_lines(header_lines):
        match = _TYPE_DECL_RE.match(line)
        if match:
            name = next(g for g in match.groups() if g)
            names.add(name)
    return names


def declared_operations(header_lines: list[str]) -> set[str]:
    names = set()
    for line in logical_lines(header_lines):
        for pattern in (_FREE_FUNC_DECL_RE, _VIRTUAL_METHOD_DECL_RE):
            match = pattern.match(line)
            if match:
                names.add(match.group(1))
    return names


def used_tokens(adapter_lines: list[str]) -> set[str]:
    tokens = set()
    for line in adapter_lines:
        tokens.update(_WORD_RE.findall(line))
    return tokens


def call_sites(adapter_text: str, operation_names: set[str]) -> dict[str, list[int]]:
    """Returns {operation_name: [argument_count, ...]} for every call site found."""
    results: dict[str, list[int]] = {name: [] for name in operation_names}
    for match in _CALL_RE.finditer(adapter_text):
        name = match.group(1)
        if name not in operation_names or name in _KEYWORDS:
            continue
        open_paren = match.end() - 1
        depth = 0
        i = open_paren
        end = None
        for i in range(open_paren, len(adapter_text)):
            c = adapter_text[i]
            if c == "(":
                depth += 1
            elif c == ")":
                depth -= 1
                if depth == 0:
                    end = i
                    break
        if end is None:
            continue
        args_text = adapter_text[open_paren + 1:end]
        if args_text.strip() == "":
            results[name].append(0)
            continue
        depth = 0
        commas = 0
        for c in args_text:
            if c in "([{":
                depth += 1
            elif c in ")]}":
                depth -= 1
            elif c == "," and depth == 0:
                commas += 1
        results[name].append(commas + 1)
    return results


def measure_side(name: str, adapter_paths: list[Path], header_paths: list[Path]) -> dict:
    header_lines = read_lines(header_paths)
    adapter_lines = read_lines(adapter_paths)
    adapter_text = "\n".join(adapter_lines)

    types = declared_types(header_lines)
    tokens = used_tokens(adapter_lines)
    concepts_used = sorted(t for t in types if t in tokens)

    operations = declared_operations(header_lines)
    sites = call_sites(adapter_text, operations)
    operations_used = sorted(op for op, calls in sites.items() if calls)

    total_calls = sum(len(sites[op]) for op in operations_used)
    total_args = sum(sum(sites[op]) for op in operations_used)
    header_line_count = sum(1 for _ in header_lines if _.strip())

    return {
        "name": name,
        "conceptCount": len(concepts_used),
        "concepts": concepts_used,
        "operationCount": len(operations_used),
        "operations": {op: {"callSites": len(sites[op]), "totalArgs": sum(sites[op])}
                       for op in operations_used},
        "totalCallSites": total_calls,
        "totalArgs": total_args,
        "publicHeaderLines": header_line_count,
    }


def print_table(sides: list[dict]) -> None:
    print(f"{'metric':<24}" + "".join(f"{s['name']:>16}" for s in sides))
    print("-" * (24 + 16 * len(sides)))
    rows = [
        ("concepts (types)", "conceptCount"),
        ("operations (entry pts)", "operationCount"),
        ("call sites", "totalCallSites"),
        ("total call arguments", "totalArgs"),
        ("public header lines*", "publicHeaderLines"),
    ]
    for label, key in rows:
        print(f"{label:<24}" + "".join(f"{s[key]:>16}" for s in sides))
    print("* auxiliary evidence only; never decisive alone (spec section 9).")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[3])
    parser.add_argument("--json", type=Path, help="optional path to also write the full result as "
                                                   "JSON")
    args = parser.parse_args()
    root = args.repo_root
    experiments = root / "Experiments" / "NoApi"

    incumbent = measure_side(
        "incumbent",
        [experiments / "Bench" / "RhiAdapter.h", experiments / "Bench" / "RhiAdapter.cpp"],
        [root / "RHI" / "Include" / "RHI" / "RHI.h", root / "RHI" / "Include" / "RHI" / "Validate.h"])

    prototype_headers = sorted((experiments / "Include" / "NoApi").glob("*.h"))
    prototype = measure_side(
        "prototype",
        [experiments / "Bench" / "NoApiAdapter.h", experiments / "Bench" / "NoApiAdapter.cpp"],
        prototype_headers)

    print_table([incumbent, prototype])

    if args.json:
        import json
        args.json.write_text(json.dumps({"incumbent": incumbent, "prototype": prototype},
                                        indent=2) + "\n")
        print(f"\nWrote full result to {args.json}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
