#!/usr/bin/env python3
"""Compare measured frames of a parent App build with a candidate build, scene by scene.

Each scene runs the identical default command `App --scene S --measure out.json --warmup 32
--frames 256` on both binaries, a fresh process per side, in 12 alternating AB/BA pairs (A is the
parent). Reports must be complete schema-5 runs with equal plans. encodeMs, gpuSumMs and every
per-frame `*GpuMs` stage both sides report are compared with the paired-median bootstrap of
visibility_paired (10,000 resamples, seed 0x4C4D5836). A metric regresses when the candidate's
paired median is more than 5% slower and the 95% interval excludes 5%; in analyze_pairs' sign,
where positive means the candidate costs less, that is medianDeltaPct < -5 and ci95Pct[1] < -5.
Every attempt stays on disk beside summary.json. No collection runs in --selftest.
"""
import argparse
import copy
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import statistics
import subprocess
import sys
import unittest
from unittest import mock

import visibility_paired as visibility

SCENES = ("sponza", "material-lab", "temporal-lab", "light-lab", "visibility-lab")
OPTIONAL_SCENES = ("san-miguel",)
REPETITIONS, WARMUP, FRAMES = 12, 32, 256
THRESHOLD_PCT = 5.0
INSTRUMENTATION_PREFIXES = ("MTL_", "METAL_", "DYLD_")


def command(binary, output, scene, warmup, frames, unscored):
    cmd = [str(binary), "--scene", scene, "--measure", str(output),
           "--warmup", str(warmup), "--frames", str(frames)]
    return cmd + ["--unscored"] if unscored else cmd


def number(value, name):
    if type(value) not in (int, float) or not math.isfinite(value) or value < 0:
        raise ValueError("invalid measurement: " + name)


def stage_metrics(samples):
    """encodeMs, gpuSumMs and every *GpuMs key present in every sample, in a stable order."""
    common = set.intersection(*(set(sample) for sample in samples))
    stages = sorted(key for key in common if key.endswith("GpuMs") and key != "gpuSumMs")
    return ["encodeMs", "gpuSumMs", *stages]


def validate_report(report, scene, warmup, frames, scored):
    """Reject anything but a complete schema-5 run of the requested scene and frame counts."""
    if not isinstance(report, dict) or report.get("schemaVersion") != 5 or report.get("complete") is not True:
        raise ValueError("incomplete or non-schema-5 report")
    if report.get("interactive") is not False or report.get("scored") is not scored:
        raise ValueError("scoring/frontend mismatch")
    if report.get("pacing") != "serialized-retirement":
        raise ValueError("unexpected retirement pacing")
    plan = report.get("plan")
    if (not isinstance(plan, dict) or plan.get("scene") != scene or plan.get("warmupFrames") != warmup or
            plan.get("measuredFrames") != frames):
        raise ValueError("run plan differs from requested invocation")
    provenance = report.get("provenance")
    if not isinstance(provenance, dict):
        raise ValueError("missing provenance")
    for key in ("device", "os", "buildMode", "executableHash"):
        if not isinstance(provenance.get(key), str) or not provenance[key]:
            raise ValueError("missing provenance: " + key)
    environment = provenance.get("environment")
    if not isinstance(environment, dict):
        raise ValueError("missing environment inventory")
    if scored:
        if provenance["buildMode"] != "release":
            raise ValueError("scored comparison requires release binaries")
        if any(key.startswith(INSTRUMENTATION_PREFIXES) and value not in ("", "0")
               for key, value in environment.items()):
            raise ValueError("scored report contains enabled validation/capture instrumentation")
    document = report.get("sceneDocument")
    if not isinstance(document, dict) or not isinstance(document.get("sha256"), str) or len(document["sha256"]) != 64:
        raise ValueError("missing sceneDocument provenance")
    if scored and document.get("dirty") is not False:
        raise ValueError("scored run measured a document with unsaved edits")
    samples = report.get("samples")
    if not isinstance(samples, list) or len(samples) != frames:
        raise ValueError("missing measured frame")
    for ordinal, sample in enumerate(samples):
        if not isinstance(sample, dict) or sample.get("ordinal") != ordinal or sample.get("retired") is not True:
            raise ValueError("missing or unretired measured frame")
        passes = sample.get("passes")
        if not isinstance(passes, list) or not passes:
            raise ValueError("missing GPU pass timings")
        for timing in passes:
            if not isinstance(timing, dict) or not isinstance(timing.get("label"), str):
                raise ValueError("invalid pass timing")
            number(timing.get("gpuMs"), "pass GPU timing")
        for key in ("encodeMs", "gpuSumMs"):
            number(sample.get(key), key)
        for key, value in sample.items():
            if key.endswith("GpuMs"):
                number(value, key)
        if not math.isclose(sum(p["gpuMs"] for p in passes), sample["gpuSumMs"], rel_tol=1e-9, abs_tol=1e-9):
            raise ValueError("GPU sum differs from joined passes")


