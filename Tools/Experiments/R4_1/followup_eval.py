#!/usr/bin/env python3
"""Score the R4.1 parent-calibrated parity follow-up (`docs/milestones/r/r4.1-followup.md`).

Stage 1 is an H-only, 8-round, 29-case session (`session.py`'s existing H-only multi-round mode)
that measures each case's parent repeatability before any candidate capture. Stage 2 is a fresh
H/A/B session scored two ways: repeatable cases keep R4.1's strict hash rule (reused from
`session.evaluate`); non-repeatable cases use a leave-one-round-out image comparison under
`Tools/Screenshots/compare.py`'s unchanged `strict` profile.

Amendment, 2026-09-25 (see the spec's "Amendment" note under Stage 1 and the line after decision
item 5): a case is non-repeatable for the decision when H's hashes vary in stage 1 OR in stage 2 -
only H's captures ever decide it, never a candidate's. Items 3, 4 and 5 (and their per-case detail)
are computed under this amended classification for the top-level verdict, and a second, clearly
labelled block reports the same items under the frozen stage-1-only classification.

Fix round 1 (review): stage 2 refuses to score if any capture (any build) has a null sha256 or a
non-zero exit, matching the same rule stage 1 already enforced; stage 1's own check was tightened
to also reject a non-zero exit, a duplicate or out-of-range-round record, or an unexpected build or
case; the top-level result carries a `pass`/`fail`/`incomplete` verdict; power only counts
measurable (null-control-holding) non-repeatable cases; non-discriminating cases (B passes) are
listed explicitly.

This module never renders and never edits `session.py` or `compare.py`.
"""
from __future__ import annotations

import argparse
import json
import shutil
import sys
import tempfile
import unittest
from collections import defaultdict
from pathlib import Path
from unittest import mock

import session

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "Screenshots"))
import compare  # noqa: E402  (path must be extended first)

DEFAULT_EXTENT = (1280, 720)
DEFAULT_ROUNDS = 8
STAGE1_ONLY_RULE = "stage1-only: repeatable iff all 8 H captures in stage 1 share one hash"


# ------------------------------------------------------------------------------------------------
# Stage 1: parent repeatability
# ------------------------------------------------------------------------------------------------

def _stage1_hashes_and_classification(stage1_dir: Path) -> tuple[dict[str, list[str]], dict[str, str]]:
    stage1_dir = Path(stage1_dir)
    header, records = session._read_session(stage1_dir, truncate_torn=False)
    if header is None:
        raise ValueError(f"no session found under {stage1_dir}")

    expected_cases = [c.name for c in session.CASES]
    case_names = set(expected_cases)
    if header.get("builds") != ["H"]:
        raise ValueError(f"stage 1 header must have builds ['H'], got {header.get('builds')!r}")
    if header.get("rounds") != DEFAULT_ROUNDS:
        raise ValueError(f"stage 1 header must have rounds {DEFAULT_ROUNDS}, got {header.get('rounds')!r}")
    header_cases = header.get("cases", [])
    if set(header_cases) != case_names or len(header_cases) != len(expected_cases):
        raise ValueError("stage 1 header must list exactly the 29 session.CASES names")

    # Exactly one record per (round, case): no duplicate, out-of-range-round, unexpected-case or
    # unexpected-build record, and the one record present must be a clean success (non-null
    # sha256, exit 0).
    seen: dict[tuple[int, str], dict] = {}
    for rec in records:
        if rec.get("kind") != "capture":
            continue
        case_name = rec.get("case")
        round_ = rec.get("round")
        build = rec.get("build")
        if build != "H":
            raise ValueError(f"unexpected build {build!r} in stage 1 session (H-only expected)")
        if case_name not in case_names:
            raise ValueError(f"unexpected case {case_name!r} in stage 1 session")
        if not isinstance(round_, int) or isinstance(round_, bool) or not (0 <= round_ < DEFAULT_ROUNDS):
            raise ValueError(f"out-of-range round {round_!r} for case {case_name!r} in stage 1 session")
        key = (round_, case_name)
        if key in seen:
            raise ValueError(f"duplicate stage 1 capture for round {round_} case {case_name!r}")
        seen[key] = rec

    hashes_by_case: dict[str, list[str]] = {}
    classification: dict[str, str] = {}
    for case in session.CASES:
        hashes = []
        for r in range(DEFAULT_ROUNDS):
            rec = seen.get((r, case.name))
            if rec is None:
                raise ValueError(f"missing stage 1 capture for round {r} case {case.name!r}")
            if rec.get("sha256") is None or rec.get("exit") != 0:
                raise ValueError(
                    f"stage 1 capture round={r} case={case.name!r} failed "
                    f"(sha256={rec.get('sha256')!r}, exit={rec.get('exit')!r})")
            hashes.append(rec["sha256"])
        hashes_by_case[case.name] = hashes
        classification[case.name] = "repeatable" if len(set(hashes)) == 1 else "non-repeatable"
    return hashes_by_case, classification


def classify(stage1_dir: Path) -> dict[str, str]:
    """Classify every session.CASES case from a stage 1 (H-only, 8-round) session alone.

    Raises ValueError if the header does not match the expected shape (builds ["H"], 8 rounds,
    exactly the 29 session.CASES names), if any (round, case) does not have exactly one record, or
    if that record is not a clean success (non-null sha256, exit 0). This is the stage-1-only
    classification the spec's amendment reports "beside" the amended one; it does not by itself
    decide the follow-up.
    """
    _, classification = _stage1_hashes_and_classification(stage1_dir)
    return classification


def build_classification_record(stage1_dir: Path) -> dict:
    """Build the frozen `classification.json` record: the stage-1-only class plus its evidence."""
    stage1_dir = Path(stage1_dir)
    hashes_by_case, classification = _stage1_hashes_and_classification(stage1_dir)
    return {
        "classification": classification,
        "stage1SessionSha256": session.sha256(stage1_dir / "session.jsonl"),
        "rule": STAGE1_ONLY_RULE,
        "distinctHashes": {name: sorted(set(hashes)) for name, hashes in hashes_by_case.items()},
    }


# ------------------------------------------------------------------------------------------------
# Stage 2: leave-one-round-out comparison for non-repeatable cases
# ------------------------------------------------------------------------------------------------

