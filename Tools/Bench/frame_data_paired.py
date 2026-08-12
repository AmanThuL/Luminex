#!/usr/bin/env python3
"""Paired collection driver for FrameDataBench.

Runs the frozen paired-repetition protocol (spec section 11) for one or more of the five frozen
frame-data workloads by launching two fresh `FrameDataBench` processes back to back per repetition
-- one from a baseline executable, one from a candidate executable -- alternating AB/BA order every
repetition. Each process runs `--case <name> --out <path> --verify --warmup 16 --frames 256`
(defaults; both overridable). "One repetition = one fresh process per side" mirrors M5.1's own
protocol (docs/research/2026-08-12-execution-model-evidence.md), except baseline and candidate here
are two separate executables -- built from different points on the M5.2 branch -- rather than two
adapters inside one process, so they cannot share a process the way M5.1's NoApiBench did.

Before accepting any repetition's timing, both sides' JSON is checked for two things: that `case`/
`warmupFrames`/`measuredFrames` agree (an unintentionally mismatched invocation is not a timing
comparison at all) and that the `--verify` digests agree (spec: "the driver also compares the
--verify digests between sides and refuses to report timing for a workload whose outputs differ").
Either mismatch refuses to report timing for that workload for the whole collection. A side that
exits non-zero for any reason is not a driver bug: that repetition is skipped and recorded, and a
workload with no successful paired repetitions is reported as not comparable rather than crashing
the collection or fabricating a confidence interval from no data.

The decision statistic mirrors M5.1's frozen fixed-resample method (same paired-percentage-delta
bootstrap, same resample count, same seed) but is reimplemented here rather than imported from the
NoApi evidence frozen at tag `m5.1-noapi-evidence`.

Stdlib only (Tools/*.py convention; no third-party JSON/stat libraries).

Usage:
    python3 frame_data_paired.py --baseline <path/to/FrameDataBench> \\
        --candidate <path/to/FrameDataBench> [--out DIR] [--repetitions 12] \\
        [--workloads F-FIT-512,F-DYNAMIC-1024,...] [--warmup 16] [--frames 256]

    python3 frame_data_paired.py --selftest
        Checks this script's own pure logic (the acceptance-threshold sign convention analyze_pairs
        applies) against synthetic data and exits 0 iff every check passed; launches no subprocess.

Exit codes: 0 the collection completed, and a frozen scored collection obtained every requested
pair for every workload. 1 a configuration error, an output failure, or an incomplete/non-comparable
frozen collection. An explicitly unscored trial still records incomplete workloads as data and
returns 0.
"""
import argparse
import json
import os
import random
import statistics
import subprocess
import sys
from pathlib import Path

SEED = 0x4C4D5835
RESAMPLES = 10_000
CONFIDENCE = 0.95
DEFAULT_REPETITIONS = 12
DEFAULT_WARMUP = 16
DEFAULT_FRAMES = 256
# Spec section 11's two acceptance thresholds: a dynamic case's material win, and the upper
# regression bound F-FIT-512/both static cases must stay inside. Frozen with the spec, not a
# tunable of this driver.
MATERIAL_WIN_THRESHOLD_PCT = 25.0
REGRESSION_BOUND_PCT = 5.0
DEFAULT_WORKLOADS = [
    "F-FIT-512",
    "F-DYNAMIC-1024",
    "F-DYNAMIC-4096",
    "F-STATIC-1024",
    "F-STATIC-4096",
]
SIDES = ("baseline", "candidate")  # paired_bootstrap.py's convention: [incumbent, prototype].

_REPO_ROOT = Path(__file__).resolve().parents[2]


def _default_out() -> Path:
    # Outside the repo tree by construction, matching the archived M5.1 collector's convention
    # (docs/conventions/documentation.md: raw captures stay outside the published tree).
    return _REPO_ROOT.parent / "lmx-m5.2-frame-data-measurements"