def check_pair(reports):
    """The two sides must have measured the same plan, document and population on one machine."""
    parent, candidate = reports
    if parent["plan"] != candidate["plan"]:
        raise ValueError("plans differ between parent and candidate")
    for key in ("device", "os", "buildMode"):
        if parent["provenance"][key] != candidate["provenance"][key]:
            raise ValueError("paired provenance differs: " + key)
    if parent["sceneDocument"]["sha256"] != candidate["sceneDocument"]["sha256"]:
        raise ValueError("scene documents differ between parent and candidate")
    if parent.get("startingPopulation") != candidate.get("startingPopulation"):
        raise ValueError("starting populations differ between parent and candidate")


def verdict(analysis):
    """Apply the parity-gate rule to one analyze_pairs result (positive = candidate cheaper)."""
    if not analysis.get("available"):
        return "unavailable"
    median, (_, upper) = analysis["medianDeltaPct"], analysis["ci95Pct"]
    return "regression" if median < -THRESHOLD_PCT and upper < -THRESHOLD_PCT else "pass"


def run_side(binary, path, scene, args, env):
    cmd = command(binary, path, scene, args.warmup, args.frames, args.unscored)
    attempt = dict(command=cmd, cwd=str(binary.parent), extraEnvironment=env,
                   startedAt=datetime.now(timezone.utc).isoformat())
    report = None
    try:
        proc = subprocess.run(cmd, cwd=binary.parent, capture_output=True, text=True,
                              timeout=args.timeout, env={**os.environ, **env})
        attempt.update(returncode=proc.returncode, stdout=proc.stdout[-20000:], stderr=proc.stderr[-20000:])
        if proc.returncode:
            raise ValueError(f"process returned {proc.returncode}")
        report = json.loads(path.read_text())
        validate_report(report, scene, args.warmup, args.frames, not args.unscored)
        if report["provenance"]["executableHash"] != args.hashes[str(binary)]:
            raise ValueError("executable changed since collection freeze")
        attempt["valid"] = True
    except (OSError, ValueError, TypeError, KeyError, subprocess.TimeoutExpired) as exc:
        attempt.update(valid=False, failure=str(exc))
        report = None
    attempt["completedAt"] = datetime.now(timezone.utc).isoformat()
    with path.with_suffix(".attempt.json").open("x") as stream:
        json.dump(attempt, stream, indent=2); stream.write("\n")
    return report, attempt