def _validate_stage2_captures(records: list[dict]) -> None:
    """Refuse to score stage 2 if any capture (any build) failed: null sha256 or non-zero exit.

    A failed capture must never be silently treated as a null result by the leave-one-out or hash
    rules below (a crashed candidate capture would otherwise manufacture spurious power, and a
    crashed H capture could still let a repeatable case pass on the strict hash rule).
    """
    for rec in records:
        if rec.get("kind") != "capture":
            continue
        if rec.get("sha256") is None or rec.get("exit") != 0:
            raise ValueError(
                f"stage 2 capture round={rec.get('round')} build={rec.get('build')!r} "
                f"case={rec.get('case')!r} failed (sha256={rec.get('sha256')!r}, "
                f"exit={rec.get('exit')!r}); refusing to score a session containing a failed capture")


def leave_one_out(case_dir: Path, h_by_round: dict[int, str | None],
                   x_by_round: dict[int, str | None],
                   extent: tuple[int, int] = DEFAULT_EXTENT) -> list[dict]:
    """Leave-one-round-out comparison of x_by_round against h_by_round's other seven rounds.

    For each round r, compares x_by_round[r] against h_by_round[s] for every s != r under the
    `strict` profile. Identical hashes are a pass with zero difference without opening any file.
    Returns one row per round: round, pass (any of the seven comparisons passed), nearestRound
    (fewest differing pixels), its differingPixels/over8Pixels/maxChannelDelta, and whether
    x_by_round[r]'s hash occurred anywhere among h_by_round's values (informational only).
    """
    case_dir = Path(case_dir)
    all_h_hashes = {h for h in h_by_round.values() if h is not None}

    rows: list[dict] = []
    for r in range(DEFAULT_ROUNDS):
        x_hash = x_by_round.get(r)
        comparisons: list[dict] = []
        for s in range(DEFAULT_ROUNDS):
            if s == r:
                continue
            h_hash = h_by_round.get(s)
            if h_hash is None or x_hash is None:
                continue
            if h_hash == x_hash:
                comparisons.append({"round": s, "pass": True, "differingPixels": 0,
                                     "over8Pixels": 0, "maxChannelDelta": 0})
                continue
            parent_path = case_dir / f"{h_hash}.bmp"
            candidate_path = case_dir / f"{x_hash}.bmp"
            result = compare.image_difference(parent=parent_path, candidate=candidate_path,
                                               extent=extent, profile="strict")
            comparisons.append({"round": s, "pass": bool(result["pass"]),
                                 "differingPixels": result["differingPixels"],
                                 "over8Pixels": result["over8Pixels"],
                                 "maxChannelDelta": result["maxChannelDelta"]})

        if comparisons:
            nearest = min(comparisons, key=lambda c: c["differingPixels"])
            nearest_round = nearest["round"]
            differing_pixels = nearest["differingPixels"]
            over8_pixels = nearest["over8Pixels"]
            max_channel_delta = nearest["maxChannelDelta"]
        else:
            nearest_round = None
            differing_pixels = None
            over8_pixels = None
            max_channel_delta = None

        rows.append({
            "round": r,
            "pass": any(c["pass"] for c in comparisons),
            "nearestRound": nearest_round,
            "differingPixels": differing_pixels,
            "over8Pixels": over8_pixels,
            "maxChannelDelta": max_channel_delta,
            "hashSeenFromH": x_hash is not None and x_hash in all_h_hashes,
        })
    return rows


def _hashes_by_round(records: list[dict], build: str, case_name: str) -> dict[int, str | None]:
    by_round: dict[int, str | None] = {}
    for rec in records:
        if rec.get("kind") != "capture" or rec.get("build") != build or rec.get("case") != case_name:
            continue
        by_round[rec["round"]] = rec.get("sha256")
    return by_round


def _leave_one_out_case_result(name: str, records: list[dict], stage2_dir: Path,
                                extent: tuple[int, int]) -> dict:
    case_dir = stage2_dir / "images" / name
    h_by_round = _hashes_by_round(records, "H", name)
    a_by_round = _hashes_by_round(records, "A", name)
    b_by_round = _hashes_by_round(records, "B", name)

    null_rows = leave_one_out(case_dir, h_by_round, h_by_round, extent)
    a_rows = leave_one_out(case_dir, h_by_round, a_by_round, extent)
    b_rows = leave_one_out(case_dir, h_by_round, b_by_round, extent)

    def _worst(rows: list[dict]) -> dict:
        return max(rows, key=lambda row: (row["differingPixels"]
                                           if row["differingPixels"] is not None else -1))

    return {
        "nullHolds": all(row["pass"] for row in null_rows),
        "aPasses": all(row["pass"] for row in a_rows),
        "bPasses": all(row["pass"] for row in b_rows),
        "unmeasurable": not all(row["pass"] for row in null_rows),
        "nullPassCount": sum(1 for row in null_rows if row["pass"]),
        "aPassCount": sum(1 for row in a_rows if row["pass"]),
        "bPassCount": sum(1 for row in b_rows if row["pass"]),
        "aWorstNearest": _worst(a_rows),
        "rounds": {"null": null_rows, "A": a_rows, "B": b_rows},
    }


def _score(classification: dict[str, str], stage2_report: dict, case_row_cache: dict[str, dict]) -> dict:
    repeatable = sorted(n for n, s in classification.items() if s == "repeatable")
    non_repeatable = sorted(n for n, s in classification.items() if s == "non-repeatable")

    item3 = all(
        stage2_report["cases"].get(n, {}).get("candidates", {}).get("A", {}).get("unchanged", False)
        for n in repeatable)

    case_rows = {n: case_row_cache[n] for n in non_repeatable}
    any_unmeasurable = any(row["unmeasurable"] for row in case_rows.values())
    if non_repeatable:
        item4 = all(not row["unmeasurable"] and row["aPasses"] for row in case_rows.values())
        # Power is decided only from measurable cases: a case whose null control fails is
        # untrustworthy, so its B result must not manufacture (or hide) apparent power.
        measurable = {n: row for n, row in case_rows.items() if not row["unmeasurable"]}
        power = any(not row["bPasses"] for row in measurable.values())
        item5 = power
        non_discriminating = sorted(n for n, row in measurable.items() if row["bPasses"])
    else:
        item4 = True
        power = True
        item5 = True
        non_discriminating = []

    return {"repeatableCases": repeatable, "nonRepeatableCases": non_repeatable,
            "item3": item3, "item4": item4, "item5": item5, "power": power,
            "cases": case_rows, "anyUnmeasurable": any_unmeasurable,
            "nonDiscriminatingCases": non_discriminating}


