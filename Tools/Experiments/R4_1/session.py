#!/usr/bin/env python3
"""Run every capture of a strict three-build R4.1 parity session and evaluate the results."""
from __future__ import annotations

import argparse
import contextlib
import hashlib
import io
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
from collections import Counter, defaultdict
from datetime import datetime, timezone
from pathlib import Path
from typing import NamedTuple
from unittest import mock

REFERENCE_PATH = Path(__file__).resolve().parents[2] / "Screenshots" / "reference.json"
DEFAULT_TIMEOUT = 600
DEFAULT_ROUNDS = 8

_ALLOWED_LABELS = ("H", "A", "B")
_EXTENSION_SCENES = ("sponza", "damaged-helmet", "material-lab", "san-miguel", "visibility-lab")
_MASKED_SCENES = ("san-miguel", "visibility-lab")

_VARIANTS = ("opaque-manual", "masked-manual", "opaque-auto", "masked-auto")
_ENTRIES = ("plain", "motion")

_COVERAGE_RE = re.compile(
    r"r4\.1-coverage auto=(?P<auto>[01]) motion=(?P<motion>[01]) "
    r"opaqueRuns=(?P<opaqueRuns>\d+) maskedRuns=(?P<maskedRuns>\d+)"
)


class Case(NamedTuple):
    name: str
    scene: str
    temporal: str
    renderScale: float
    auto: bool


def _load_reference_cases(reference_path: Path = REFERENCE_PATH) -> list[Case]:
    reference = json.loads(reference_path.read_text())
    return [Case(row["name"], row["scene"], row["temporal"], row["renderScale"], False)
            for row in reference["images"]]


def _build_cases(reference_path: Path = REFERENCE_PATH) -> list[Case]:
    cases = _load_reference_cases(reference_path)
    for scene in _EXTENSION_SCENES:
        cases.append(Case(f"{scene}-auto-off", scene, "off", 1.0, True))
        cases.append(Case(f"{scene}-auto-taa-1", scene, "taa", 1.0, True))
    for scene in _MASKED_SCENES:
        cases.append(Case(f"{scene}-manual-off", scene, "off", 1.0, False))
        cases.append(Case(f"{scene}-manual-taa-1", scene, "taa", 1.0, False))
    return cases


CASES: list[Case] = _build_cases()


def round_order(r: int) -> list[str]:
    order = ["H", "A", "B"]
    k = r % 3
    return order[k:] + order[:k]


def command_for(app: Path, case: Case, out: Path) -> list[str]:
    return [str(app), "--scene", case.scene, "--temporal", case.temporal,
            "--render-scale", format(case.renderScale, "g"), "--frames", "32",
            "--screenshot", str(out)]


def env_for(case: Case) -> dict[str, str]:
    # Strip every LMX_*/MTL_* variable the parent process may carry (e.g. a leftover
    # LMX_EXPERIMENT_AUTO_EXPOSURE, LMX_MAX_FRAMES or MTL_CAPTURE_ENABLED from an earlier run)
    # so each capture's pipeline selection is controlled only by what this function sets below.
    env = {key: value for key, value in os.environ.items()
           if not key.startswith("LMX_") and not key.startswith("MTL_")}
    env["MTL_DEBUG_LAYER"] = "1"
    env["LMX_EXPERIMENT_PIPELINE_LOG"] = "1"
    if case.auto:
        env["LMX_EXPERIMENT_AUTO_EXPOSURE"] = "1"
    return env


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def parse_coverage(log: Path) -> dict | None:
    match = None
    with log.open("r", errors="replace") as stream:
        for line in stream:
            found = _COVERAGE_RE.search(line)
            if found:
                match = found
    if match is None:
        return None
    return {"auto": int(match["auto"]), "motion": int(match["motion"]),
            "opaqueRuns": int(match["opaqueRuns"]), "maskedRuns": int(match["maskedRuns"])}


def _parse_builds_arg(value: str) -> dict[str, Path]:
    builds: dict[str, Path] = {}
    for item in value.split(","):
        label, _, path = item.partition("=")
        if not label or not path:
            raise ValueError(f"invalid --builds entry: {item!r}")
        if label not in _ALLOWED_LABELS:
            raise ValueError(f"unknown build label: {label!r} (expected one of {_ALLOWED_LABELS})")
        builds[label] = Path(path)
    return builds


def _select_only_builds(builds: dict[str, Path], only_builds: list[str] | None) -> dict[str, Path]:
    if not only_builds:
        return builds
    unknown = [label for label in only_builds if label not in builds]
    if unknown:
        raise ValueError(f"unknown --only-builds label(s): {unknown}")
    return {label: path for label, path in builds.items() if label in only_builds}


def _session_path(output: Path) -> Path:
    return output / "session.jsonl"


def _build_info(resolved_builds: dict[str, Path]) -> dict[str, dict[str, str]]:
    return {label: {"path": str(path), "sha256": sha256(path)} for label, path in resolved_builds.items()}


def _truncate_torn_line(path: Path, good_lines: list[str]) -> None:
    content = "\n".join(good_lines)
    if content:
        content += "\n"
    path.write_text(content)


def _read_session(output: Path, truncate_torn: bool = True):
    # truncate_torn=True is only for run()'s own restart path: a torn final line there means the
    # previous process died mid-write, so it is safe (and necessary, to make the capture retryable)
    # to drop it from the file. Every other caller (--evaluate, and any other read-only reporting)
    # passes truncate_torn=False: it must never rewrite session.jsonl, only report the session as
    # incomplete when its last line can't be parsed.
    path = _session_path(output)
    if not path.exists():
        return None, []
    text = path.read_text()
    lines = text.split("\n")
    if lines and lines[-1] == "":
        lines.pop()
    if not lines:
        return None, []

    last_index = len(lines) - 1
    header = None
    records: list[dict] = []
    for i, line in enumerate(lines):
        if not line.strip():
            continue
        try:
            row = json.loads(line)
        except json.JSONDecodeError:
            if i == last_index:
                if truncate_torn:
                    _truncate_torn_line(path, lines[:i])
                    print(f"warning: {path} ended with an unparseable line; truncated it and "
                          "treated that capture as missing", file=sys.stderr)
                else:
                    print(f"warning: {path} ends with an unparseable line; reporting the session "
                          "as incomplete without modifying it", file=sys.stderr)
                continue
            raise ValueError(f"malformed session record at {path} line {i + 1}: {line!r}")
        if row.get("kind") == "header":
            header = row
        else:
            records.append(row)
    return header, records


def _append_line(path: Path, row: dict) -> None:
    with path.open("a") as stream:
        stream.write(json.dumps(row) + "\n")