def collect_scene(args, scene):
    pairs, failures, metrics = {}, [], None
    only = {"parent": set(), "candidate": set()}
    for repetition in range(args.repetitions):
        order = (0, 1) if repetition % 2 == 0 else (1, 0)
        reports = [None, None]
        for side in order:
            binary = args.parent if side == 0 else args.binary
            env = args.parent_env if side == 0 else {}
            path = args.out / f"{scene}.rep{repetition:02d}.{'AB'[side]}.json"
            reports[side], attempt = run_side(binary, path, scene, args, env)
            if reports[side] is None:
                failures.append(dict(repetition=repetition, side="AB"[side], reason=attempt["failure"]))
        if None in reports:
            continue
        try:
            check_pair(reports)
            sides = [stage_metrics(report["samples"]) for report in reports]
            only["parent"].update(set(sides[0]) - set(sides[1]))
            only["candidate"].update(set(sides[1]) - set(sides[0]))
            names = [name for name in sides[0] if name in sides[1]]
            if metrics is not None and names != metrics:
                raise ValueError("stage metrics changed across pairs")
        except ValueError as exc:
            failures.append(dict(repetition=repetition, reason=str(exc)))
            continue
        metrics = names
        for name in metrics:
            pairs.setdefault(name, []).append(
                [statistics.median(sample[name] for sample in report["samples"]) for report in reports])
        print(f"{scene}: pair {repetition + 1}/{args.repetitions} {'AB' if order[0] == 0 else 'BA'}", flush=True)
    complete = not failures and metrics is not None and all(len(v) == args.repetitions for v in pairs.values())
    analysis = {name: visibility.analyze_pairs(values) for name, values in pairs.items()} if complete else {}
    verdicts = {name: verdict(result) for name, result in analysis.items()}
    return dict(scene=scene, complete=complete, failures=failures, metrics=metrics or [],
                onlyInParent=sorted(only["parent"]), onlyInCandidate=sorted(only["candidate"]),
                retainedPairs=pairs, analysis=analysis, verdicts=verdicts)


def gate(cells, scored):
    """Assemble the gate: regressions, unrateable metrics and stages the candidate dropped.

    An unrateable encodeMs or gpuSumMs fails; an unrateable stage is listed only. A stage the parent
    reports and the candidate does not fails a scored comparison, so a renamed stage cannot vanish."""
    regressions = [dict(scene=c["scene"], metric=name, **{k: c["analysis"][name][k] for k in ("medianDeltaPct", "ci95Pct")})
                   for c in cells for name, v in c["verdicts"].items() if v == "regression"]
    unavailable = [dict(scene=c["scene"], metric=name, gating=name in ("encodeMs", "gpuSumMs"),
                        reason=c["analysis"][name].get("reason", ""))
                   for c in cells for name, v in c["verdicts"].items() if v == "unavailable"]
    dropped = [dict(scene=c["scene"], metric=name) for c in cells for name in c["onlyInParent"]]
    added = [dict(scene=c["scene"], metric=name) for c in cells for name in c["onlyInCandidate"]]
    complete = all(c["complete"] for c in cells)
    passed = (complete and not regressions and not any(u["gating"] for u in unavailable) and
              not (scored and dropped))
    return dict(complete=complete, passed=passed, regressions=regressions, unavailable=unavailable,
                onlyInParent=dropped, onlyInCandidate=added)


def summarize(cells):
    lines = [f"{'scene':16} {'metric':18} {'parent ms':>10} {'cand ms':>10} {'median %':>9} {'ci95 %':>17}  verdict"]
    for cell in cells:
        if not cell["complete"]:
            lines.append(f"{cell['scene']:16} incomplete: {len(cell['failures'])} failure(s)")
            continue
        for name, result in cell["analysis"].items():
            absolute = result["absolute"]
            relative = (f"{result['medianDeltaPct']:9.2f} [{result['ci95Pct'][0]:7.2f},{result['ci95Pct'][1]:7.2f}]"
                        if result["available"] else f"{'-':>9} {'-':>17}")
            lines.append(f"{cell['scene']:16} {name:18} {absolute['baselineMedianMs']:10.4f} "
                         f"{absolute['candidateMedianMs']:10.4f} {relative}  {cell['verdicts'][name]}")
    return "\n".join(lines)


def fixture(scene="sponza", frames=2, scored=True, encode=1.0, gpu=2.0, executable="a" * 64):
    plan = dict(scene=scene, warmupFrames=0, measuredFrames=frames, temporal="taa")
    samples = [dict(ordinal=i, retired=True, encodeMs=encode, gpuSumMs=gpu, sceneGpuMs=gpu / 2,
                    shadowGpuMs=gpu / 2, passes=[dict(label="lmx.pass.scene", gpuMs=gpu / 2),
                                                 dict(label="lmx.pass.shadow", gpuMs=gpu / 2)])
               for i in range(frames)]
    return dict(schemaVersion=5, complete=True, scored=scored, interactive=False,
                pacing="serialized-retirement", plan=plan, samples=samples,
                sceneDocument=dict(path="/s.gltf", sha256="d" * 64, dirty=False),
                startingPopulation=dict(objects=3),
                provenance=dict(device="gpu", os="os", buildMode="release", executableHash=executable,
                                shaderHashes={}, environment={"MTL_DEBUG_LAYER": ""}))