def _verdict(item2: bool, item3: bool, item4: bool, item5: bool, any_unmeasurable: bool) -> str:
    """pass/fail/incomplete per the spec's decision rule (incomplete takes precedence over fail)."""
    if not item2 or any_unmeasurable:
        return "incomplete"
    if not (item3 and item4 and item5):
        return "fail"
    return "pass"


def evaluate_followup(stage1_dir: Path, stage2_dir: Path, frozen_record: dict,
                       extent: tuple[int, int] = DEFAULT_EXTENT) -> dict:
    """Score the follow-up's decision-rule items 2-5 from a frozen `classification.json` record
    and a stage 2 session.

    `frozen_record` is the dict shape `build_classification_record` produces: `classification`
    (the stage-1-only case -> "repeatable"/"non-repeatable" map), `stage1SessionSha256`, `rule` and
    `distinctHashes`. Both the classification map and the stage 1 session's SHA-256 are checked
    against a fresh read of `stage1_dir`, so a frozen record no longer describing that directory is
    refused. Every stage 2 capture (any build) must be a clean success (non-null sha256, exit 0);
    a session containing a failed capture is refused outright rather than scored. Item 1 (the build
    checks) is the controller's responsibility, not this function's; it is reported as "external".

    Items 3, 4 and 5 use the amended classification (non-repeatable when H's hashes vary in stage 1
    OR in stage 2); the same items are also reported, under the key "stage1OnlyResult", using the
    frozen stage-1-only classification alone, per the spec's amendment. The top-level `verdict`
    (and its `stage1OnlyResult` counterpart) is "incomplete" when item 2 fails or any non-repeatable
    case is unmeasurable, "fail" when complete but item 3, 4 or 5 does not hold, else "pass".
    """
    stage1_dir = Path(stage1_dir)
    stage2_dir = Path(stage2_dir)

    frozen_classification = frozen_record["classification"]
    live_classification = classify(stage1_dir)
    if live_classification != frozen_classification:
        raise ValueError(
            "stage 1's classification no longer matches the frozen classification; the follow-up "
            "requires the classification to be frozen before stage 2 starts")
    actual_stage1_sha256 = session.sha256(stage1_dir / "session.jsonl")
    if actual_stage1_sha256 != frozen_record.get("stage1SessionSha256"):
        raise ValueError(
            "stage 1's session.jsonl no longer matches the frozen classification.json's "
            "stage1SessionSha256; refusing to score against a changed stage 1 session")

    header, records = session._read_session(stage2_dir, truncate_torn=False)
    if header is None:
        raise ValueError(f"no session found under {stage2_dir}")
    _validate_stage2_captures(records)

    header_cases = set(header.get("cases", []))
    cases = [c for c in session.CASES if c.name in header_cases]
    coverage = session.collect_coverage(stage2_dir, records, cases)
    reference = json.loads(session.REFERENCE_PATH.read_text())
    stage2_report = session.evaluate(records, coverage, reference, cases, header)

    item2 = (stage2_report["sessionComplete"]
             and all(row["coverageOk"] for row in stage2_report["cases"].values())
             and all(stage2_report["coverageCells"].values()))

    # Amendment: non-repeatable when H's hashes vary in stage 1 OR in stage 2. Only H's own stage 2
    # captures can widen the stage-1-only classification; A and B never decide it.
    stage2_h_hash_sets: dict[str, set[str]] = {}
    for case in cases:
        h_by_round = _hashes_by_round(records, "H", case.name)
        stage2_h_hash_sets[case.name] = {h for h in h_by_round.values() if h is not None}

    amended_classification = {
        name: ("non-repeatable"
               if status == "non-repeatable" or len(stage2_h_hash_sets.get(name, set())) > 1
               else "repeatable")
        for name, status in frozen_classification.items()
    }

    # The amended non-repeatable set is always a superset of the stage-1-only one (widening can
    # only add cases), so computing leave-one-out once for it covers both scorings.
    all_non_repeatable = sorted(n for n, s in amended_classification.items() if s == "non-repeatable")
    case_row_cache = {name: _leave_one_out_case_result(name, records, stage2_dir, extent)
                       for name in all_non_repeatable}

    amended_score = _score(amended_classification, stage2_report, case_row_cache)
    stage1_only_score = _score(frozen_classification, stage2_report, case_row_cache)

    verdict = _verdict(item2, amended_score["item3"], amended_score["item4"], amended_score["item5"],
                        amended_score["anyUnmeasurable"])
    stage1_only_verdict = _verdict(item2, stage1_only_score["item3"], stage1_only_score["item4"],
                                    stage1_only_score["item5"], stage1_only_score["anyUnmeasurable"])

    return {
        "buildChecks": "external",
        "item2": item2,
        "item3": amended_score["item3"],
        "item4": amended_score["item4"],
        "item5": amended_score["item5"],
        "power": amended_score["power"],
        "verdict": verdict,
        "aPassesFollowup": verdict == "pass",
        "classification": {"amended": amended_classification, "stage1Only": frozen_classification},
        "repeatableCases": amended_score["repeatableCases"],
        "nonRepeatableCases": amended_score["nonRepeatableCases"],
        "nonDiscriminatingCases": amended_score["nonDiscriminatingCases"],
        "cases": amended_score["cases"],
        "stage1OnlyResult": {
            "item3": stage1_only_score["item3"],
            "item4": stage1_only_score["item4"],
            "item5": stage1_only_score["item5"],
            "power": stage1_only_score["power"],
            "verdict": stage1_only_verdict,
            "aPassesFollowup": stage1_only_verdict == "pass",
            "repeatableCases": stage1_only_score["repeatableCases"],
            "nonRepeatableCases": stage1_only_score["nonRepeatableCases"],
            "nonDiscriminatingCases": stage1_only_score["nonDiscriminatingCases"],
            "cases": stage1_only_score["cases"],
        },
        "stage2": stage2_report,
    }


