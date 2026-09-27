#!/usr/bin/env python3
"""Compare candidate captures with every parent hash from alternating parity.py rounds."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import unittest

import parity


def _valid_rows(rows: list[dict], names: set[str]) -> bool:
    return (len(rows) == len(names) and {row.get("name") for row in rows} == names and
            all(row.get("returnCode") == 0 and
                isinstance(row.get("actualSha256"), str) and
                re.fullmatch(r"[0-9a-f]{64}", row["actualSha256"]) for row in rows))


def compare_rounds(parent: list[list[dict]], candidate: list[list[dict]]) -> dict:
    """Require a complete capture per round and a parent-observed hash for each candidate."""
    names = {row.get("name") for rows in parent + candidate for row in rows}
    complete = bool(parent and candidate and len(parent) == len(candidate) and names and
                    all(_valid_rows(rows, names) for rows in parent + candidate))
    cases = {}
    for name in sorted(names):
        parent_hashes = sorted({row["actualSha256"] for rows in parent for row in rows
                                if row.get("name") == name and "actualSha256" in row})
        candidate_hashes = sorted({row["actualSha256"] for rows in candidate for row in rows
                                   if row.get("name") == name and "actualSha256" in row})
        unseen = sorted(set(candidate_hashes) - set(parent_hashes))
        cases[name] = {"parentHashes": parent_hashes,
                       "candidateHashes": candidate_hashes,
                       "unseenCandidateHashes": unseen,
                       "match": complete and not unseen}
    return {"complete": complete, "allMatched": complete and all(row["match"] for row in cases.values()),
            "cases": cases}


def run(parent_app: Path, candidate_app: Path, rounds: int, output: Path,
        reference: Path, parent_documents: Path | None = None,
        candidate_documents: Path | None = None) -> bool:
    reference = reference.resolve()
    frozen = parity.load_reference(reference)
    if rounds < 1:
        raise ValueError("rounds must be positive")
    parent_app, candidate_app = parent_app.resolve(), candidate_app.resolve()
    if not parent_app.is_file() or not candidate_app.is_file():
        raise ValueError("both App binaries must exist")
    if output.exists() and any(output.iterdir()):
        raise ValueError("output directory must be new or empty")
    output.mkdir(parents=True, exist_ok=True)
    app_hashes = {"parent": parity.sha256(parent_app), "candidate": parity.sha256(candidate_app)}
    shader_hashes = {"parent": parity.shader_hashes(parent_app),
                     "candidate": parity.shader_hashes(candidate_app)}
    reference_hash = parity.sha256(reference)
    reference_hashes = {row["name"]: row["sha256"] for row in frozen["images"]}
    reference_names = set(reference_hashes)
    captures: dict[str, list[list[dict]]] = {"parent": [], "candidate": []}
    attempts = []
    summary_path = output / "summary.json"
    for round_index in range(rounds):
        # AB, BA, AB, BA keeps order effects from always favoring one binary.
        order = ("parent", "candidate") if round_index % 2 == 0 else ("candidate", "parent")
        for side in order:
            app = parent_app if side == "parent" else candidate_app
            documents = parent_documents if side == "parent" else candidate_documents
            directory = output / f"round-{round_index + 1:02d}-{side}"
            command = [sys.executable, str(Path(parity.__file__).resolve()),
                       "--app", str(app), "--output", str(directory),
                       "--reference", str(reference)]
            if documents is not None:
                command += ["--documents", str(documents)]
            try:
                result = subprocess.run(command, capture_output=True, text=True)
            except OSError as error:
                result = subprocess.CompletedProcess(command, -1, "", str(error))
            log_path = output / f"round-{round_index + 1:02d}-{side}.log"
            log_path.write_text(result.stdout + result.stderr)
            attempt = {"round": round_index + 1, "side": side, "command": command,
                       "returnCode": result.returncode, "log": log_path.name,
                       "report": str(directory / "parity.json"), "complete": False}
            try:
                report = json.loads((directory / "parity.json").read_text())
                rows = report["images"]
                attempt["referenceMatches"] = sum(row.get("match") is True for row in rows)
                attempt["referenceAllMatched"] = report.get("allMatched") is True
                valid_rows = _valid_rows(rows, reference_names)
                historical_rows = (valid_rows and all(
                    row.get("expectedSha256") == reference_hashes[row["name"]] and
                    type(row.get("match")) is bool and
                    row["match"] == (row["actualSha256"] == row["expectedSha256"])
                    for row in rows))
                historical_match = historical_rows and all(row["match"] for row in rows)
                attempt["complete"] = (report.get("complete") is True and
                                       report.get("referenceSha256") == reference_hash and
                                       report.get("appSha256") == app_hashes[side] and
                                       report.get("shaderSha256") == shader_hashes[side] and
                                       historical_rows and
                                       report.get("allMatched") is historical_match and
                                       result.returncode == (0 if historical_match else 1))
                if attempt["complete"]:
                    captures[side].append(rows)
                else:
                    attempt["failure"] = (f"unexpected parity.py exit {result.returncode}"
                                          if result.returncode not in (0, 1) else
                                          "incomplete or inconsistent parity report")
            except (OSError, ValueError, TypeError, KeyError, AttributeError) as error:
                attempt["failure"] = str(error)
            if parity.sha256(app) != app_hashes[side]:
                attempt["complete"] = False
                attempt["failure"] = "App changed during capture"
            if parity.shader_hashes(app) != shader_hashes[side]:
                attempt["complete"] = False
                attempt["failure"] = "runtime shaders changed during capture"
            if parity.sha256(reference) != reference_hash:
                attempt["complete"] = False
                attempt["failure"] = "reference changed during capture"
            attempts.append(attempt)
            comparison = compare_rounds(captures["parent"], captures["candidate"])
            summary = {"schemaVersion": 1, "reference": str(reference),
                       "referenceSha256": reference_hash, "rounds": rounds,
                       "appSha256": app_hashes, "shaderSha256": shader_hashes,
                       "attempts": attempts, **comparison}
            summary["complete"] = (len(attempts) == 2 * rounds and
                                   all(item["complete"] for item in attempts) and
                                   comparison["complete"])
            summary["allMatched"] = summary["complete"] and comparison["allMatched"]
            summary_path.write_text(json.dumps(summary, indent=2) + "\n")
            print(f"round {round_index + 1}/{rounds} {side}: " +
                  ("captured" if attempt["complete"] else attempt["failure"]), flush=True)
    return summary["allMatched"]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--parent-app", type=Path)
    parser.add_argument("--candidate-app", type=Path)
    parser.add_argument("--rounds", type=int, default=8)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--reference", type=Path,
                        default=Path(__file__).with_name("reference.json"))
    parser.add_argument("--parent-documents", type=Path)
    parser.add_argument("--candidate-documents", type=Path)
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    if args.selftest:
        suite = unittest.defaultTestLoader.discover(str(Path(__file__).resolve().parents[1] / "tests"),
                                                    pattern="test_parity_rounds.py")
        return 0 if unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful() else 1
    if args.parent_app is None or args.candidate_app is None or args.output is None:
        parser.error("--parent-app, --candidate-app and --output are required")
    try:
        return 0 if run(args.parent_app, args.candidate_app, args.rounds, args.output,
                        args.reference, args.parent_documents, args.candidate_documents) else 1
    except (OSError, ValueError, KeyError) as error:
        print(f"parity rounds refused: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