def _capture(app: Path, build: str, case: Case, r: int, output: Path, timeout: int) -> dict:
    log_path = output / "logs" / f"r{r}-{build}-{case.name}.log"
    tmp_path = output / "tmp" / f"r{r}-{build}-{case.name}.bmp"
    command = command_for(app, case, tmp_path)
    started = time.monotonic()
    utc = datetime.now(timezone.utc).isoformat()
    exit_code = None
    with log_path.open("w") as log:
        try:
            result = subprocess.run(command, cwd=app.parent, env=env_for(case),
                                     stdout=log, stderr=subprocess.STDOUT, timeout=timeout)
            exit_code = result.returncode
        except subprocess.TimeoutExpired:
            exit_code = None
    seconds = time.monotonic() - started

    sha = None
    if exit_code == 0 and tmp_path.is_file():
        sha = sha256(tmp_path)
        case_dir = output / "images" / case.name
        case_dir.mkdir(parents=True, exist_ok=True)
        dest = case_dir / f"{sha}.bmp"
        if not dest.exists():
            shutil.move(str(tmp_path), str(dest))
        else:
            tmp_path.unlink()
    elif tmp_path.exists():
        tmp_path.unlink()

    return {"kind": "capture", "round": r, "build": build, "case": case.name,
            "sha256": sha, "exit": exit_code, "seconds": seconds, "utc": utc}


def run(builds: dict[str, Path], output: Path, rounds: int = DEFAULT_ROUNDS,
        cases: list[Case] | None = None, timeout: int = DEFAULT_TIMEOUT) -> list[dict]:
    if cases is not None and set(builds.keys()) == {"H", "A", "B"}:
        raise ValueError(
            "a parity session over H, A and B must use the full case list; --cases is only for "
            "a reduced-build coverage smoke (combine it with --only-builds)")
    cases = CASES if cases is None else cases
    if set(builds.keys()) == {"H", "A", "B"} and rounds != DEFAULT_ROUNDS:
        raise ValueError(
            f"a parity session over H, A and B must run exactly {DEFAULT_ROUNDS} rounds, got {rounds}; "
            "use --only-builds for a reduced-round smoke")

    output = Path(output).resolve()
    (output / "logs").mkdir(parents=True, exist_ok=True)
    (output / "images").mkdir(parents=True, exist_ok=True)
    (output / "tmp").mkdir(parents=True, exist_ok=True)
    session_path = _session_path(output)

    resolved_builds = {label: Path(path).resolve() for label, path in builds.items()}
    wanted_header = {
        "kind": "header",
        "builds": sorted(builds.keys()),
        "cases": [c.name for c in cases],
        "rounds": rounds,
        "buildInfo": _build_info(resolved_builds),
    }
    existing_header, existing_records = _read_session(output)
    if existing_header is not None:
        mismatches = [key for key in ("builds", "cases", "rounds", "buildInfo")
                      if existing_header.get(key) != wanted_header[key]]
        if mismatches:
            raise ValueError(
                f"{session_path} already holds a session with a different {', '.join(mismatches)}; "
                "refusing to mix sessions in one output directory")
    else:
        _append_line(session_path, wanted_header)

    done = {(rec["round"], rec["build"], rec["case"]) for rec in existing_records}
    all_records = list(existing_records)

    for r in range(rounds):
        order = round_order(r)
        for case in cases:
            for build in order:
                if build not in resolved_builds:
                    continue
                if (r, build, case.name) in done:
                    continue
                record = _capture(resolved_builds[build], build, case, r, output, timeout)
                _append_line(session_path, record)
                all_records.append(record)
                done.add((r, build, case.name))
    return all_records


def collect_coverage(output: Path, records: list[dict],
                      cases: list[Case] | None = None) -> dict[str, list[dict | None]]:
    cases = CASES if cases is None else cases
    case_names = {c.name for c in cases}
    output = Path(output)
    coverage: dict[str, list[dict | None]] = defaultdict(list)
    for rec in records:
        if rec.get("kind") != "capture" or rec["build"] != "H" or rec["case"] not in case_names:
            continue
        log_path = output / "logs" / f"r{rec['round']}-H-{rec['case']}.log"
        coverage[rec["case"]].append(parse_coverage(log_path) if log_path.exists() else None)
    return dict(coverage)


def evaluate(records: list[dict], coverage: dict[str, list[dict | None]], reference: dict,
             cases: list[Case] | None = None, header: dict | None = None) -> dict:
    cases = CASES if cases is None else cases
    header = header or {}
    established_names = {row["name"] for row in reference.get("images", [])}
    reference_hashes = {row["name"]: row["sha256"] for row in reference.get("images", [])}

    captures = [rec for rec in records if rec.get("kind") == "capture"]

    hashes_by_case_build: dict[str, dict[str, list]] = defaultdict(lambda: defaultdict(list))
    for rec in captures:
        hashes_by_case_build[rec["case"]][rec["build"]].append(rec["sha256"])

    cell_covered = {(v, e): False for v in _VARIANTS for e in _ENTRIES}
    cases_report: dict[str, dict] = {}
    extension_coverage_ok = True

    for case in cases:
        builds_hashes = hashes_by_case_build.get(case.name, {})
        hash_sets: dict[str, list] = {}
        null_counts: dict[str, int] = {}
        for build in ("H", "A", "B"):
            all_hashes = builds_hashes.get(build, [])
            hash_sets[build] = sorted({h for h in all_hashes if h is not None})
            null_counts[build] = sum(1 for h in all_hashes if h is None)
        h_set = set(hash_sets["H"])

        candidates = {}
        for candidate in ("A", "B"):
            c_all = builds_hashes.get(candidate, [])
            unchanged = bool(c_all) and all(h is not None for h in c_all) and set(c_all) <= h_set
            unseen = sorted(set(hash_sets[candidate]) - h_set)
            candidates[candidate] = {"unchanged": unchanged, "unseenHashes": unseen}

        reference_sha = reference_hashes.get(case.name)
        reference_match = None
        if reference_sha is not None:
            reference_match = {b: reference_sha in set(hash_sets[b]) for b in ("H", "A", "B")}

        expected_auto = 1 if case.auto else 0
        expected_motion = 1 if case.temporal in ("taa", "metalfx") else 0
        needs_masked = case.scene in _MASKED_SCENES
        cov_list = coverage.get(case.name, [])
        coverage_ok = len(cov_list) > 0 and all(
            cov is not None
            and cov["auto"] == expected_auto
            and cov["motion"] == expected_motion
            and (cov["maskedRuns"] > 0 if needs_masked else True)
            for cov in cov_list
        )
        is_extension = case.name not in established_names
        if is_extension and not coverage_ok:
            extension_coverage_ok = False

        for cov in cov_list:
            if cov is None or cov["auto"] != expected_auto or cov["motion"] != expected_motion:
                continue
            entry = "motion" if cov["motion"] else "plain"
            if cov["opaqueRuns"] > 0:
                cell_covered[("opaque-auto" if cov["auto"] else "opaque-manual", entry)] = True
            if cov["maskedRuns"] > 0:
                cell_covered[("masked-auto" if cov["auto"] else "masked-manual", entry)] = True

        cases_report[case.name] = {
            "scene": case.scene, "temporal": case.temporal, "renderScale": case.renderScale,
            "auto": case.auto, "isExtension": is_extension,
            "hashes": hash_sets, "nullCounts": null_counts,
            "candidates": candidates, "referenceMatch": reference_match,
            "coverageOk": coverage_ok,
        }

    cells_complete = all(cell_covered.values())
    coverage_complete = cells_complete and extension_coverage_ok

    # A "complete" session is a genuine full parity run: every one of H, A and B, exactly 8 rounds,
    # and the full 29-case CASES list in its canonical order (not merely whatever `cases` this
    # particular evaluate() call was given — a header claiming a case subset, or a reduced round
    # count, must never be reported complete, however fully that subset was itself recorded).
    builds_exact = sorted(header.get("builds", [])) == ["A", "B", "H"]
    rounds_exact = header.get("rounds") == DEFAULT_ROUNDS
    full_case_names = [c.name for c in CASES]
    cases_exact = header.get("cases") == full_case_names
    if builds_exact and rounds_exact and cases_exact:
        expected_keys = {(r, b, c.name) for r in range(DEFAULT_ROUNDS) for b in ("H", "A", "B") for c in CASES}
        key_counts = Counter((rec["round"], rec["build"], rec["case"]) for rec in captures)
        session_complete = (
            set(key_counts.keys()) == expected_keys
            and all(count == 1 for count in key_counts.values())
        )
    else:
        session_complete = False

    candidates_pass = {
        candidate: session_complete and coverage_complete and all(
            cases_report[c.name]["candidates"][candidate]["unchanged"] for c in cases)
        for candidate in ("A", "B")
    }

    return {
        "capturesScored": len(captures),
        "sessionComplete": session_complete,
        "cases": cases_report,
        "coverageCells": {f"{v}/{e}": covered for (v, e), covered in cell_covered.items()},
        "extensionCoverageOk": extension_coverage_ok,
        "coverageComplete": coverage_complete,
        "candidatesPass": candidates_pass,
    }