def _write_evaluation(output: Path, result: dict) -> None:
    output.mkdir(parents=True)
    (output / "followup.json").write_text(json.dumps(result, indent=2) + "\n")

    amended_classification = result["classification"]["amended"]
    stage1_only_classification = result["classification"]["stage1Only"]

    lines = [
        "# R4.1 follow-up evaluation",
        "",
        "## Amended classification (primary verdict)",
        "",
        "Non-repeatable when H's hashes vary in stage 1 OR in stage 2 (amendment, 2026-09-25).",
        "",
        f"Item 1 (build checks): {result['buildChecks']}",
        f"Item 2 (stage 2 complete, every H capture covered, all eight cells covered): {result['item2']}",
        f"Item 3 (every repeatable case unchanged for A under the strict hash rule): {result['item3']}",
        f"Item 4 (every non-repeatable case holds its null control and A passes it): {result['item4']}",
        f"Item 5 (the rule shows power in at least one non-repeatable, measurable case): {result['item5']}",
        f"Power shown: {result['power']}",
        f"Verdict: {result['verdict']}",
        f"A passes the follow-up: {result['aPassesFollowup']}",
        f"Non-discriminating cases (non-repeatable, B passes): "
        f"{', '.join(result['nonDiscriminatingCases']) or 'none'}",
        "",
        "| Case | Amended class | Stage-1-only class | Unmeasurable | Null (of 8) | A (of 8) | "
        "B (of 8) | A worst nearest differing pixels |",
        "|---|---|---|---|---|---|---|---|",
    ]
    for case in session.CASES:
        if case.name not in amended_classification:
            continue
        amended_status = amended_classification[case.name]
        stage1_status = stage1_only_classification.get(case.name, "n/a")
        row = result["cases"].get(case.name)
        if row is None:
            unmeasurable = "n/a"
            null_s = a_s = b_s = "n/a"
            worst = "n/a"
        else:
            unmeasurable = row["unmeasurable"]
            null_s = f"{row['nullPassCount']}/8"
            a_s = f"{row['aPassCount']}/8"
            b_s = f"{row['bPassCount']}/8"
            worst = row["aWorstNearest"]["differingPixels"]
        lines.append(f"| {case.name} | {amended_status} | {stage1_status} | {unmeasurable} | "
                     f"{null_s} | {a_s} | {b_s} | {worst} |")

    stage1_only = result["stage1OnlyResult"]
    lines += [
        "",
        "## Stage-1-only classification (frozen, secondary; reported beside the amended result)",
        "",
        f"Item 3 (stage-1-only): {stage1_only['item3']}",
        f"Item 4 (stage-1-only): {stage1_only['item4']}",
        f"Item 5 (stage-1-only): {stage1_only['item5']}",
        f"Power shown (stage-1-only): {stage1_only['power']}",
        f"Verdict (stage-1-only): {stage1_only['verdict']}",
        f"A would pass the follow-up under the stage-1-only classification: "
        f"{stage1_only['aPassesFollowup']}",
        f"Non-discriminating cases (stage-1-only, non-repeatable, B passes): "
        f"{', '.join(stage1_only['nonDiscriminatingCases']) or 'none'}",
    ]
    (output / "followup.md").write_text("\n".join(lines) + "\n")


# ------------------------------------------------------------------------------------------------
# Selftests
# ------------------------------------------------------------------------------------------------

def _all_case_names() -> list[str]:
    return [c.name for c in session.CASES]


def _stage1_header(overrides: dict | None = None) -> dict:
    header = {"kind": "header", "builds": ["H"], "cases": _all_case_names(),
              "rounds": DEFAULT_ROUNDS, "buildInfo": {}}
    if overrides:
        header.update(overrides)
    return header


def _capture_row(round_: int, build: str, case: str, sha: str | None, exit_code: int = 0) -> dict:
    return {"kind": "capture", "round": round_, "build": build, "case": case,
            "sha256": sha, "exit": exit_code, "seconds": 1.0, "utc": "now"}


def _full_stage1_records(hash_overrides: dict[str, list[str | None]] | None = None) -> list[dict]:
    # A hash of "MISSING" for a round means no capture record was written for that round at all
    # (a missing round); None means a capture record was written with a null sha256 (a failure).
    hash_overrides = hash_overrides or {}
    records = []
    for case in session.CASES:
        hashes = hash_overrides.get(case.name, ["same"] * DEFAULT_ROUNDS)
        for r, h in enumerate(hashes):
            if h == "MISSING":
                continue
            records.append(_capture_row(r, "H", case.name, h))
    return records


def _write_session_file(path: Path, header: dict, records: list[dict]) -> None:
    lines = [json.dumps(header)] + [json.dumps(rec) for rec in records]
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n")


def _write_bmp(path: Path, size: tuple[int, int], color: tuple[int, int, int] = (10, 20, 30)) -> None:
    from PIL import Image
    Image.new("RGB", size, color).save(path, format="BMP")


def _write_bmp_with_marker(path: Path, size: tuple[int, int], marker_index: int,
                            base: tuple[int, int, int] = (10, 20, 30),
                            marker: tuple[int, int, int] = (250, 5, 5)) -> None:
    from PIL import Image
    image = Image.new("RGB", size, base)
    x, y = marker_index % size[0], marker_index // size[0]
    image.putpixel((x, y), marker)
    image.save(path, format="BMP")


class _TempDirMixin:
    """Allocates a fresh temp directory and guarantees its cleanup at test teardown (M3)."""

    def _mkdtemp(self) -> Path:
        path = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, path, ignore_errors=True)
        return path