class ParentPairedTests(unittest.TestCase):
    def test_protocol(self):
        self.assertEqual((REPETITIONS, WARMUP, FRAMES, THRESHOLD_PCT), (12, 32, 256, 5.0))
        self.assertEqual((visibility.SEED, visibility.RESAMPLES), (0x4C4D5836, 10000))
        self.assertEqual(len(SCENES), 5)
        self.assertEqual(command(Path("/p/App"), Path("/o.json"), "light-lab", 32, 256, False),
                         ["/p/App", "--scene", "light-lab", "--measure", "/o.json", "--warmup", "32", "--frames", "256"])
        self.assertEqual(command(Path("/p/App"), Path("/o.json"), "sponza", 1, 2, True)[-1], "--unscored")

    def test_sign_convention_flags_a_slower_candidate_only(self):
        # Pairs are [parent, candidate]; analyze_pairs reports (parent - candidate) / parent.
        slower = visibility.analyze_pairs([[1.0, 1.10 + i * 1e-3] for i in range(12)])
        self.assertLess(slower["medianDeltaPct"], -5)
        self.assertEqual(verdict(slower), "regression")
        faster = visibility.analyze_pairs([[1.0, 0.90 - i * 1e-3] for i in range(12)])
        self.assertGreater(faster["medianDeltaPct"], 5)
        self.assertEqual(verdict(faster), "pass")
        self.assertEqual(verdict(visibility.analyze_pairs([[1.0, 1.04]] * 12)), "pass")
        # A median beyond +5% whose interval still reaches +5% is noise, not a regression.
        noisy = visibility.analyze_pairs([[1.0, c] for c in (1.0, 1.01, 1.02, 1.03, 1.04, 1.06,
                                                              1.07, 1.08, 1.09, 1.10, 1.11, 1.12)])
        self.assertLess(noisy["medianDeltaPct"], -5)
        self.assertGreaterEqual(noisy["ci95Pct"][1], -5)
        self.assertEqual(verdict(noisy), "pass")
        self.assertEqual(verdict(visibility.analyze_pairs([[0.0, 1.0]] * 12)), "unavailable")

    def test_report_validation(self):
        validate_report(fixture(), "sponza", 0, 2, True)
        mutations = [lambda r: r.update(schemaVersion=4), lambda r: r.update(complete=False),
                     lambda r: r.update(scored=False), lambda r: r["plan"].update(scene="light-lab"),
                     lambda r: r["plan"].update(measuredFrames=3), lambda r: r["samples"].pop(),
                     lambda r: r["samples"][1].update(retired=False),
                     lambda r: r["samples"][0].update(encodeMs=float("nan")),
                     lambda r: r["samples"][0].update(sceneGpuMs=-1),
                     lambda r: r["samples"][0].update(gpuSumMs=5),
                     lambda r: r["provenance"].update(buildMode="debug"),
                     lambda r: r["provenance"]["environment"].update(MTL_DEBUG_LAYER="1"),
                     lambda r: r["sceneDocument"].update(dirty=True)]
        for mutation in mutations:
            bad = copy.deepcopy(fixture()); mutation(bad)
            with self.assertRaises(ValueError):
                validate_report(bad, "sponza", 0, 2, True)
        # The App itself refuses a scored run under a DYLD_ loader path; so does the driver.
        report = fixture(); report["provenance"]["environment"]["DYLD_LIBRARY_PATH"] = "/sdl"
        with self.assertRaises(ValueError):
            validate_report(report, "sponza", 0, 2, True)
        report = fixture(scored=False); report["provenance"]["environment"]["MTL_DEBUG_LAYER"] = "1"
        validate_report(report, "sponza", 0, 2, False)

    def test_pair_checks_and_stage_discovery(self):
        check_pair([fixture(), fixture()])
        for mutation in (lambda r: r["plan"].update(temporal="off"),
                         lambda r: r["sceneDocument"].update(sha256="e" * 64),
                         lambda r: r.update(startingPopulation=dict(objects=4)),
                         lambda r: r["provenance"].update(device="other")):
            bad = fixture(); mutation(bad)
            with self.assertRaises(ValueError):
                check_pair([fixture(), bad])
        samples = fixture()["samples"]
        self.assertEqual(stage_metrics(samples), ["encodeMs", "gpuSumMs", "sceneGpuMs", "shadowGpuMs"])
        del samples[1]["shadowGpuMs"]
        self.assertEqual(stage_metrics(samples), ["encodeMs", "gpuSumMs", "sceneGpuMs"])

    def test_collection_alternates_order_and_scopes_parent_environment(self):
        import contextlib
        import io
        import tempfile

        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "p").mkdir(); (root / "c").mkdir()
            parent, candidate = root / "p" / "App", root / "c" / "App"
            parent.write_bytes(b"parent"); candidate.write_bytes(b"candidate")
            args = argparse.Namespace(parent=parent, binary=candidate, out=root / "out", repetitions=4,
                                      warmup=0, frames=2, unscored=True, timeout=5,
                                      parent_env={"DYLD_LIBRARY_PATH": "/sdl"})
            args.out.mkdir()
            args.hashes = {str(b): hashlib.sha256(b.read_bytes()).hexdigest() for b in (parent, candidate)}
            calls = []

            def fake_run(cmd, cwd, env, **_):
                is_parent = cmd[0] == str(parent)
                calls.append((is_parent, cmd[1:], cwd, env.get("DYLD_LIBRARY_PATH")))
                report = fixture(encode=1.0 if is_parent else 1.5, executable=args.hashes[cmd[0]],
                                 scored="--unscored" not in cmd)
                report["samples"][0]["visibilityGpuMs"] = 0.0
                for sample in report["samples"]:
                    sample["lightingGpuMs"] = 0.0
                if is_parent:
                    report["provenance"]["environment"]["DYLD_LIBRARY_PATH"] = "/sdl"
                Path(cmd[cmd.index("--measure") + 1]).write_text(json.dumps(report))
                return subprocess.CompletedProcess(cmd, 0, "", "")

            with mock.patch.object(subprocess, "run", side_effect=fake_run), \
                    contextlib.redirect_stdout(io.StringIO()):
                cell = collect_scene(args, "sponza")
            self.assertTrue(cell["complete"], cell["failures"])
            self.assertEqual([c[0] for c in calls], [True, False, False, True] * 2)
            self.assertEqual({c[3] for c in calls if c[0]}, {"/sdl"})
            self.assertEqual({c[3] for c in calls if not c[0]}, {os.environ.get("DYLD_LIBRARY_PATH")})
            self.assertEqual({c[2] for c in calls}, {parent.parent, candidate.parent})
            stripped = {tuple(a for a in c[1] if not a.endswith(".json")) for c in calls}
            self.assertEqual(len(stripped), 1)
            self.assertEqual(cell["metrics"], ["encodeMs", "gpuSumMs", "lightingGpuMs", "sceneGpuMs", "shadowGpuMs"])
            self.assertEqual(cell["retainedPairs"]["encodeMs"], [[1.0, 1.5]] * 4)
            self.assertEqual(cell["verdicts"]["encodeMs"], "regression")
            self.assertEqual(cell["verdicts"]["gpuSumMs"], "pass")
            self.assertEqual(cell["verdicts"]["lightingGpuMs"], "unavailable")
            self.assertIn("regression", summarize([cell]))
            self.assertEqual(len(list(args.out.glob("*.attempt.json"))), 8)

            args.out = root / "out2"; args.out.mkdir(); args.unscored = False
            with mock.patch.object(subprocess, "run", side_effect=fake_run), \
                    contextlib.redirect_stdout(io.StringIO()):
                cell = collect_scene(args, "sponza")
            self.assertFalse(cell["complete"])
            self.assertIn("instrumentation", cell["failures"][0]["reason"])

    def test_gate_fails_unrateable_totals_and_dropped_stages(self):
        def cell(verdicts, parent_only=(), candidate_only=()):
            analysis = {name: {"medianDeltaPct": -1.0, "ci95Pct": [-2.0, 0.0], "reason": "zero"} for name in verdicts}
            return dict(scene="sponza", complete=True, verdicts=verdicts, analysis=analysis,
                        onlyInParent=list(parent_only), onlyInCandidate=list(candidate_only))
        ok = {"encodeMs": "pass", "gpuSumMs": "pass"}
        self.assertTrue(gate([cell(ok)], True)["passed"])
        result = gate([cell({**ok, "hzbGpuMs": "unavailable"})], True)
        self.assertTrue(result["passed"])
        self.assertEqual(result["unavailable"], [dict(scene="sponza", metric="hzbGpuMs", gating=False, reason="zero")])
        self.assertFalse(gate([cell({**ok, "gpuSumMs": "unavailable"})], True)["passed"])
        self.assertFalse(gate([cell({**ok, "encodeMs": "regression"})], True)["passed"])
        dropped = gate([cell(ok, parent_only=["hzbGpuMs"], candidate_only=["newGpuMs"])], True)
        self.assertFalse(dropped["passed"])
        self.assertEqual(dropped["onlyInParent"], [dict(scene="sponza", metric="hzbGpuMs")])
        self.assertEqual(dropped["onlyInCandidate"], [dict(scene="sponza", metric="newGpuMs")])
        self.assertTrue(gate([cell(ok, parent_only=["hzbGpuMs"])], False)["passed"])
        self.assertTrue(gate([cell(ok, candidate_only=["newGpuMs"])], True)["passed"])
        incomplete = cell(ok); incomplete["complete"] = False
        self.assertFalse(gate([incomplete], True)["passed"])

    def test_main_writes_summary_and_exit_code(self):
        import contextlib
        import io
        import tempfile

        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp).resolve()
            (root / "p").mkdir(); (root / "c").mkdir()
            parent, candidate = root / "p" / "App", root / "c" / "App"
            parent.write_bytes(b"parent"); candidate.write_bytes(b"candidate")
            hashes = {str(b): hashlib.sha256(b.read_bytes()).hexdigest() for b in (parent, candidate)}

            def run_main(out, slower_scene, parent_extra_stage):
                def fake_run(cmd, cwd, env, **_):
                    is_parent = cmd[0] == str(parent)
                    scene = cmd[cmd.index("--scene") + 1]
                    encode = 1.5 if scene == slower_scene and not is_parent else 1.0
                    report = fixture(scene=scene, encode=encode, executable=hashes[cmd[0]])
                    if is_parent and parent_extra_stage:
                        for sample in report["samples"]:
                            sample["hzbGpuMs"] = 0.5
                    Path(cmd[cmd.index("--measure") + 1]).write_text(json.dumps(report))
                    return subprocess.CompletedProcess(cmd, 0, "", "")

                argv = ["--parent", str(parent), "--binary", str(candidate), "--out", str(out),
                        "--scenes", "sponza,light-lab", "--repetitions", "2", "--warmup", "0", "--frames", "2"]
                with mock.patch.object(subprocess, "run", side_effect=fake_run), \
                        contextlib.redirect_stdout(io.StringIO()) as stdout:
                    code = main(argv)
                return code, json.loads((out / "summary.json").read_text()), stdout.getvalue()

            code, summary, stdout = run_main(root / "clean", None, False)
            self.assertEqual(code, 0)
            self.assertTrue(summary["passed"] and summary["complete"] and summary["scored"])
            self.assertEqual([c["scene"] for c in summary["cells"]], ["sponza", "light-lab"])
            for key in ("regressions", "unavailable", "onlyInParent", "onlyInCandidate"):
                self.assertEqual(summary[key], [])
            self.assertEqual(summary["candidate"]["sha256"], hashes[str(candidate)])
            self.assertIn("gate passed", stdout)
            self.assertTrue((root / "clean" / "summary.txt").is_file())

            code, summary, stdout = run_main(root / "bad", "light-lab", True)
            self.assertEqual(code, 1)
            self.assertFalse(summary["passed"])
            self.assertEqual([(r["scene"], r["metric"]) for r in summary["regressions"]], [("light-lab", "encodeMs")])
            self.assertLess(summary["regressions"][0]["medianDeltaPct"], -5)
            self.assertEqual(summary["onlyInParent"], [dict(scene="sponza", metric="hzbGpuMs"),
                                                       dict(scene="light-lab", metric="hzbGpuMs")])
            self.assertEqual(summary["cells"][0]["onlyInParent"], ["hzbGpuMs"])
            self.assertIn("gate FAILED", stdout)
            with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                main(["--parent", str(parent), "--binary", str(candidate), "--out", str(root / "bad")])