def run_bench(binary: Path, case: str, warmup: int, frames: int, out_path: Path,
             env: dict[str, str]) -> subprocess.CompletedProcess:
    """Runs one `FrameDataBench --case <case> --verify` invocation and returns its process result.

    FrameDataBench resolves shaders relative to its own working directory, so it is launched with
    its own directory as cwd (AGENTS.md gotcha, restated for this benchmark). A non-zero exit is
    reported to the caller rather than raised: the incumbent RHI's fixed-capacity per-frame uniform
    ring is expected to abort some workloads on the pre-migration baseline, and that is data the
    collection records, not a reason to stop.
    """
    cmd = [str(binary), "--case", case, "--out", str(out_path), "--verify",
          "--warmup", str(warmup), "--frames", str(frames)]
    return subprocess.run(cmd, cwd=binary.parent, env=env, capture_output=True, text=True)


def paired_deltas_pct(pairs: list[list[float]]) -> list[float]:
    """Percentage delta of each pair relative to the baseline (positive = candidate cheaper)."""
    deltas = []
    for baseline, candidate in pairs:
        if baseline == 0:
            raise ValueError("baseline median of zero cannot anchor a percentage delta")
        deltas.append((baseline - candidate) / baseline * 100.0)
    return deltas


def bootstrap_ci(deltas: list[float], rng: random.Random) -> tuple[float, float]:
    """Two-sided 95% percentile bootstrap interval over the median of the paired deltas.

    Same fixed-resample method M5.1 froze (10,000 resamples, seed 0x4C4D5835): resample the paired
    deltas with replacement, take each resample's median, then read off the 2.5th/97.5th percentile
    of the resampled medians.
    """
    medians = []
    n = len(deltas)
    for _ in range(RESAMPLES):
        sample = [deltas[rng.randrange(n)] for _ in range(n)]
        medians.append(statistics.median(sample))
    medians.sort()
    lo_index = int((1.0 - CONFIDENCE) / 2.0 * RESAMPLES)
    hi_index = RESAMPLES - 1 - lo_index
    return medians[lo_index], medians[hi_index]


def analyze_pairs(pairs: list[list[float]]) -> dict:
    deltas = paired_deltas_pct(pairs)
    rng = random.Random(SEED)
    lo, hi = bootstrap_ci(deltas, rng)
    median_delta = statistics.median(deltas)
    excludes_zero = (lo > 0.0 and hi > 0.0) or (lo < 0.0 and hi < 0.0)
    # Spec section 11's own two acceptance thresholds, computed here so a reader never has to apply
    # the rule by hand against the raw numbers. Both read off this function's delta convention
    # (paired_deltas_pct: (baseline - candidate) / baseline * 100, positive = candidate cheaper), so
    # a regression -- the candidate being slower -- is a NEGATIVE delta, and the "upper" bound on
    # how much slower the candidate is allowed to be is therefore the CI's LOWER numeric bound, not
    # its upper one.
    #   - materialWin: a dynamic case needs >= 25% candidate win with the interval excluding zero.
    #   - withinRegressionBound: spec section 11's "candidate's upper 95% confidence bound is no
    #     more than +5%" is a statement about the regression tail's magnitude, which in this sign
    #     convention is `lo` (the most-negative, i.e. most-regressed, end of the interval) -- so the
    #     bound holds iff `lo >= -REGRESSION_BOUND_PCT`. F-FIT-512 and both static cases need this
    #     one; which field is the relevant gate for a given workload is a per-workload judgment this
    #     driver does not make.
    material_win = median_delta >= MATERIAL_WIN_THRESHOLD_PCT and excludes_zero
    within_regression_bound = lo >= -REGRESSION_BOUND_PCT
    return {
        "pairCount": len(pairs),
        "pairCountMatchesSpec": len(pairs) == DEFAULT_REPETITIONS,
        "pairedDeltasPct": [round(d, 4) for d in deltas],
        "medianDeltaPct": round(median_delta, 4),
        "ci95Pct": [round(lo, 4), round(hi, 4)],
        "ciExcludesZero": excludes_zero,
        "materialWin": material_win,
        "withinRegressionBound": within_regression_bound,
        "direction": "candidateFaster" if median_delta > 0 else (
            "candidateSlower" if median_delta < 0 else "tie"),
    }