class ClassifyTests(unittest.TestCase, _TempDirMixin):
    def test_repeatable_vs_non_repeatable(self):
        non_rep = session.CASES[0].name
        overrides = {non_rep: ["h0", "h1", "h0", "h0", "h0", "h0", "h0", "h0"]}
        stage1 = self._mkdtemp()
        _write_session_file(stage1 / "session.jsonl", _stage1_header(),
                             _full_stage1_records(overrides))
        result = classify(stage1)
        self.assertEqual(len(result), 29)
        self.assertEqual(result[non_rep], "non-repeatable")
        self.assertEqual(result[session.CASES[1].name], "repeatable")

    def test_rejects_null_capture(self):
        name = session.CASES[0].name
        overrides = {name: ["same", None, "same", "same", "same", "same", "same", "same"]}
        stage1 = self._mkdtemp()
        _write_session_file(stage1 / "session.jsonl", _stage1_header(),
                             _full_stage1_records(overrides))
        with self.assertRaises(ValueError):
            classify(stage1)

    def test_rejects_missing_round(self):
        name = session.CASES[0].name
        overrides = {name: ["same", "MISSING", "same", "same", "same", "same", "same", "same"]}
        stage1 = self._mkdtemp()
        _write_session_file(stage1 / "session.jsonl", _stage1_header(),
                             _full_stage1_records(overrides))
        with self.assertRaises(ValueError):
            classify(stage1)

    def test_rejects_wrong_header(self):
        records = _full_stage1_records()
        stage1 = self._mkdtemp()
        _write_session_file(stage1 / "session.jsonl",
                             _stage1_header({"builds": ["H", "A"]}), records)
        with self.assertRaises(ValueError):
            classify(stage1)

        stage1b = self._mkdtemp()
        _write_session_file(stage1b / "session.jsonl", _stage1_header({"rounds": 4}), records)
        with self.assertRaises(ValueError):
            classify(stage1b)

        stage1c = self._mkdtemp()
        _write_session_file(stage1c / "session.jsonl",
                             _stage1_header({"cases": _all_case_names()[:-1]}), records)
        with self.assertRaises(ValueError):
            classify(stage1c)

    def test_rejects_nonzero_exit(self):
        # M1: a capture with a non-null hash but a non-zero exit must still be refused.
        name = session.CASES[0].name
        records = _full_stage1_records()
        records = [rec if not (rec["case"] == name and rec["round"] == 0)
                   else _capture_row(0, "H", name, "same", exit_code=1)
                   for rec in records]
        stage1 = self._mkdtemp()
        _write_session_file(stage1 / "session.jsonl", _stage1_header(), records)
        with self.assertRaises(ValueError):
            classify(stage1)

    def test_rejects_duplicate_record_for_same_round_and_case(self):
        # M1: two records for the same (round, case) is an extra record, not a legitimate retry.
        name = session.CASES[0].name
        records = _full_stage1_records() + [_capture_row(0, "H", name, "same")]
        stage1 = self._mkdtemp()
        _write_session_file(stage1 / "session.jsonl", _stage1_header(), records)
        with self.assertRaises(ValueError):
            classify(stage1)

    def test_rejects_out_of_range_round(self):
        # M1: a record naming a round outside 0..7 is refused outright.
        name = session.CASES[0].name
        records = _full_stage1_records() + [_capture_row(8, "H", name, "extra")]
        stage1 = self._mkdtemp()
        _write_session_file(stage1 / "session.jsonl", _stage1_header(), records)
        with self.assertRaises(ValueError):
            classify(stage1)


class ClassificationRecordTests(unittest.TestCase, _TempDirMixin):
    def test_record_shape_and_stage1_sha256(self):
        non_rep = session.CASES[0].name
        overrides = {non_rep: ["h0", "h1", "h0", "h0", "h0", "h0", "h0", "h0"]}
        stage1 = self._mkdtemp()
        session_path = stage1 / "session.jsonl"
        _write_session_file(session_path, _stage1_header(), _full_stage1_records(overrides))
        record = build_classification_record(stage1)
        self.assertEqual(set(record), {"classification", "stage1SessionSha256", "rule",
                                        "distinctHashes"})
        self.assertEqual(record["classification"][non_rep], "non-repeatable")
        self.assertEqual(record["stage1SessionSha256"], session.sha256(session_path))
        self.assertIsInstance(record["rule"], str)
        self.assertEqual(record["distinctHashes"][non_rep], ["h0", "h1"])
        self.assertEqual(record["distinctHashes"][session.CASES[1].name], ["same"])


class LeaveOneOutTests(unittest.TestCase, _TempDirMixin):
    def test_excludes_same_round_and_passes_via_any_other_round(self):
        case_dir = self._mkdtemp()
        colors = [(r * 30 % 256, 10, 20) for r in range(8)]
        h_by_round = {}
        for r, color in enumerate(colors):
            h_by_round[r] = f"h{r}"
            _write_bmp(case_dir / f"h{r}.bmp", (100, 100), color)

        # Round 0's candidate reuses H round 0's own hash: since round 0 is excluded from its
        # own pool, this must NOT shortcut to a pass; every other round's H image differs
        # enough (a full-frame color change) to fail strict, so round 0 fails.
        x_by_round = dict(h_by_round)
        rows = leave_one_out(case_dir, h_by_round, x_by_round, extent=(100, 100))
        self.assertFalse(rows[0]["pass"])
        self.assertNotEqual(rows[0]["nearestRound"], 0)

        # Round 2's candidate matches H round 5's image byte-for-byte (a different hash label
        # pointing at identical content): it must pass because at least one of the seven
        # (any-of-seven) comparisons passes.
        x_by_round[2] = "special"
        _write_bmp(case_dir / "special.bmp", (100, 100), colors[5])
        rows = leave_one_out(case_dir, h_by_round, x_by_round, extent=(100, 100))
        self.assertTrue(rows[2]["pass"])
        self.assertEqual(rows[2]["nearestRound"], 5)
        self.assertEqual(rows[2]["differingPixels"], 0)

    def test_parent_candidate_argument_order(self):
        calls = []

        def fake_image_difference(parent, candidate, extent, profile="strict"):
            calls.append((parent, candidate, extent, profile))
            return {"pass": False, "differingPixels": 1, "over8Pixels": 0, "maxChannelDelta": 1}

        case_dir = Path("/nonexistent-case-dir")
        h_by_round = {r: f"h{r}" for r in range(8)}
        x_by_round = {r: f"x{r}" for r in range(8)}
        with mock.patch.object(compare, "image_difference", side_effect=fake_image_difference):
            leave_one_out(case_dir, h_by_round, x_by_round, extent=(8, 4))

        self.assertTrue(calls)
        for parent, candidate, extent, profile in calls:
            self.assertTrue(Path(parent).name.startswith("h"))
            self.assertTrue(Path(candidate).name.startswith("x"))
            self.assertEqual(extent, (8, 4))
            self.assertEqual(profile, "strict")

    def test_identical_hash_shortcut_avoids_file_io(self):
        case_dir = Path("/nonexistent-case-dir/does/not/exist")
        h_by_round = {r: "same" for r in range(8)}
        x_by_round = {r: "same" for r in range(8)}
        rows = leave_one_out(case_dir, h_by_round, x_by_round, extent=(8, 4))
        self.assertTrue(all(row["pass"] for row in rows))
        self.assertTrue(all(row["differingPixels"] == 0 for row in rows))
        self.assertTrue(all(row["hashSeenFromH"] for row in rows))