def parse_env(text):
    key, sep, value = text.partition("=")
    if not sep or not key:
        raise argparse.ArgumentTypeError("expected KEY=VALUE")
    return key, value


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    if "--selftest" in argv:
        suite = unittest.defaultTestLoader.loadTestsFromTestCase(ParentPairedTests)
        return 0 if unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful() else 1
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--parent", type=Path, required=True, help="frozen parent App")
    parser.add_argument("--binary", type=Path, required=True, help="candidate App")
    parser.add_argument("--out", type=Path, required=True, help="new directory for every attempt and summary.json")
    parser.add_argument("--scenes", help="comma list; default " + ",".join(SCENES))
    parser.add_argument("--parent-env", type=parse_env, action="append", default=[], metavar="KEY=VALUE",
                        help="environment for parent processes only, e.g. a frozen build's SDL3 "
                             "DYLD_LIBRARY_PATH; the App refuses scored runs under DYLD_ variables")
    parser.add_argument("--repetitions", type=int, default=REPETITIONS)
    parser.add_argument("--warmup", type=int, default=WARMUP)
    parser.add_argument("--frames", type=int, default=FRAMES)
    parser.add_argument("--unscored", action="store_true")
    parser.add_argument("--timeout", type=int, default=600)
    args = parser.parse_args(argv)
    try:
        scenes = visibility.unique_selection(args.scenes or ",".join(SCENES), SCENES + OPTIONAL_SCENES, "scene")
    except ValueError as exc:
        parser.error(str(exc))
    args.parent, args.binary, args.out = args.parent.resolve(), args.binary.resolve(), args.out.resolve()
    if not args.parent.is_file() or not args.binary.is_file() or args.parent == args.binary:
        parser.error("two distinct App binaries are required")
    if args.repetitions < 2 or args.repetitions % 2 or args.frames < 1 or args.warmup < 0 or args.timeout <= 0:
        parser.error("an even repetition count of at least 2, frames, warmup and a timeout are required")
    if args.out.exists():
        parser.error("output exists; preserve every collection attempt")
    args.parent_env = dict(args.parent_env)
    args.hashes = {str(b): hashlib.sha256(b.read_bytes()).hexdigest() for b in (args.parent, args.binary)}
    args.out.mkdir(parents=True)
    cells = [collect_scene(args, scene) for scene in scenes]
    summary = dict(schemaVersion=1, scored=not args.unscored,
                   protocol=dict(pairs=args.repetitions, warmup=args.warmup, frames=args.frames,
                                 bootstrapResamples=visibility.RESAMPLES, seed=visibility.SEED,
                                 rule="regression: paired median < -5% and ci95 upper < -5% (positive = candidate cheaper)"),
                   parent=dict(path=str(args.parent), sha256=args.hashes[str(args.parent)], environment=args.parent_env),
                   candidate=dict(path=str(args.binary), sha256=args.hashes[str(args.binary)]),
                   **gate(cells, not args.unscored), cells=cells)
    with (args.out / "summary.json").open("x") as stream:
        json.dump(summary, stream, indent=2); stream.write("\n")
    table = summarize(cells)
    (args.out / "summary.txt").write_text(table + "\n")
    print(table)
    for key in ("regressions", "unavailable", "onlyInParent", "onlyInCandidate"):
        for row in summary[key]:
            print(f"{key}: {row['scene']} {row['metric']}")
    print("gate " + ("passed" if summary["passed"] else "FAILED"))
    return 0 if summary["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