def write_report(output: Path, report: dict) -> None:
    output = Path(output)
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    lines = [
        f"Captures scored: {report['capturesScored']}",
        f"Session complete (header builds == H,A,B, header rounds == {DEFAULT_ROUNDS}, header "
        f"cases == the full {len(CASES)}-case list, and every round/build/case recorded exactly "
        f"once): {report['sessionComplete']}",
        f"Extension coverage OK (gates the decision; the 15 established cases' coverage claims "
        f"below are reported for information only): {report['extensionCoverageOk']}",
        f"Coverage complete (extension coverage and all eight variant/entry cells): "
        f"{report['coverageComplete']}",
        f"Candidate A passes rendered parity: {report['candidatesPass']['A']}",
        f"Candidate B passes rendered parity: {report['candidatesPass']['B']}",
        "",
        "| Case | Scene | Temporal | Scale | Auto | Extension | H hashes | Null H/A/B | "
        "A unchanged | A unseen | B unchanged | B unseen | Coverage OK | Ref match (H) |",
        "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|",
    ]
    for name, row in report["cases"].items():
        ref = row["referenceMatch"]["H"] if row["referenceMatch"] is not None else "n/a"
        nulls = f"{row['nullCounts']['H']}/{row['nullCounts']['A']}/{row['nullCounts']['B']}"
        lines.append(
            f"| {name} | {row['scene']} | {row['temporal']} | {row['renderScale']} | "
            f"{row['auto']} | {row['isExtension']} | {', '.join(row['hashes']['H']) or 'none'} | "
            f"{nulls} | "
            f"{row['candidates']['A']['unchanged']} | {', '.join(row['candidates']['A']['unseenHashes']) or 'none'} | "
            f"{row['candidates']['B']['unchanged']} | {', '.join(row['candidates']['B']['unseenHashes']) or 'none'} | "
            f"{row['coverageOk']} | {ref} |")
    (output / "report.md").write_text("\n".join(lines) + "\n")