class StageTwoCaptureValidationTests(unittest.TestCase, _TempDirMixin):
    """C1: a failed stage 2 capture (any build) must refuse scoring, not silently score as pass."""

    def _frozen_with_one_non_repeatable(self, non_rep: str) -> dict:
        overrides = {non_rep: ["h0", "h1", "h0", "h0", "h0", "h0", "h0", "h0"]}
        stage1 = self._mkdtemp()
        _write_session_file(stage1 / "session.jsonl", _stage1_header(),
                             _full_stage1_records(overrides))
        record = build_classification_record(stage1)
        record["_stage1_dir"] = stage1  # test-only convenience, not part of the real file shape
        return record

    def test_crashed_b_capture_raises_instead_of_manufacturing_power(self):
        non_rep = session.CASES[1].name
        frozen_record = self._frozen_with_one_non_repeatable(non_rep)
        stage1 = frozen_record.pop("_stage1_dir")

        stage2 = self._mkdtemp()
        case_dir = stage2 / "images" / non_rep
        case_dir.mkdir(parents=True)
        h_hashes = [f"h{r}" for r in range(8)]
        for r, h in enumerate(h_hashes):
            _write_bmp_with_marker(case_dir / f"{h}.bmp", (100, 100), marker_index=r)

        records = []
        for r in range(8):
            records.append(_capture_row(r, "H", non_rep, h_hashes[r]))
            records.append(_capture_row(r, "A", non_rep, h_hashes[r]))
            if r == 3:
                records.append(_capture_row(r, "B", non_rep, None, exit_code=1))  # crashed
            else:
                records.append(_capture_row(r, "B", non_rep, h_hashes[r]))
        header = {"kind": "header", "builds": ["A", "B", "H"], "cases": [non_rep],
                  "rounds": 8, "buildInfo": {}}
        _write_session_file(stage2 / "session.jsonl", header, records)

        with self.assertRaises(ValueError):
            evaluate_followup(stage1, stage2, frozen_record, extent=(100, 100))

    def test_crashed_h_capture_in_repeatable_case_raises_instead_of_passing(self):
        repeatable_case = session.CASES[5].name
        stage1 = self._mkdtemp()
        _write_session_file(stage1 / "session.jsonl", _stage1_header(),
                             _full_stage1_records())
        frozen_record = build_classification_record(stage1)
        self.assertEqual(frozen_record["classification"][repeatable_case], "repeatable")

        stage2 = self._mkdtemp()
        records = []
        for r in range(8):
            if r == 2:
                records.append(_capture_row(r, "H", repeatable_case, None, exit_code=1))  # crashed
            else:
                records.append(_capture_row(r, "H", repeatable_case, "same"))
            records.append(_capture_row(r, "A", repeatable_case, "same"))
            records.append(_capture_row(r, "B", repeatable_case, "same"))
        header = {"kind": "header", "builds": ["A", "B", "H"], "cases": [repeatable_case],
                  "rounds": 8, "buildInfo": {}}
        _write_session_file(stage2 / "session.jsonl", header, records)

        with self.assertRaises(ValueError):
            evaluate_followup(stage1, stage2, frozen_record)