def has_complete_pairs(pair_count: int, requested_repetitions: int) -> bool:
    """True only when the workload produced every pair the requested protocol called for."""
    return pair_count == requested_repetitions


def collection_exit_code(frozen_protocol: bool, comparable: int, requested: int) -> int:
    """Fail a scored collection unless every requested workload is comparable."""
    return 1 if frozen_protocol and comparable != requested else 0


def _pairs_for_deltas(deltas_pct: list[float], baseline: float = 1_000_000.0) -> list[list[float]]:
    """Synthetic [baseline, candidate] pairs whose paired_deltas_pct is exactly `deltas_pct`."""
    return [[baseline, baseline * (1.0 - delta / 100.0)] for delta in deltas_pct]


def run_selftest() -> bool:
    """Synthetic checks of analyze_pairs' sign convention -- exactly the class of bug a silent sign
    inversion in `within_regression_bound` was (a uniformly regressed candidate scored as passing,
    a uniformly much-faster one scored as failing). No FrameDataBench binary is touched.

    Cases: a uniformly 8%-slower candidate must fail the regression bound and not register as a
    material win; a uniformly 40%-faster one must pass the bound and register as a material win;
    a small spread straddling zero (no real difference) must still pass the bound.
    """
    failures: list[str] = []

    def check(condition: bool, what: str) -> None:
        if not condition:
            failures.append(what)

    regressed = analyze_pairs(_pairs_for_deltas([-8.0] * 12))
    check(regressed["withinRegressionBound"] is False,
         "uniform -8% delta: withinRegressionBound should be False, got "
         f"{regressed['withinRegressionBound']} (ci95Pct={regressed['ci95Pct']})")
    check(regressed["materialWin"] is False,
         f"uniform -8% delta: materialWin should be False, got {regressed['materialWin']}")

    faster = analyze_pairs(_pairs_for_deltas([40.0] * 12))
    check(faster["withinRegressionBound"] is True,
         "uniform +40% delta: withinRegressionBound should be True, got "
         f"{faster['withinRegressionBound']} (ci95Pct={faster['ci95Pct']})")
    check(faster["materialWin"] is True,
         f"uniform +40% delta: materialWin should be True, got {faster['materialWin']}")

    straddling = analyze_pairs(
        _pairs_for_deltas([3.0, -3.0, 2.0, -2.0, 1.0, -1.0, 0.0, 4.0, -4.0, 2.0, -2.0, 0.0]))
    check(straddling["withinRegressionBound"] is True,
         "straddling small CI: withinRegressionBound should be True, got "
         f"{straddling['withinRegressionBound']} (ci95Pct={straddling['ci95Pct']})")
    check(straddling["ciExcludesZero"] is False,
         f"straddling small CI: ciExcludesZero should be False, got {straddling['ciExcludesZero']}")
    check(has_complete_pairs(12, 12), "12/12 pairs should be complete")
    check(not has_complete_pairs(11, 12), "11/12 pairs must not be complete")
    check(collection_exit_code(True, 4, 5) == 1,
          "a frozen collection with 4/5 comparable workloads must fail")
    check(collection_exit_code(False, 4, 5) == 0,
          "an unscored trial may preserve incomplete workloads as data")

    if failures:
        print(f"frame_data_paired.py --selftest: {len(failures)} check(s) failed:", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return False
    print("frame_data_paired.py --selftest: all checks passed")
    return True


def collect_workload(baseline: Path, candidate: Path, case: str, repetitions: int, warmup: int,
                     frames: int, raw_dir: Path, env: dict[str, str]) -> dict:
    """Runs `repetitions` paired repetitions of `case`, returning a summary dict.

    Every repetition compares both sides' `--verify` digest before its median pair is accepted; the
    first digest mismatch marks the whole workload not comparable (spec: refuse to report timing
    for a workload whose outputs differ). A repetition whose side fails to run or to parse is
    skipped and recorded under `failures` rather than raised.
    """
    pairs: list[list[float]] = []
    failures: list[dict] = []
    digest_mismatch: dict | None = None
    config_mismatch: dict | None = None

    for repetition in range(repetitions):
        order = SIDES if repetition % 2 == 0 else tuple(reversed(SIDES))
        json_paths = {side: raw_dir / f"{case}.{side}.rep{repetition:02d}.json" for side in SIDES}
        side_results: dict[str, dict] = {}
        for side in order:
            binary = baseline if side == "baseline" else candidate
            proc = run_bench(binary, case, warmup, frames, json_paths[side], env)
            # spdlog's default sink is stdout, not stderr, so a fatal LMX_ASSERT's diagnostic (for
            # example the incumbent per-frame uniform ring's fixed-capacity abort) lands there; fall
            # back to stdout's last line only when stderr has nothing, rather than assuming either.
            diagnostic = proc.stderr.strip() or proc.stdout.strip()
            record = {"returncode": proc.returncode,
                     "diagnosticTail": diagnostic.splitlines()[-1] if diagnostic else ""}
            if proc.returncode == 0:
                try:
                    record["json"] = json.loads(json_paths[side].read_text())
                except (OSError, json.JSONDecodeError) as exc:
                    record["json"] = None
                    record["parseError"] = str(exc)
            else:
                record["json"] = None
            side_results[side] = record

        if side_results["baseline"]["json"] is None or side_results["candidate"]["json"] is None:
            failures.append({
                "repetition": repetition,
                "order": list(order),
                "baseline": {k: v for k, v in side_results["baseline"].items() if k != "json"},
                "candidate": {k: v for k, v in side_results["candidate"].items() if k != "json"},
            })
            continue

        baseline_json = side_results["baseline"]["json"]
        candidate_json = side_results["candidate"]["json"]
        # A baseline/candidate pair run under different case/warmup/frame configuration is not a
        # timing comparison at all -- catch a misconfigured invocation (e.g. mismatched --warmup
        # between two independently-launched binaries) before it silently pairs incomparable
        # medians.
        mismatched_fields = [field for field in ("case", "warmupFrames", "measuredFrames")
                            if baseline_json.get(field) != candidate_json.get(field)]
        if mismatched_fields:
            config_mismatch = {
                "repetition": repetition,
                "mismatchedFields": mismatched_fields,
                "baseline": {field: baseline_json.get(field) for field in mismatched_fields},
                "candidate": {field: candidate_json.get(field) for field in mismatched_fields},
            }
            break
        if baseline_json["digest"] != candidate_json["digest"]:
            digest_mismatch = {
                "repetition": repetition,
                "baselineDigest": baseline_json["digest"],
                "candidateDigest": candidate_json["digest"],
            }
            break

        pairs.append([baseline_json["medianNs"], candidate_json["medianNs"]])
        print(f"{case} repetition {repetition:02d} order={','.join(order)}: "
             f"baseline={baseline_json['medianNs']:.0f}ns candidate={candidate_json['medianNs']:.0f}ns "
             f"digest={baseline_json['digest']}")

    result: dict = {"case": case, "requestedRepetitions": repetitions, "pairs": pairs,
                    "failures": failures}
    if config_mismatch is not None:
        result["comparable"] = False
        result["reason"] = "run configuration mismatch between baseline and candidate"
        result["configMismatch"] = config_mismatch
        print(f"{case}: REFUSING to report timing -- configuration differs at repetition "
             f"{config_mismatch['repetition']:02d}: {config_mismatch['mismatchedFields']} "
             f"(baseline={config_mismatch['baseline']} candidate={config_mismatch['candidate']})",
             file=sys.stderr)
    elif digest_mismatch is not None:
        result["comparable"] = False
        result["reason"] = "verify digest mismatch between baseline and candidate"
        result["digestMismatch"] = digest_mismatch
        print(f"{case}: REFUSING to report timing -- verify digests differ at repetition "
             f"{digest_mismatch['repetition']:02d} "
             f"(baseline={digest_mismatch['baselineDigest']} "
             f"candidate={digest_mismatch['candidateDigest']})", file=sys.stderr)
    elif not pairs:
        result["comparable"] = False
        result["reason"] = "no successful paired repetitions"
        print(f"{case}: not comparable -- every repetition failed on at least one side "
             f"({len(failures)}/{repetitions})", file=sys.stderr)
        if failures:
            print(f"{case}: first failure -- baseline rc="
                 f"{failures[0]['baseline']['returncode']} "
                 f"({failures[0]['baseline']['diagnosticTail']}); candidate rc="
                 f"{failures[0]['candidate']['returncode']} "
                 f"({failures[0]['candidate']['diagnosticTail']})", file=sys.stderr)
    elif not has_complete_pairs(len(pairs), repetitions):
        result["comparable"] = False
        result["reason"] = "incomplete paired repetition set"
        result["successfulPairCount"] = len(pairs)
        print(f"{case}: not comparable -- only {len(pairs)}/{repetitions} paired repetitions "
              "succeeded", file=sys.stderr)
    else:
        result["comparable"] = True
        result["analysis"] = analyze_pairs(pairs)
    return result


def collect_environment(out_dir: Path) -> None:
    commands = {
        "hardware": ["system_profiler", "SPHardwareDataType"],
        "macos": ["sw_vers"],
        "xcode": ["xcodebuild", "-version"],
        "power": ["pmset", "-g", "batt"],
        "thermal": ["pmset", "-g", "therm"],
        "gitCommit": ["git", "-C", str(_REPO_ROOT), "rev-parse", "HEAD"],
    }
    environment = {}
    for key, cmd in commands.items():
        try:
            proc = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
            environment[key] = proc.stdout.strip() if proc.returncode == 0 else (
                f"<command failed: {proc.stderr.strip()}>")
        except (OSError, subprocess.TimeoutExpired) as exc:
            environment[key] = f"<unavailable: {exc}>"
    (out_dir / "environment.json").write_text(json.dumps(environment, indent=2) + "\n")


def main() -> int:
    # --selftest is a distinct mode (no --baseline/--candidate, no subprocess launched) recognized
    # before argparse, which would otherwise reject it for lacking those required arguments --
    # mirrors FrameDataBench's own --selftest precedent (Benchmarks/FrameData/Main.cpp).
    if "--selftest" in sys.argv[1:]:
        return 0 if run_selftest() else 1

    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--baseline", required=True, type=Path,
                        help="path to the pre-migration FrameDataBench binary")
    parser.add_argument("--candidate", required=True, type=Path,
                        help="path to the candidate FrameDataBench binary")
    parser.add_argument("--out", type=Path, default=_default_out(),
                        help="output directory (default: outside the repo tree)")
    parser.add_argument("--repetitions", type=int, default=DEFAULT_REPETITIONS,
                        help=f"paired repetitions per workload (spec: exactly {DEFAULT_REPETITIONS} "
                             "for a scored round; pass a smaller number for an explicitly unscored "
                             "trial)")
    parser.add_argument("--warmup", type=int, default=DEFAULT_WARMUP)
    parser.add_argument("--frames", type=int, default=DEFAULT_FRAMES)
    parser.add_argument("--workloads", default=",".join(DEFAULT_WORKLOADS),
                        help="comma-separated workload list")
    parser.add_argument("--label", default="analysis",
                        help="basename for the written analysis JSON (e.g. 'trial' vs 'scored')")
    args = parser.parse_args()

    args.baseline = args.baseline.resolve()
    args.candidate = args.candidate.resolve()
    for role, binary in (("--baseline", args.baseline), ("--candidate", args.candidate)):
        if not binary.is_file():
            print(f"frame_data_paired.py: {role} '{binary}' does not exist or is not a file",
                 file=sys.stderr)
            return 1

    workloads = [w.strip() for w in args.workloads.split(",") if w.strip()]
    unsupported = sorted(set(workloads) - set(DEFAULT_WORKLOADS))
    if unsupported:
        print(f"frame_data_paired.py: unsupported workload(s): {', '.join(unsupported)}; expected "
             f"only {', '.join(DEFAULT_WORKLOADS)}", file=sys.stderr)
        return 1
    if not workloads or args.repetitions <= 0 or args.warmup < 0 or args.frames <= 0:
        print("frame_data_paired.py: workloads must be non-empty, repetitions/frames positive, and "
             "warmup non-negative", file=sys.stderr)
        return 1

    frozen_protocol = (args.repetitions == DEFAULT_REPETITIONS and args.warmup == DEFAULT_WARMUP and
                      args.frames == DEFAULT_FRAMES)
    if not frozen_protocol:
        print("NOTE: repetition/warmup/frame counts differ from the frozen 12/16/256 protocol -- "
             "this run is UNSCORED and must not feed the milestone record.", file=sys.stderr)
        if args.label.lower() == "scored":
            print("frame_data_paired.py: refusing --label=scored for a non-frozen protocol",
                 file=sys.stderr)
            return 1

    # Performance runs use validation and capture off (spec section 11); a debug-layer session
    # inherited from the caller's shell would silently invalidate every timing this driver collects.
    env = dict(os.environ)
    for var in ("MTL_DEBUG_LAYER", "MTL_CAPTURE_ENABLED"):
        if env.pop(var, None) is not None:
            print(f"frame_data_paired.py: unset inherited {var} for this collection",
                 file=sys.stderr)

    raw_dir = args.out / "raw"
    try:
        raw_dir.mkdir(parents=True, exist_ok=True)
    except OSError as exc:
        print(f"frame_data_paired.py: cannot create output directory '{raw_dir}': {exc}",
             file=sys.stderr)
        return 1
    collect_environment(args.out)

    (args.out / "collection.json").write_text(json.dumps({
        "scoredProtocol": frozen_protocol,
        "repetitions": args.repetitions,
        "warmupFrames": args.warmup,
        "measuredFrames": args.frames,
        "workloads": workloads,
        "baseline": str(args.baseline),
        "candidate": str(args.candidate),
    }, indent=2) + "\n")

    results = {}
    for workload in workloads:
        results[workload] = collect_workload(args.baseline, args.candidate, workload,
                                             args.repetitions, args.warmup, args.frames, raw_dir, env)

    analysis_path = args.out / f"{args.label}.json"
    analysis_path.write_text(json.dumps({
        "rule": {"confidence": CONFIDENCE, "resamples": RESAMPLES, "seed": SEED,
                "expectedPairs": DEFAULT_REPETITIONS},
        "workloads": results,
    }, indent=2) + "\n")

    print(f"\nWrote raw per-run JSONs to {raw_dir}")
    print(f"Wrote analysis to {analysis_path}")
    comparable = sum(1 for r in results.values() if r["comparable"])
    print(f"{comparable}/{len(results)} workload(s) comparable")
    exit_code = collection_exit_code(frozen_protocol, comparable, len(results))
    if exit_code != 0:
        print("frame_data_paired.py: frozen collection is incomplete; refusing scored success",
              file=sys.stderr)
    return exit_code


if __name__ == "__main__":
    sys.exit(main())