class SessionTests(unittest.TestCase):
    _EIGHT_CELL_CASES = [
        Case("opaque-manual-plain", "sponza", "off", 1.0, False),
        Case("opaque-manual-motion", "sponza", "taa", 1.0, False),
        Case("opaque-auto-plain", "sponza", "off", 1.0, True),
        Case("opaque-auto-motion", "sponza", "taa", 1.0, True),
        Case("masked-manual-plain", "san-miguel", "off", 1.0, False),
        Case("masked-manual-motion", "san-miguel", "taa", 1.0, False),
        Case("masked-auto-plain", "san-miguel", "off", 1.0, True),
        Case("masked-auto-motion", "san-miguel", "taa", 1.0, True),
    ]
    _EIGHT_CELL_COVERAGE = {
        "opaque-manual-plain": [{"auto": 0, "motion": 0, "opaqueRuns": 4, "maskedRuns": 0}],
        "opaque-manual-motion": [{"auto": 0, "motion": 1, "opaqueRuns": 4, "maskedRuns": 0}],
        "opaque-auto-plain": [{"auto": 1, "motion": 0, "opaqueRuns": 4, "maskedRuns": 0}],
        "opaque-auto-motion": [{"auto": 1, "motion": 1, "opaqueRuns": 4, "maskedRuns": 0}],
        "masked-manual-plain": [{"auto": 0, "motion": 0, "opaqueRuns": 4, "maskedRuns": 2}],
        "masked-manual-motion": [{"auto": 0, "motion": 1, "opaqueRuns": 4, "maskedRuns": 2}],
        "masked-auto-plain": [{"auto": 1, "motion": 0, "opaqueRuns": 4, "maskedRuns": 2}],
        "masked-auto-motion": [{"auto": 1, "motion": 1, "opaqueRuns": 4, "maskedRuns": 2}],
    }

    @staticmethod
    def _bmp_bytes(extra: bytes = b"") -> bytes:
        header = bytearray(26)
        header[:2] = b"BM"
        return bytes(header) + extra

    @staticmethod
    def _full_session_coverage() -> dict[str, list[dict]]:
        """A synthetic H coverage entry per real CASES row that exactly matches its own claim."""
        coverage = {}
        for case in CASES:
            auto = 1 if case.auto else 0
            motion = 1 if case.temporal in ("taa", "metalfx") else 0
            masked = 2 if case.scene in _MASKED_SCENES else 0
            coverage[case.name] = [{"auto": auto, "motion": motion, "opaqueRuns": 4, "maskedRuns": masked}]
        return coverage

    # -- cases / rotation / argv / env -----------------------------------------------------

    def test_cases_are_29_unique_and_shaped(self):
        self.assertEqual(len(CASES), 29)
        self.assertEqual(len({c.name for c in CASES}), 29)
        reference = json.loads(REFERENCE_PATH.read_text())
        self.assertEqual(sum(1 for c in CASES if not c.auto), 15 + 4)
        self.assertEqual(sum(1 for c in CASES if c.auto), 10)
        reference_names = {row["name"] for row in reference["images"]}
        self.assertTrue(reference_names <= {c.name for c in CASES})
        for scene in _EXTENSION_SCENES:
            self.assertIn(Case(f"{scene}-auto-off", scene, "off", 1.0, True), CASES)
            self.assertIn(Case(f"{scene}-auto-taa-1", scene, "taa", 1.0, True), CASES)
        for scene in _MASKED_SCENES:
            self.assertIn(Case(f"{scene}-manual-off", scene, "off", 1.0, False), CASES)
            self.assertIn(Case(f"{scene}-manual-taa-1", scene, "taa", 1.0, False), CASES)

    def test_round_order_rotates_left(self):
        self.assertEqual(round_order(0), ["H", "A", "B"])
        self.assertEqual(round_order(1), ["A", "B", "H"])
        self.assertEqual(round_order(2), ["B", "H", "A"])
        self.assertEqual(round_order(3), ["H", "A", "B"])
        self.assertEqual(round_order(8), ["B", "H", "A"])

    def test_command_for_matches_parity_style(self):
        case = Case("sponza-taa-1", "sponza", "taa", 1.0, False)
        self.assertEqual(
            command_for(Path("/build/App"), case, Path("/out/image.bmp")),
            ["/build/App", "--scene", "sponza", "--temporal", "taa",
             "--render-scale", "1", "--frames", "32", "--screenshot", "/out/image.bmp"])
        half = Case("sponza-taa-0.5", "sponza", "taa", 0.5, False)
        self.assertIn("0.5", command_for(Path("/build/App"), half, Path("/out/image.bmp")))

    def test_env_isolation_strips_all_lmx_and_mtl_vars_and_sets_exact_set(self):
        manual = Case("san-miguel-manual-off", "san-miguel", "off", 1.0, False)
        auto = Case("san-miguel-auto-off", "san-miguel", "off", 1.0, True)
        polluted = {
            "LMX_EXPERIMENT_AUTO_EXPOSURE": "1",
            "LMX_SCREENSHOT_NO_BLOOM": "1",
            "LMX_MAX_FRAMES": "5",
            "MTL_CAPTURE_ENABLED": "1",
            "MTL_DEBUG_LAYER": "0",
            "SOME_OTHER_VAR": "keep-me",
        }
        with mock.patch.dict(os.environ, polluted):
            manual_env = env_for(manual)
            auto_env = env_for(auto)
        for leaked in ("LMX_EXPERIMENT_AUTO_EXPOSURE", "LMX_SCREENSHOT_NO_BLOOM", "LMX_MAX_FRAMES",
                       "MTL_CAPTURE_ENABLED"):
            self.assertNotIn(leaked, manual_env)
        self.assertEqual(manual_env["MTL_DEBUG_LAYER"], "1")
        self.assertEqual(manual_env["LMX_EXPERIMENT_PIPELINE_LOG"], "1")
        self.assertEqual(manual_env["SOME_OTHER_VAR"], "keep-me")
        self.assertEqual(auto_env["LMX_EXPERIMENT_AUTO_EXPOSURE"], "1")
        extras = {k for k in manual_env if k.startswith("LMX_") or k.startswith("MTL_")} - {
            "MTL_DEBUG_LAYER", "LMX_EXPERIMENT_PIPELINE_LOG"}
        self.assertEqual(extras, set())

    def test_parse_coverage_returns_last_matching_line(self):
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "r0-H-sponza-off.log"
            log.write_text(
                "[2026-09-24 00:00:00.000] [info] r4.1-coverage auto=0 motion=0 opaqueRuns=12 maskedRuns=0\n"
                "[2026-09-24 00:00:00.016] [info] r4.1-coverage auto=0 motion=0 opaqueRuns=12 maskedRuns=3\n"
            )
            self.assertEqual(parse_coverage(log),
                              {"auto": 0, "motion": 0, "opaqueRuns": 12, "maskedRuns": 3})

    def test_parse_coverage_missing_line_is_none(self):
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "empty.log"
            log.write_text("nothing relevant here\n")
            self.assertIsNone(parse_coverage(log))

    # -- strict rendered-parity rule --------------------------------------------------------

    def test_strict_rule_unseen_candidate_hash_is_changed(self):
        cases = [Case("sponza-off", "sponza", "off", 1.0, False)]
        records = [
            {"kind": "capture", "round": 0, "build": "H", "case": "sponza-off", "sha256": "aa"},
            {"kind": "capture", "round": 1, "build": "H", "case": "sponza-off", "sha256": "aa"},
            {"kind": "capture", "round": 0, "build": "A", "case": "sponza-off", "sha256": "aa"},
            {"kind": "capture", "round": 1, "build": "A", "case": "sponza-off", "sha256": "bb"},
        ]
        reference = {"images": []}
        report = evaluate(records, {}, reference, cases=cases)
        self.assertFalse(report["cases"]["sponza-off"]["candidates"]["A"]["unchanged"])

    def test_strict_rule_failed_capture_is_changed(self):
        cases = [Case("sponza-off", "sponza", "off", 1.0, False)]
        records = [
            {"kind": "capture", "round": 0, "build": "H", "case": "sponza-off", "sha256": "aa"},
            {"kind": "capture", "round": 0, "build": "A", "case": "sponza-off", "sha256": "aa"},
            {"kind": "capture", "round": 1, "build": "A", "case": "sponza-off", "sha256": None},
        ]
        reference = {"images": []}
        report = evaluate(records, {}, reference, cases=cases)
        self.assertFalse(report["cases"]["sponza-off"]["candidates"]["A"]["unchanged"])

    def test_strict_rule_matching_subset_is_unchanged(self):
        cases = [Case("sponza-off", "sponza", "off", 1.0, False)]
        records = [
            {"kind": "capture", "round": 0, "build": "H", "case": "sponza-off", "sha256": "aa"},
            {"kind": "capture", "round": 1, "build": "H", "case": "sponza-off", "sha256": "bb"},
            {"kind": "capture", "round": 0, "build": "B", "case": "sponza-off", "sha256": "aa"},
            {"kind": "capture", "round": 1, "build": "B", "case": "sponza-off", "sha256": "aa"},
        ]
        reference = {"images": []}
        report = evaluate(records, {}, reference, cases=cases)
        self.assertTrue(report["cases"]["sponza-off"]["candidates"]["B"]["unchanged"])

    def test_report_lists_hashes_null_counts_and_unseen_hashes_per_candidate(self):
        cases = [Case("sponza-off", "sponza", "off", 1.0, False)]
        records = [
            {"kind": "capture", "round": 0, "build": "H", "case": "sponza-off", "sha256": "aa"},
            {"kind": "capture", "round": 1, "build": "H", "case": "sponza-off", "sha256": None},
            {"kind": "capture", "round": 0, "build": "A", "case": "sponza-off", "sha256": "aa"},
            {"kind": "capture", "round": 1, "build": "A", "case": "sponza-off", "sha256": "bb"},
            {"kind": "capture", "round": 0, "build": "B", "case": "sponza-off", "sha256": "aa"},
        ]
        reference = {"images": []}
        report = evaluate(records, {}, reference, cases=cases)
        row = report["cases"]["sponza-off"]
        self.assertEqual(row["hashes"]["H"], ["aa"])
        self.assertEqual(row["nullCounts"], {"H": 1, "A": 0, "B": 0})
        self.assertEqual(row["candidates"]["A"]["unseenHashes"], ["bb"])
        self.assertFalse(row["candidates"]["A"]["unchanged"])
        self.assertTrue(row["candidates"]["B"]["unchanged"])
        self.assertEqual(row["candidates"]["B"]["unseenHashes"], [])

    # -- coverage: per-case checks, gating, cells -------------------------------------------

    def test_coverage_check_requires_masked_runs_for_masked_scenes(self):
        cases = [Case("san-miguel-manual-off", "san-miguel", "off", 1.0, False)]
        records = [
            {"kind": "capture", "round": 0, "build": "H", "case": "san-miguel-manual-off", "sha256": "aa"},
        ]
        coverage = {"san-miguel-manual-off": [{"auto": 0, "motion": 0, "opaqueRuns": 5, "maskedRuns": 0}]}
        reference = {"images": []}
        report = evaluate(records, coverage, reference, cases=cases)
        self.assertFalse(report["cases"]["san-miguel-manual-off"]["coverageOk"])

        coverage_ok = {"san-miguel-manual-off": [{"auto": 0, "motion": 0, "opaqueRuns": 5, "maskedRuns": 2}]}
        report_ok = evaluate(records, coverage_ok, reference, cases=cases)
        self.assertTrue(report_ok["cases"]["san-miguel-manual-off"]["coverageOk"])

    def test_coverage_entry_contradicting_case_claim_earns_no_cell_credit(self):
        # A manual case (claims auto=0, motion=0) whose only H log entry falsely reports auto=1.
        # If evaluate() credited cells from the entry's own auto/motion fields instead of gating
        # on the case's claim, this entry (auto=1, motion=0, opaqueRuns>0) would wrongly credit
        # opaque-auto/plain. The contradiction guard must suppress that credit entirely: neither
        # opaque-manual/plain (this case's own cell) nor opaque-auto/plain may become True.
        case = Case("sponza-off", "sponza", "off", 1.0, False)  # claims auto=0, motion=0
        coverage = {"sponza-off": [{"auto": 1, "motion": 0, "opaqueRuns": 5, "maskedRuns": 0}]}
        records = [{"kind": "capture", "round": 0, "build": "H", "case": "sponza-off", "sha256": "aa"}]
        reference = {"images": []}
        report = evaluate(records, coverage, reference, cases=[case])
        self.assertFalse(report["cases"]["sponza-off"]["coverageOk"])
        self.assertFalse(report["coverageCells"]["opaque-manual/plain"])
        self.assertFalse(report["coverageCells"]["opaque-auto/plain"])

    def test_coverage_cells_track_all_eight_variant_entry_pairs(self):
        cases = self._EIGHT_CELL_CASES
        records = [{"kind": "capture", "round": 0, "build": "H", "case": c.name, "sha256": "x"} for c in cases]
        reference = {"images": []}
        report = evaluate(records, self._EIGHT_CELL_COVERAGE, reference, cases=cases)
        self.assertTrue(all(report["coverageCells"].values()))
        self.assertEqual(len(report["coverageCells"]), 8)

        coverage = dict(self._EIGHT_CELL_COVERAGE)
        coverage["masked-auto-motion"] = [{"auto": 1, "motion": 1, "opaqueRuns": 4, "maskedRuns": 0}]
        report_incomplete = evaluate(records, coverage, reference, cases=cases)
        self.assertFalse(report_incomplete["coverageCells"]["masked-auto/motion"])

    def test_extension_case_coverage_failure_blocks_decision(self):
        broken = Case("broken-extension-auto-off", "damaged-helmet", "off", 1.0, True)
        cases = self._EIGHT_CELL_CASES + [broken]
        coverage = dict(self._EIGHT_CELL_COVERAGE)
        # Claims auto=1 (broken.auto is True) but the H log reports auto=0: a contradiction.
        coverage["broken-extension-auto-off"] = [{"auto": 0, "motion": 0, "opaqueRuns": 4, "maskedRuns": 0}]
        records = [{"kind": "capture", "round": 0, "build": "H", "case": c.name, "sha256": "x"} for c in cases]
        reference = {"images": []}
        report = evaluate(records, coverage, reference, cases=cases)
        self.assertTrue(all(report["coverageCells"].values()))
        self.assertFalse(report["extensionCoverageOk"])
        self.assertFalse(report["coverageComplete"])
        self.assertFalse(report["candidatesPass"]["A"])
        self.assertFalse(report["candidatesPass"]["B"])

    def test_established_case_coverage_failure_does_not_block_decision(self):
        established = Case("sponza-off", "sponza", "off", 1.0, False)
        cases = self._EIGHT_CELL_CASES + [established]
        coverage = dict(self._EIGHT_CELL_COVERAGE)
        # Wildly wrong claim on an established (non-extension) case: informational only.
        coverage["sponza-off"] = [{"auto": 1, "motion": 1, "opaqueRuns": 0, "maskedRuns": 0}]
        records = [{"kind": "capture", "round": 0, "build": "H", "case": c.name, "sha256": "x"} for c in cases]
        reference = {"images": [{"name": "sponza-off", "scene": "sponza", "temporal": "off",
                                  "renderScale": 1.0, "sha256": "deadbeef"}]}
        report = evaluate(records, coverage, reference, cases=cases)
        self.assertTrue(report["extensionCoverageOk"])
        self.assertTrue(report["coverageComplete"])
        self.assertFalse(report["cases"]["sponza-off"]["coverageOk"])
        self.assertFalse(report["cases"]["sponza-off"]["isExtension"])

    # -- session completeness ----------------------------------------------------------------

    def test_session_complete_when_matrix_fully_recorded_across_all_rounds(self):
        # sessionComplete requires the header to claim the FULL CASES list (not a subset): see
        # test_four_case_probe_with_three_builds_gives_session_incomplete for the negative case.
        header = {"builds": ["A", "B", "H"], "rounds": DEFAULT_ROUNDS, "cases": [c.name for c in CASES]}
        records = []
        for r in range(DEFAULT_ROUNDS):
            for build in ("H", "A", "B"):
                for case in CASES:
                    records.append({"kind": "capture", "round": r, "build": build,
                                     "case": case.name, "sha256": "same"})
        reference = {"images": []}
        coverage = self._full_session_coverage()
        report = evaluate(records, coverage, reference, cases=CASES, header=header)
        self.assertTrue(report["sessionComplete"])
        self.assertEqual(report["capturesScored"], DEFAULT_ROUNDS * 3 * len(CASES))
        self.assertTrue(report["candidatesPass"]["A"])
        self.assertTrue(report["candidatesPass"]["B"])

        incomplete_records = records[:-1]
        report_incomplete = evaluate(incomplete_records, coverage, reference,
                                      cases=CASES, header=header)
        self.assertFalse(report_incomplete["sessionComplete"])
        self.assertFalse(report_incomplete["candidatesPass"]["A"])
        self.assertFalse(report_incomplete["candidatesPass"]["B"])

    def test_session_incomplete_when_header_builds_are_not_exact_triple(self):
        case = Case("sponza-off", "sponza", "off", 1.0, False)
        header = {"builds": ["H"], "rounds": DEFAULT_ROUNDS, "cases": ["sponza-off"]}
        records = [{"kind": "capture", "round": r, "build": "H", "case": "sponza-off", "sha256": "x"}
                   for r in range(DEFAULT_ROUNDS)]
        reference = {"images": []}
        report = evaluate(records, {}, reference, cases=[case], header=header)
        self.assertFalse(report["sessionComplete"])

    def test_four_case_probe_with_three_builds_gives_session_incomplete(self):
        # Regression for a verdict that passed on a subset of cases: a header claiming only the
        # four san-miguel cases, fully and correctly recorded over 8 rounds and all three builds,
        # must still report sessionComplete=False (and so no candidate passes), because the header
        # does not claim the full 29-case CASES list.
        san_miguel_cases = [c for c in CASES if c.scene == "san-miguel"]
        self.assertEqual(len(san_miguel_cases), 4)
        header = {"builds": ["A", "B", "H"], "rounds": DEFAULT_ROUNDS,
                  "cases": [c.name for c in san_miguel_cases]}
        records = []
        for r in range(DEFAULT_ROUNDS):
            for build in ("H", "A", "B"):
                for case in san_miguel_cases:
                    records.append({"kind": "capture", "round": r, "build": build,
                                     "case": case.name, "sha256": "same"})
        reference = {"images": []}
        report = evaluate(records, {}, reference, cases=san_miguel_cases, header=header)
        self.assertEqual(report["capturesScored"], DEFAULT_ROUNDS * 3 * 4)
        self.assertFalse(report["sessionComplete"])
        self.assertFalse(report["candidatesPass"]["A"])
        self.assertFalse(report["candidatesPass"]["B"])

    def test_one_round_full_case_header_gives_session_incomplete(self):
        # Regression: a three-build header claiming the full case list but only 1 round, fully
        # recorded for that single round, must still report sessionComplete=False.
        header = {"builds": ["A", "B", "H"], "rounds": 1, "cases": [c.name for c in CASES]}
        records = []
        for build in ("H", "A", "B"):
            for case in CASES:
                records.append({"kind": "capture", "round": 0, "build": build,
                                 "case": case.name, "sha256": "same"})
        reference = {"images": []}
        report = evaluate(records, {}, reference, cases=CASES, header=header)
        self.assertEqual(report["capturesScored"], 3 * len(CASES))
        self.assertFalse(report["sessionComplete"])
        self.assertFalse(report["candidatesPass"]["A"])
        self.assertFalse(report["candidatesPass"]["B"])

    def test_captures_scored_counts_only_capture_records(self):
        records = [
            {"kind": "header", "builds": ["A", "B", "H"]},
            {"kind": "capture", "round": 0, "build": "H", "case": "sponza-off", "sha256": "aa"},
            {"kind": "capture", "round": 0, "build": "A", "case": "sponza-off", "sha256": "aa"},
        ]
        reference = {"images": []}
        report = evaluate(records, {}, reference, cases=[Case("sponza-off", "sponza", "off", 1.0, False)])
        self.assertEqual(report["capturesScored"], 2)

    def test_write_report_creates_json_and_markdown(self):
        cases = [Case("sponza-off", "sponza", "off", 1.0, False)]
        records = [{"kind": "capture", "round": 0, "build": "H", "case": "sponza-off", "sha256": "aa"}]
        reference = {"images": []}
        report = evaluate(records, {}, reference, cases=cases)
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            write_report(output, report)
            self.assertTrue((output / "report.json").is_file())
            self.assertTrue((output / "report.md").is_file())
            self.assertEqual(json.loads((output / "report.json").read_text()), report)
            markdown = (output / "report.md").read_text()
            self.assertIn("sponza-off", markdown)
            self.assertIn(str(report["capturesScored"]), markdown)

    # -- CLI argument helpers ------------------------------------------------------------------

    def test_parse_builds_arg_rejects_unknown_label(self):
        with self.assertRaises(ValueError):
            _parse_builds_arg("H=/a,X=/b")

    def test_parse_builds_arg_accepts_known_labels(self):
        builds = _parse_builds_arg("H=/a,A=/b,B=/c")
        self.assertEqual(set(builds), {"H", "A", "B"})

    def test_select_only_builds_rejects_unknown_label(self):
        builds = {"H": Path("/a"), "A": Path("/b")}
        with self.assertRaises(ValueError):
            _select_only_builds(builds, ["X"])

    def test_select_only_builds_filters_to_requested_labels(self):
        builds = {"H": Path("/a"), "A": Path("/b"), "B": Path("/c")}
        self.assertEqual(set(_select_only_builds(builds, ["H"])), {"H"})

    # -- run(): rounds, restart, provenance, malformed session files -------------------------

    def test_round_cap_rejects_non_eight_rounds_for_full_triple(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            app = root / "App"
            app.write_bytes(b"fake")
            output = root / "output"

            def capture(command, **kwargs):
                self.fail("subprocess.run should not be called when the round cap is rejected")

            # No explicit `cases` here: a full H/A/B triple must also use the full case list (see
            # test_cases_restriction_rejected_for_full_triple), so this isolates the rounds guard.
            with mock.patch.object(subprocess, "run", side_effect=capture):
                with self.assertRaises(ValueError):
                    run({"H": app, "A": app, "B": app}, output, rounds=3)

    def test_round_cap_allows_reduced_rounds_when_not_full_triple(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            app = root / "App"
            app.write_bytes(b"fake")
            output = root / "output"
            cases = [Case("sponza-off", "sponza", "off", 1.0, False)]

            def capture(command, **kwargs):
                Path(command[-1]).write_bytes(self._bmp_bytes())
                return subprocess.CompletedProcess(command, 0)

            with mock.patch.object(subprocess, "run", side_effect=capture):
                records = run({"H": app}, output, rounds=1, cases=cases)
            self.assertEqual(len(records), 1)

    def test_round_cap_mismatch_on_restart_is_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            app = root / "App"
            app.write_bytes(b"fake")
            output = root / "output"
            cases = [Case("sponza-off", "sponza", "off", 1.0, False)]

            def capture(command, **kwargs):
                Path(command[-1]).write_bytes(self._bmp_bytes())
                return subprocess.CompletedProcess(command, 0)

            with mock.patch.object(subprocess, "run", side_effect=capture):
                run({"H": app}, output, rounds=1, cases=cases)
                with self.assertRaises(ValueError):
                    run({"H": app}, output, rounds=2, cases=cases)

    def test_binary_provenance_stored_and_checked_on_restart(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            app_v1 = root / "App_v1"
            app_v1.write_bytes(b"version-one")
            app_v2 = root / "App_v2"
            app_v2.write_bytes(b"version-two")
            output = root / "output"
            cases = [Case("sponza-off", "sponza", "off", 1.0, False)]

            def capture(command, **kwargs):
                Path(command[-1]).write_bytes(self._bmp_bytes())
                return subprocess.CompletedProcess(command, 0)

            with mock.patch.object(subprocess, "run", side_effect=capture):
                run({"H": app_v1}, output, rounds=1, cases=cases)
                with self.assertRaises(ValueError):
                    run({"H": app_v2}, output, rounds=1, cases=cases)

    def test_restart_retries_capture_missing_after_crash(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            app = root / "App"
            app.write_bytes(b"fake")
            output = root / "output"
            cases = [Case("sponza-off", "sponza", "off", 1.0, False),
                     Case("sponza-taa-1", "sponza", "taa", 1.0, False)]

            def capture(command, **kwargs):
                Path(command[-1]).write_bytes(self._bmp_bytes())
                return subprocess.CompletedProcess(command, 0)

            with mock.patch.object(subprocess, "run", side_effect=capture):
                run({"H": app}, output, rounds=1, cases=cases)

            # Simulate a crash: the process died before appending sponza-taa-1's line at all.
            session_path = output / "session.jsonl"
            kept = [line for line in session_path.read_text().splitlines()
                    if json.loads(line).get("case") != "sponza-taa-1"]
            session_path.write_text("\n".join(kept) + "\n")

            calls = []

            def capture_and_track(command, **kwargs):
                calls.append(command[-1])
                Path(command[-1]).write_bytes(self._bmp_bytes())
                return subprocess.CompletedProcess(command, 0)

            with mock.patch.object(subprocess, "run", side_effect=capture_and_track):
                run({"H": app}, output, rounds=1, cases=cases)
            self.assertEqual(len(calls), 1)
            self.assertIn("sponza-taa-1", calls[0])

    def test_restart_does_not_retry_recorded_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            app = root / "App"
            app.write_bytes(b"fake")
            output = root / "output"
            cases = [Case("sponza-off", "sponza", "off", 1.0, False)]

            def capture_fail(command, **kwargs):
                return subprocess.CompletedProcess(command, 1)

            with mock.patch.object(subprocess, "run", side_effect=capture_fail):
                run({"H": app}, output, rounds=1, cases=cases)
            _, records = _read_session(output)
            self.assertIsNone(records[0]["sha256"])

            def fail_if_called(command, **kwargs):
                self.fail("subprocess.run should not be called for an already-recorded capture")

            with mock.patch.object(subprocess, "run", side_effect=fail_if_called):
                run({"H": app}, output, rounds=1, cases=cases)

    def test_session_refuses_mismatched_build_set_or_cases(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            app = root / "App"
            app.write_bytes(b"fake")
            output = root / "output"
            cases = [Case("sponza-off", "sponza", "off", 1.0, False)]

            def capture(command, **kwargs):
                Path(command[-1]).write_bytes(self._bmp_bytes())
                return subprocess.CompletedProcess(command, 0)

            with mock.patch.object(subprocess, "run", side_effect=capture):
                run({"H": app, "A": app}, output, rounds=1, cases=cases)

                with self.assertRaises(ValueError):
                    run({"H": app}, output, rounds=1, cases=cases)

                other_cases = [Case("sponza-taa-1", "sponza", "taa", 1.0, False)]
                with self.assertRaises(ValueError):
                    run({"H": app, "A": app}, output, rounds=1, cases=other_cases)

    def test_timeout_and_nonzero_exit_record_null_sha_and_driver_continues(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            app = root / "App"
            app.write_bytes(b"fake")
            output = root / "output"
            cases = [Case("sponza-off", "sponza", "off", 1.0, False),
                     Case("sponza-taa-1", "sponza", "taa", 1.0, False),
                     Case("material-lab-off", "material-lab", "off", 1.0, False)]

            def capture(command, **kwargs):
                if "sponza-off" in command[-1]:
                    raise subprocess.TimeoutExpired(command, 1)
                if "sponza-taa-1" in command[-1]:
                    return subprocess.CompletedProcess(command, 1)
                Path(command[-1]).write_bytes(self._bmp_bytes())
                return subprocess.CompletedProcess(command, 0)

            with mock.patch.object(subprocess, "run", side_effect=capture):
                records = run({"H": app}, output, rounds=1, cases=cases, timeout=1)
            by_case = {r["case"]: r for r in records if r["kind"] == "capture"}
            self.assertIsNone(by_case["sponza-off"]["sha256"])
            self.assertIsNone(by_case["sponza-taa-1"]["sha256"])
            self.assertEqual(by_case["sponza-taa-1"]["exit"], 1)
            self.assertIsNotNone(by_case["material-lab-off"]["sha256"])

    def test_images_are_deduplicated_by_hash(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            app = root / "App"
            app.write_bytes(b"fake")
            output = root / "output"

            def capture(command, **kwargs):
                Path(command[-1]).write_bytes(self._bmp_bytes())
                return subprocess.CompletedProcess(command, 0)

            # Full triple must use the default full case list; check dedup on one representative case.
            with mock.patch.object(subprocess, "run", side_effect=capture):
                run({"H": app, "A": app, "B": app}, output, rounds=DEFAULT_ROUNDS)
            images = list((output / "images" / "sponza-off").glob("*.bmp"))
            self.assertEqual(len(images), 1)

    def test_malformed_line_not_last_raises(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            session_path = output / "session.jsonl"
            header = {"kind": "header", "builds": ["H"], "cases": ["sponza-off"],
                      "rounds": 1, "buildInfo": {}}
            good = {"kind": "capture", "round": 0, "build": "H", "case": "sponza-off",
                    "sha256": "aa", "exit": 0, "seconds": 1.0, "utc": "now"}
            session_path.write_text(json.dumps(header) + "\n{not json at all\n" + json.dumps(good) + "\n")
            with self.assertRaises(ValueError):
                _read_session(output)

    def test_torn_final_line_is_truncated_treated_missing_and_warned(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            session_path = output / "session.jsonl"
            header = {"kind": "header", "builds": ["H"], "cases": ["sponza-off"],
                      "rounds": 1, "buildInfo": {}}
            good = {"kind": "capture", "round": 0, "build": "H", "case": "sponza-off",
                    "sha256": "aa", "exit": 0, "seconds": 1.0, "utc": "now"}
            torn = '{"kind": "capture", "round": 1, "build": "H"'
            session_path.write_text(json.dumps(header) + "\n" + json.dumps(good) + "\n" + torn)

            stderr = io.StringIO()
            with contextlib.redirect_stderr(stderr):
                header_out, records = _read_session(output)
            self.assertEqual(len(records), 1)
            self.assertEqual(records[0]["case"], "sponza-off")
            self.assertIn("warning", stderr.getvalue().lower())
            remaining = session_path.read_text()
            self.assertNotIn('"round": 1', remaining)
            self.assertTrue(remaining.endswith("\n"))

    def test_evaluate_read_is_read_only_and_never_truncates_a_torn_final_line(self):
        # --evaluate (and any other reporting read) must never rewrite session.jsonl. A torn final
        # line there means the session is incomplete, reported as such, with the file untouched.
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            session_path = output / "session.jsonl"
            header = {"kind": "header", "builds": ["H"], "cases": ["sponza-off"],
                      "rounds": 1, "buildInfo": {}}
            good = {"kind": "capture", "round": 0, "build": "H", "case": "sponza-off",
                    "sha256": "aa", "exit": 0, "seconds": 1.0, "utc": "now"}
            torn = '{"kind": "capture", "round": 1, "build": "H"'
            original_text = json.dumps(header) + "\n" + json.dumps(good) + "\n" + torn
            session_path.write_text(original_text)

            stderr = io.StringIO()
            with contextlib.redirect_stderr(stderr):
                header_out, records = _read_session(output, truncate_torn=False)
            self.assertEqual(len(records), 1)
            self.assertEqual(records[0]["case"], "sponza-off")
            self.assertEqual(session_path.read_text(), original_text)

    def test_torn_final_line_during_evaluate_reports_incomplete_without_writing(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            session_path = output / "session.jsonl"
            header = {"kind": "header", "builds": ["A", "B", "H"], "rounds": DEFAULT_ROUNDS,
                      "cases": [c.name for c in CASES]}
            full_records = []
            for r in range(DEFAULT_ROUNDS):
                for build in ("H", "A", "B"):
                    for case in CASES:
                        full_records.append({"kind": "capture", "round": r, "build": build,
                                              "case": case.name, "sha256": "same"})
            lines = [json.dumps(header)] + [json.dumps(rec) for rec in full_records[:-1]]
            torn = json.dumps(full_records[-1])[:-5]  # cut the last record short: an invalid tail
            original_text = "\n".join(lines) + "\n" + torn
            session_path.write_text(original_text)

            with contextlib.redirect_stderr(io.StringIO()):
                header_out, records = _read_session(output, truncate_torn=False)
            report = evaluate(records, {}, {"images": []}, cases=CASES, header=header_out)
            self.assertFalse(report["sessionComplete"])
            self.assertFalse(report["candidatesPass"]["A"])
            self.assertFalse(report["candidatesPass"]["B"])
            self.assertEqual(session_path.read_text(), original_text)

    # -- run() mechanics: round order, subprocess kwargs, independent candidates -------------

    def test_run_follows_round_order_across_rounds(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            # Only H and A: a reduced build set (not the full H/A/B triple), so an explicit
            # single-case list is allowed and rounds isn't capped to 8.
            apps = {}
            for label in ("H", "A"):
                path = root / f"{label}_app"
                path.write_bytes(label.encode())
                apps[label] = path
            output = root / "output"
            cases = [Case("sponza-off", "sponza", "off", 1.0, False)]
            seen_order = []

            def capture(command, **kwargs):
                seen_order.append(Path(command[0]).name.split("_")[0])
                Path(command[-1]).write_bytes(self._bmp_bytes(command[0].encode()))
                return subprocess.CompletedProcess(command, 0)

            with mock.patch.object(subprocess, "run", side_effect=capture):
                run(apps, output, rounds=DEFAULT_ROUNDS, cases=cases)

            expected = []
            for r in range(DEFAULT_ROUNDS):
                expected.extend(b for b in round_order(r) if b in ("H", "A"))
            self.assertEqual(seen_order, expected)

    def test_subprocess_receives_cwd_env_and_timeout(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            app_dir = root / "buildH"
            app_dir.mkdir()
            app = app_dir / "App"
            app.write_bytes(b"fake")
            output = root / "output"
            case = Case("san-miguel-auto-off", "san-miguel", "off", 1.0, True)
            seen_kwargs = {}

            def capture(command, **kwargs):
                seen_kwargs.update(kwargs)
                Path(command[-1]).write_bytes(self._bmp_bytes())
                return subprocess.CompletedProcess(command, 0)

            with mock.patch.object(subprocess, "run", side_effect=capture):
                run({"H": app}, output, rounds=1, cases=[case])

            self.assertEqual(seen_kwargs["cwd"], app.resolve().parent)
            self.assertEqual(seen_kwargs["timeout"], DEFAULT_TIMEOUT)
            self.assertEqual(seen_kwargs["env"], env_for(case))

    def test_candidate_a_changed_and_b_unchanged_in_one_session(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            app = root / "App"
            app.write_bytes(b"fake")
            output = root / "output"

            def capture(command, **kwargs):
                dest = command[-1]
                extra = b"different" if "-A-" in dest else b"same"
                Path(dest).write_bytes(self._bmp_bytes(extra))
                return subprocess.CompletedProcess(command, 0)

            # Full triple: must use the default full case list; check one representative case.
            with mock.patch.object(subprocess, "run", side_effect=capture):
                run({"H": app, "A": app, "B": app}, output, rounds=DEFAULT_ROUNDS)

            header, records = _read_session(output)
            report = evaluate(records, {}, {"images": []}, cases=CASES, header=header)
            self.assertFalse(report["cases"]["sponza-off"]["candidates"]["A"]["unchanged"])
            self.assertTrue(report["cases"]["sponza-off"]["candidates"]["B"]["unchanged"])

    def test_cases_restriction_rejected_for_full_triple(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            app = root / "App"
            app.write_bytes(b"fake")
            output = root / "output"
            cases = [Case("sponza-off", "sponza", "off", 1.0, False)]

            def capture(command, **kwargs):
                self.fail("subprocess.run should not be called when --cases is rejected")

            with mock.patch.object(subprocess, "run", side_effect=capture):
                with self.assertRaises(ValueError):
                    run({"H": app, "A": app, "B": app}, output, rounds=DEFAULT_ROUNDS, cases=cases)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--builds", type=str)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--rounds", type=int, default=DEFAULT_ROUNDS)
    parser.add_argument("--only-builds", nargs="+")
    parser.add_argument("--cases", nargs="+")
    parser.add_argument("--evaluate", type=Path)
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()

    if args.selftest:
        result = unittest.TextTestRunner(verbosity=2).run(
            unittest.defaultTestLoader.loadTestsFromTestCase(SessionTests))
        return 0 if result.wasSuccessful() else 1

    if args.evaluate is not None:
        header, records = _read_session(args.evaluate, truncate_torn=False)
        if header is None:
            print(f"no session found under {args.evaluate}", file=sys.stderr)
            return 1
        cases = [c for c in CASES if c.name in header["cases"]]
        coverage = collect_coverage(args.evaluate, records, cases)
        reference = json.loads(REFERENCE_PATH.read_text())
        report = evaluate(records, coverage, reference, cases, header)
        write_report(args.evaluate, report)
        print(json.dumps({"capturesScored": report["capturesScored"],
                           "sessionComplete": report["sessionComplete"],
                           "coverageComplete": report["coverageComplete"],
                           "candidatesPass": report["candidatesPass"]}, indent=2))
        return 0

    if args.output is None or args.builds is None:
        parser.error("--builds and --output are required unless --selftest or --evaluate is used")

    try:
        builds = _parse_builds_arg(args.builds)
        builds = _select_only_builds(builds, args.only_builds)
    except ValueError as error:
        print(f"argument error: {error}", file=sys.stderr)
        return 1

    # None (the full CASES list) unless --cases explicitly restricts it; run() itself refuses a
    # restricted case list when the build set is the full H/A/B triple.
    cases = None
    if args.cases:
        selected = set(args.cases)
        cases = [c for c in CASES if c.name in selected]
        missing = selected - {c.name for c in cases}
        if missing:
            parser.error(f"unknown case names: {sorted(missing)}")

    try:
        run(builds, args.output, rounds=args.rounds, cases=cases)
    except ValueError as error:
        print(f"session refused: {error}", file=sys.stderr)
        return 1

    report_cases = cases if cases is not None else CASES
    header, records = _read_session(args.output, truncate_torn=False)
    coverage = collect_coverage(args.output, records, report_cases)
    reference = json.loads(REFERENCE_PATH.read_text())
    report = evaluate(records, coverage, reference, report_cases, header)
    write_report(args.output, report)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