class EvaluateFollowupTests(unittest.TestCase, _TempDirMixin):
    def _frozen_with_one_non_repeatable(self, non_rep: str) -> tuple[Path, dict]:
        overrides = {non_rep: ["h0", "h1", "h0", "h0", "h0", "h0", "h0", "h0"]}
        stage1 = self._mkdtemp()
        _write_session_file(stage1 / "session.jsonl", _stage1_header(),
                             _full_stage1_records(overrides))
        frozen_record = build_classification_record(stage1)
        return stage1, frozen_record

    def test_unmeasurable_when_null_control_fails(self):
        non_rep = session.CASES[0].name
        stage1, frozen_record = self._frozen_with_one_non_repeatable(non_rep)
        self.assertEqual(frozen_record["classification"][non_rep], "non-repeatable")

        stage2 = self._mkdtemp()
        case_dir = stage2 / "images" / non_rep
        case_dir.mkdir(parents=True)
        # Eight pairwise drastically different H images: no round's null control can find a
        # passing partner among the other seven, so the null control fails everywhere.
        colors = [(0, 0, 0), (255, 0, 0), (0, 255, 0), (0, 0, 255),
                  (128, 128, 128), (64, 64, 64), (200, 100, 50), (10, 200, 10)]
        h_hashes = [f"h{r}" for r in range(8)]
        for r, color in enumerate(colors):
            _write_bmp(case_dir / f"{h_hashes[r]}.bmp", (100, 100), color)

        records = []
        for r in range(8):
            records.append(_capture_row(r, "H", non_rep, h_hashes[r]))
            records.append(_capture_row(r, "A", non_rep, h_hashes[r]))
            records.append(_capture_row(r, "B", non_rep, h_hashes[r]))
        header = {"kind": "header", "builds": ["A", "B", "H"], "cases": [non_rep],
                  "rounds": 8, "buildInfo": {}}
        _write_session_file(stage2 / "session.jsonl", header, records)

        result = evaluate_followup(stage1, stage2, frozen_record, extent=(100, 100))
        self.assertTrue(result["cases"][non_rep]["unmeasurable"])
        self.assertFalse(result["cases"][non_rep]["nullHolds"])
        self.assertFalse(result["item4"])
        self.assertFalse(result["aPassesFollowup"])
        self.assertEqual(result["verdict"], "incomplete")

    def test_power_false_when_b_passes_every_case(self):
        non_rep = session.CASES[1].name
        stage1, frozen_record = self._frozen_with_one_non_repeatable(non_rep)
        self.assertEqual(frozen_record["classification"][non_rep], "non-repeatable")

        stage2 = self._mkdtemp()
        case_dir = stage2 / "images" / non_rep
        case_dir.mkdir(parents=True)
        # Each H round differs from every other H round by exactly one marked pixel: any pair
        # passes strict (2 differing pixels of 10000), so the null control holds everywhere.
        h_hashes = [f"h{r}" for r in range(8)]
        for r, h in enumerate(h_hashes):
            _write_bmp_with_marker(case_dir / f"{h}.bmp", (100, 100), marker_index=r)

        records = []
        for r in range(8):
            records.append(_capture_row(r, "H", non_rep, h_hashes[r]))
            records.append(_capture_row(r, "A", non_rep, h_hashes[r]))
            # B reuses H's own hashes: it always finds a passing partner among the other rounds.
            records.append(_capture_row(r, "B", non_rep, h_hashes[r]))
        header = {"kind": "header", "builds": ["A", "B", "H"], "cases": [non_rep],
                  "rounds": 8, "buildInfo": {}}
        _write_session_file(stage2 / "session.jsonl", header, records)

        result = evaluate_followup(stage1, stage2, frozen_record, extent=(100, 100))
        self.assertTrue(result["cases"][non_rep]["nullHolds"])
        self.assertTrue(result["cases"][non_rep]["bPasses"])
        self.assertTrue(result["cases"][non_rep]["aPasses"])
        self.assertFalse(result["power"])
        self.assertFalse(result["item5"])
        self.assertFalse(result["aPassesFollowup"])
        self.assertEqual(result["nonDiscriminatingCases"], [non_rep])  # S1

    def test_unmeasurable_is_never_a_pass(self):
        # I3: null control fails at exactly one round while A passes all 8. Without the
        # "not unmeasurable" guard on item4, this would incorrectly read as a pass.
        non_rep = session.CASES[2].name
        stage1, frozen_record = self._frozen_with_one_non_repeatable(non_rep)

        stage2 = self._mkdtemp()
        case_dir = stage2 / "images" / non_rep
        case_dir.mkdir(parents=True)
        _write_bmp(case_dir / "same.bmp", (100, 100), (10, 20, 30))
        _write_bmp(case_dir / "odd.bmp", (100, 100), (250, 5, 5))
        # Verify the fixture actually fails strict, so the test exercises a real null-control
        # failure rather than an accidental pass.
        self.assertFalse(compare.image_difference(case_dir / "same.bmp", case_dir / "odd.bmp",
                                                    (100, 100), "strict")["pass"])

        h_hashes = ["odd" if r == 0 else "same" for r in range(8)]
        records = []
        for r in range(8):
            records.append(_capture_row(r, "H", non_rep, h_hashes[r]))
            records.append(_capture_row(r, "A", non_rep, "same"))
            records.append(_capture_row(r, "B", non_rep, "same"))
        header = {"kind": "header", "builds": ["A", "B", "H"], "cases": [non_rep],
                  "rounds": 8, "buildInfo": {}}
        _write_session_file(stage2 / "session.jsonl", header, records)

        result = evaluate_followup(stage1, stage2, frozen_record, extent=(100, 100))
        row = result["cases"][non_rep]
        self.assertFalse(row["nullHolds"])
        self.assertTrue(row["unmeasurable"])
        self.assertTrue(row["aPasses"])  # A passes all 8 rounds despite the case being unmeasurable
        self.assertFalse(result["item4"])  # the guard must still fail item4
        self.assertEqual(result["verdict"], "incomplete")

    def test_power_counts_only_measurable_cases(self):
        # M2: an unmeasurable case's B failure must not manufacture power, and a measurable case
        # where B passes must still report no power, even though the unmeasurable case's B result
        # looks like a failure.
        case_unmeasurable = session.CASES[3].name
        case_measurable = session.CASES[4].name
        overrides = {
            case_unmeasurable: ["h0", "h1", "h0", "h0", "h0", "h0", "h0", "h0"],
            case_measurable: ["m0", "m1", "m0", "m0", "m0", "m0", "m0", "m0"],
        }
        stage1 = self._mkdtemp()
        _write_session_file(stage1 / "session.jsonl", _stage1_header(),
                             _full_stage1_records(overrides))
        frozen_record = build_classification_record(stage1)

        stage2 = self._mkdtemp()
        case_dir_u = stage2 / "images" / case_unmeasurable
        case_dir_u.mkdir(parents=True)
        colors = [(0, 0, 0), (255, 0, 0), (0, 255, 0), (0, 0, 255),
                  (128, 128, 128), (64, 64, 64), (200, 100, 50), (10, 200, 10)]
        h_hashes_u = [f"hu{r}" for r in range(8)]
        for r, color in enumerate(colors):
            _write_bmp(case_dir_u / f"{h_hashes_u[r]}.bmp", (100, 100), color)

        case_dir_m = stage2 / "images" / case_measurable
        case_dir_m.mkdir(parents=True)
        h_hashes_m = [f"hm{r}" for r in range(8)]
        for r, h in enumerate(h_hashes_m):
            _write_bmp_with_marker(case_dir_m / f"{h}.bmp", (100, 100), marker_index=r)

        records = []
        for r in range(8):
            records.append(_capture_row(r, "H", case_unmeasurable, h_hashes_u[r]))
            records.append(_capture_row(r, "A", case_unmeasurable, h_hashes_u[r]))
            records.append(_capture_row(r, "B", case_unmeasurable, h_hashes_u[r]))
            records.append(_capture_row(r, "H", case_measurable, h_hashes_m[r]))
            records.append(_capture_row(r, "A", case_measurable, h_hashes_m[r]))
            records.append(_capture_row(r, "B", case_measurable, h_hashes_m[r]))
        header = {"kind": "header", "builds": ["A", "B", "H"],
                  "cases": [case_unmeasurable, case_measurable], "rounds": 8, "buildInfo": {}}
        _write_session_file(stage2 / "session.jsonl", header, records)

        result = evaluate_followup(stage1, stage2, frozen_record, extent=(100, 100))
        self.assertTrue(result["cases"][case_unmeasurable]["unmeasurable"])
        self.assertFalse(result["cases"][case_unmeasurable]["bPasses"])
        self.assertTrue(result["cases"][case_measurable]["nullHolds"])
        self.assertTrue(result["cases"][case_measurable]["bPasses"])
        self.assertFalse(result["power"])
        self.assertEqual(result["nonDiscriminatingCases"], [case_measurable])

    def test_classification_mismatch_raises(self):
        stage1 = self._mkdtemp()
        _write_session_file(stage1 / "session.jsonl", _stage1_header(),
                             _full_stage1_records())
        frozen_record = build_classification_record(stage1)
        wrong_record = json.loads(json.dumps(frozen_record))
        first = session.CASES[0].name
        wrong_record["classification"][first] = (
            "non-repeatable" if wrong_record["classification"][first] == "repeatable" else "repeatable")
        # I2: a nonexistent stage 2 directory would also raise ValueError ("no session found"),
        # which would let this test pass even if the classification check were disabled. Pin the
        # exact message so only that check can satisfy it.
        with self.assertRaisesRegex(ValueError, "no longer matches the frozen classification"):
            evaluate_followup(stage1, self._mkdtemp() / "stage2-unused", wrong_record)

    def test_stage1_session_hash_mismatch_raises(self):
        stage1 = self._mkdtemp()
        session_path = stage1 / "session.jsonl"
        _write_session_file(session_path, _stage1_header(), _full_stage1_records())
        frozen_record = build_classification_record(stage1)
        # Append a stray trailing newline: session.jsonl still parses to the same
        # classification, but its bytes (and so its SHA-256) no longer match the frozen record.
        with session_path.open("a") as stream:
            stream.write("\n")
        self.assertEqual(classify(stage1), frozen_record["classification"])
        # I2: pin the exact message, for the same reason as test_classification_mismatch_raises.
        with self.assertRaisesRegex(ValueError, "stage1SessionSha256"):
            evaluate_followup(stage1, self._mkdtemp() / "stage2-unused", frozen_record)

    def test_amended_classification_reclassifies_on_stage2_h_variation(self):
        # A case that repeated in stage 1 but whose H captures vary in stage 2 must be scored as
        # non-repeatable under the amended classification, while the stage-1-only block still
        # reports it as repeatable (the amendment's own san-miguel-manual-taa-1 / auto-taa-1 case).
        case_x = session.CASES[2].name
        stage1 = self._mkdtemp()
        _write_session_file(stage1 / "session.jsonl", _stage1_header(),
                             _full_stage1_records())
        frozen_record = build_classification_record(stage1)
        self.assertEqual(frozen_record["classification"][case_x], "repeatable")

        stage2 = self._mkdtemp()
        case_dir = stage2 / "images" / case_x
        case_dir.mkdir(parents=True)
        _write_bmp(case_dir / "hv1.bmp", (100, 100), (10, 20, 30))
        _write_bmp(case_dir / "hv2.bmp", (100, 100), (200, 50, 80))

        records = []
        for r in range(8):
            h_hash = "hv1" if r < 4 else "hv2"
            records.append(_capture_row(r, "H", case_x, h_hash))
            records.append(_capture_row(r, "A", case_x, "hv1"))
            records.append(_capture_row(r, "B", case_x, "hv1"))
        header = {"kind": "header", "builds": ["A", "B", "H"], "cases": [case_x],
                  "rounds": 8, "buildInfo": {}}
        _write_session_file(stage2 / "session.jsonl", header, records)

        result = evaluate_followup(stage1, stage2, frozen_record, extent=(100, 100))
        self.assertEqual(result["classification"]["stage1Only"][case_x], "repeatable")
        self.assertEqual(result["classification"]["amended"][case_x], "non-repeatable")
        self.assertIn(case_x, result["nonRepeatableCases"])
        self.assertIn(case_x, result["stage1OnlyResult"]["repeatableCases"])
        self.assertNotIn(case_x, result["stage1OnlyResult"]["nonRepeatableCases"])
        # The amended, leave-one-out score for case_x must actually be computed (not skipped).
        self.assertIn(case_x, result["cases"])


class CliTests(unittest.TestCase, _TempDirMixin):
    def test_classify_cli_writes_record_json(self):
        root = self._mkdtemp()
        stage1 = root / "stage1"
        _write_session_file(stage1 / "session.jsonl", _stage1_header(),
                             _full_stage1_records())
        output = root / "classification.json"
        argv = ["followup_eval.py", "--classify", str(stage1), "--output", str(output)]
        with mock.patch.object(sys, "argv", argv):
            rc = main()
        self.assertEqual(rc, 0)
        written = json.loads(output.read_text())
        self.assertEqual(len(written["classification"]), 29)
        self.assertEqual(written["stage1SessionSha256"], session.sha256(stage1 / "session.jsonl"))

    def test_evaluate_cli_refuses_existing_output_dir(self):
        root = self._mkdtemp()
        stage1 = root / "stage1"
        stage2 = root / "stage2"
        classification = root / "classification.json"
        _write_session_file(stage1 / "session.jsonl", _stage1_header(),
                             _full_stage1_records())
        frozen_record = build_classification_record(stage1)
        classification.write_text(json.dumps(frozen_record))
        header = {"kind": "header", "builds": ["A", "B", "H"], "cases": [], "rounds": 8,
                  "buildInfo": {}}
        _write_session_file(stage2 / "session.jsonl", header, [])
        output = root / "output"
        output.mkdir()  # Pre-existing: the CLI must refuse to write into it.
        argv = ["followup_eval.py", "--evaluate", "--stage1", str(stage1),
                "--stage2", str(stage2), "--classification", str(classification),
                "--output", str(output)]
        with mock.patch.object(sys, "argv", argv):
            with self.assertRaises(SystemExit):
                main()


_TEST_CASES = (ClassifyTests, ClassificationRecordTests, LeaveOneOutTests,
               StageTwoCaptureValidationTests, EvaluateFollowupTests, CliTests)


def _run_selftests() -> int:
    suite = unittest.TestSuite()
    loader = unittest.defaultTestLoader
    for case in _TEST_CASES:
        suite.addTests(loader.loadTestsFromTestCase(case))
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    return 0 if result.wasSuccessful() else 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--classify", type=Path, metavar="STAGE1")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--evaluate", action="store_true")
    parser.add_argument("--stage1", type=Path)
    parser.add_argument("--stage2", type=Path)
    parser.add_argument("--classification", type=Path)
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()

    if args.selftest:
        return _run_selftests()

    if args.classify is not None:
        if args.output is None:
            parser.error("--classify requires --output")
        record = build_classification_record(args.classify)
        args.output.write_text(json.dumps(record, indent=2, sort_keys=True) + "\n")
        print(json.dumps(record, indent=2, sort_keys=True))
        return 0

    if args.evaluate:
        if None in (args.stage1, args.stage2, args.classification, args.output):
            parser.error("--evaluate requires --stage1, --stage2, --classification and --output")
        if args.output.exists():
            parser.error(f"{args.output} already exists; refusing to overwrite retained evidence")
        frozen_record = json.loads(args.classification.read_text())
        result = evaluate_followup(args.stage1, args.stage2, frozen_record)
        _write_evaluation(args.output, result)
        print(json.dumps({"item2": result["item2"], "item3": result["item3"],
                           "item4": result["item4"], "item5": result["item5"],
                           "power": result["power"], "verdict": result["verdict"],
                           "aPassesFollowup": result["aPassesFollowup"],
                           "stage1OnlyVerdict": result["stage1OnlyResult"]["verdict"]},
                          indent=2))
        return 0

    parser.error("one of --selftest, --classify or --evaluate is required")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
