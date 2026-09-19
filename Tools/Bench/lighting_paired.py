#!/usr/bin/env python3
"""Collect clustered/direct or frozen-parent zero-light cost diagnostics.

The scored protocol is fixed: 12 alternating AB/BA fresh-process pairs, W32/N256,
Native TAA 1280x720 scale 1, 10,000 paired-median bootstrap resamples, seed 0x4C4D5836.
Every workload and failed attempt remains in the output. Timings never select defaults.
Use --control local for five lit workloads, or --control zero --parent App for six
frozen parent/schema-3 versus candidate/schema-4 workloads. No collection runs in --selftest.
"""
import argparse
import copy
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import statistics
import subprocess
import sys
import unittest

import lighting_report
import visibility_paired as visibility

LOCAL_WORKLOADS = {f"light-lab-{n}": ("light-lab", n, True) for n in (64, 256, 1024, 4096)}
LOCAL_WORKLOADS["sponza"] = ("sponza", 256, False)
ZERO_WORKLOADS = tuple(visibility.DEFAULT_WORKLOADS)
METRICS = (*visibility.METRICS, "lightingGpuMs")
REPETITIONS, WARMUP, FRAMES = 12, 32, 256


def expected_plan(workload, control, side):
    if control == "local":
        scene, n, track = LOCAL_WORKLOADS[workload]
        plan = visibility.expected_plan("sponza", (True, "indirect", "cpu"), WARMUP, FRAMES)
        plan.update(scene=scene, cameraTrack=track, **lighting_report.DEFAULT_PLAN)
        plan.update(localLightMode="direct" if side == "A" else "clustered",
                    localLightRig=scene == "sponza", labLights=n)
        return plan
    plan = visibility.expected_plan(workload, (True, "indirect", "cpu"), WARMUP, FRAMES)
    plan.update(lighting_report.DEFAULT_PLAN)
    plan["localLightMode"] = "off"
    return plan


def command(binary, output, plan, candidate, unscored):
    cmd = [str(binary), "--measure", str(output), "--scene", plan["scene"],
           "--frames", str(FRAMES), "--warmup", str(WARMUP), "--visibility", "cull",
           "--classify", "cpu", "--submission", "indirect", "--temporal", "taa",
           "--render-scale", "1", "--measure-camera", "track" if plan["cameraTrack"] else "initial"]
    if plan["scene"] == "visibility-lab":
        cmd += ["--lab-instances", str(plan["labInstances"])]
    if candidate:
        cmd += ["--local-lights", plan["localLightMode"], "--light-view", "off"]
        if plan["scene"] == "light-lab":
            cmd += ["--lab-lights", str(plan["labLights"]), "--lab-light-pile", "0"]
        if plan["scene"] == "sponza":
            cmd += ["--local-light-rig", "on" if plan["localLightRig"] else "off"]
    if unscored:
        cmd.append("--unscored")
    return cmd


def validate_report(report, expected, scored, candidate):
    if report.get("schemaVersion") != (4 if candidate else 3):
        raise ValueError("expected candidate schema 4 or frozen parent schema 3")
    provenance = visibility.validate_report(report, expected, scored)
    if not candidate and any(p["label"].startswith("lmx.pass.light.") for s in report["samples"] for p in s["passes"]):
        raise ValueError("frozen parent unexpectedly contains local-light passes")
    if candidate:
        generations = {s["lighting"]["sceneGeneration"] for s in report["samples"]}
        counts = {s["lighting"]["liveLightCount"] for s in report["samples"]}
        if len(generations) != 1 or len(counts) != 1:
            raise ValueError("scene generation or live light inventory changed during measurement")
    return provenance


def metric(sample, name):
    if name == "lightingGpuMs":
        return sum(p["gpuMs"] for p in sample["passes"] if p["label"].startswith("lmx.pass.light."))
    return sample[name]


def run_side(binary, path, plan, candidate, unscored, timeout):
    cmd = command(binary, path, plan, candidate, unscored)
    attempt = dict(command=cmd, cwd=str(binary.parent), executable=str(binary),
                   binarySha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
                   startedAt=datetime.now(timezone.utc).isoformat())
    report = None
    try:
        with subprocess.Popen(cmd, cwd=binary.parent, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True) as proc:
            attempt["pid"] = proc.pid
            try:
                stdout, stderr = proc.communicate(timeout=timeout)
            except subprocess.TimeoutExpired:
                proc.kill(); stdout, stderr = proc.communicate()
                attempt.update(stdout=stdout, stderr=stderr)
                raise
        attempt.update(returncode=proc.returncode, stdout=stdout, stderr=stderr)
        if proc.returncode:
            raise ValueError(f"process returned {proc.returncode}")
        report = json.loads(path.read_text())
        validate_report(report, plan, not unscored, candidate)
        attempt["valid"] = True
    except (OSError, ValueError, TypeError, KeyError, subprocess.TimeoutExpired) as exc:
        attempt.update(valid=False, failure=str(exc))
        if isinstance(exc, subprocess.TimeoutExpired):
            attempt.update(stdout=str(exc.stdout or ""), stderr=str(exc.stderr or ""))
        report = None
    attempt["completedAt"] = datetime.now(timezone.utc).isoformat()
    if path.is_file():
        attempt["reportSha256"] = hashlib.sha256(path.read_bytes()).hexdigest()
    with path.with_suffix(".attempt.json").open("x") as stream:
        json.dump(attempt, stream, indent=2); stream.write("\n")
    return report, attempt


def collect_cell(args, workload):
    pairs = {name: [] for name in METRICS}
    observations = []
    failures = []
    for repetition in range(REPETITIONS):
        reports = {}
        order = ("A", "B") if repetition % 2 == 0 else ("B", "A")
        for side in order:
            candidate = args.control == "local" or side == "B"
            role = "candidate" if candidate else "parent"
            binary = args.binary if candidate else args.parent
            path = args.out / f"{workload}.{repetition:02d}.{side}.json"
            plan = expected_plan(workload, args.control, side)
            report, attempt = run_side(binary, path, plan, candidate, args.unscored, args.timeout)
            if report is not None:
                provenance = report["provenance"]
                reason = None
                if provenance["executableHash"] != args.hashes[role]:
                    reason = "executable changed since collection freeze"
                elif role in args.provenance and args.provenance[role] != provenance:
                    reason = "runtime provenance changed across pairs/workloads"
                else:
                    args.provenance[role] = provenance
                if reason:
                    failures.append(dict(repetition=repetition, side=side, reason=reason))
                    report = None
            else:
                failures.append(dict(repetition=repetition, side=side, reason=attempt["failure"]))
            reports[side] = report
        if any(report is None for report in reports.values()):
            continue
        if any(reports["A"]["provenance"][key] != reports["B"]["provenance"][key] for key in ("device", "os", "buildMode", "environment")):
            failures.append(dict(repetition=repetition, reason="paired device/OS/configuration differs"))
            continue
        for name in pairs:
            pairs[name].append([statistics.median(metric(sample, name) for sample in reports[side]["samples"]) for side in ("A", "B")])
        observation = dict(repetition=repetition, order="".join(order))
        for side in ("A", "B"):
            samples = reports[side]["samples"]
            observation[side] = None if reports[side]["schemaVersion"] == 3 else {
                "liveLightCount": samples[0]["lighting"]["liveLightCount"],
                "listBytesMedian": statistics.median(s["lighting"]["listBytes"] for s in samples),
                "allocatedListBytes": max(s["lighting"]["allocatedListBytes"] for s in samples),
                "countersMedian": {key: statistics.median(s["lighting"]["counters"][key] for s in samples)
                                   for key in lighting_report.COUNTERS}}
        observations.append(observation)
        print(f"{workload}: pair {repetition + 1}/12 {''.join(order)}", flush=True)
    complete = not failures and all(len(values) == REPETITIONS for values in pairs.values())
    return dict(workload=workload, complete=complete, failures=failures, retainedPairs=pairs,
                observations=observations,
                analysis={name: visibility.analyze_pairs(values) for name, values in pairs.items()} if complete else {})


def fixture(schema=4, mode="direct"):
    plan = expected_plan("sponza", "zero", "B")
    plan.update(warmupFrames=0, measuredFrames=1, localLightMode=mode)
    counters = dict(candidates=2, visible=1, rejected=1, bypassed=[0, 0, 0, 0], emittedRows=1,
                    emittedCommands=1, overflowedRows=0, overflowedCommands=0, occluded=0,
                    occlusionTested=0, historyInvalid=0, nearCrossing=0, outsideSource=0, rectTooLarge=0)
    v = dict(frameId=1, classify="cpu", retired=False, overflow=False, checkEnabled=False,
             stateMismatches=0, rowMismatches=0, argumentMismatches=0, counterMismatches=0,
             occlusionEnabled=False, occlusionCheckEnabled=False, occlusionHistoryValid=False,
             occlusionInvalidReason="Disabled", occlusionSourceFrame=0, pyramidBytes=0,
             scene=copy.deepcopy(counters), shadow=copy.deepcopy(counters))
    sample = dict(ordinal=0, frameId=1, sequenceFrame=0, retired=True, candidates=2, visible=1, rejected=1,
                  passes=[dict(label="lmx.pass.scene", gpuMs=1)], renderWidth=1280, renderHeight=720,
                  outputWidth=1280, outputHeight=720, effectiveScale=1, vendorFallback=0,
                  effectiveReconstruction=1, sceneCommands=1, shadowCommands=2, tableBytes=1024,
                  listBytes=8, reservedListBytes=8, argumentBytes=60, transientBytes=4096,
                  allocatedListBytes=24, allocatedArgumentBytes=120, candidateBytes=0, runBytes=0,
                  chunkBytes=0, stateBytes=0, counterBytes=0, effectiveClassify="cpu",
                  effectiveSubmission="indirect", visibility=v, **{key: 0 for key in METRICS})
    sample.update(gpuSumMs=1, sceneGpuMs=1, localLightMode=mode, liveLightCount=0)
    sample["lighting"] = dict(frameId=1, sceneGeneration=1, requestedMode=mode, effectiveMode="off",
                              liveLightCount=0, retired=True, counters={key: 0 for key in lighting_report.COUNTERS},
                              listBytes=0, allocatedListBytes=0, checkEnabled=False, gridMismatches=0,
                              indexMismatches=0, counterMismatches=0)
    native_plan = plan if schema == 4 else lighting_report.comparable_plan(schema, plan)
    report = dict(schemaVersion=schema, complete=True, scored=True, interactive=False,
                  pacing="serialized-retirement", plan=native_plan, samples=[sample],
                  provenance=dict(device="gpu", os="os", buildMode="release", executableHash="a" * 64,
                                  shaderHashes={"scene": "b" * 64}, environment={
                                      "MTL_DEBUG_LAYER": "", "MTL_CAPTURE_ENABLED": "",
                                      "MTL_SHADER_VALIDATION": "", "LMX_CAPTURE_AT_FRAME": ""}))
    if schema == 3:
        del sample["lighting"]; del sample["lightingGpuMs"]
    return report, plan


class LightingTests(unittest.TestCase):
    def test_protocol_and_workload_inventory(self):
        self.assertEqual((REPETITIONS, WARMUP, FRAMES, visibility.SEED, visibility.RESAMPLES), (12, 32, 256, 0x4C4D5836, 10000))
        self.assertEqual(len(LOCAL_WORKLOADS), 5); self.assertEqual(len(ZERO_WORKLOADS), 6)
        for n in (64, 256, 1024, 4096):
            p = expected_plan(f"light-lab-{n}", "local", "B")
            self.assertEqual(p["labLights"], n); self.assertEqual(p["localLightMode"], "clustered")

    def test_schema3_parent_and_schema4_candidate(self):
        for schema in (3, 4):
            report, plan = fixture(schema)
            validate_report(report, plan, True, schema == 4)
        report, plan = fixture(mode=lighting_report.DEFAULT_PLAN["localLightMode"])
        visibility.validate_report(report, lighting_report.comparable_plan(3, plan), True)

    def test_forged_lighting_evidence_rejected(self):
        report, plan = fixture()
        mutations = [lambda r: r["samples"][0]["lighting"].update(frameId=99),
                     lambda r: r["samples"][0]["lighting"].update(retired=False),
                     lambda r: r["samples"][0]["lighting"].update(liveLightCount=1),
                     lambda r: r["samples"][0]["lighting"].update(effectiveMode="direct"),
                     lambda r: r["samples"][0]["lighting"].update(indexMismatches=1),
                     lambda r: r["samples"][0].update(lightingGpuMs=.1),
                     lambda r: r["plan"].update(labLights=1024),
                     lambda r: r["plan"].update(unexpected=True),
                     lambda r: r["provenance"]["environment"].update(LMX_LIGHT_CHECK_DUMP="/tmp/raw.bin")]
        for mutation in mutations:
            bad = copy.deepcopy(report); mutation(bad)
            with self.assertRaises(ValueError): validate_report(bad, plan, True, True)

    def test_cluster_scope_and_counter_reconciliation(self):
        report, plan = fixture(mode="clustered"); plan["localLightRig"] = True
        sample = report["samples"][0]; lighting = sample["lighting"]
        lighting.update(liveLightCount=8, effectiveMode="clustered", listBytes=16, allocatedListBytes=786432)
        lighting["counters"].update(candidates=4, assigned=4, maxCount=4)
        sample["passes"].append(dict(label="lmx.pass.light.count", gpuMs=.125))
        sample.update(gpuSumMs=1.125, lightingGpuMs=.125, liveLightCount=8)
        validate_report(report, plan, True, True)
        lighting["counters"]["candidates"] = 5
        with self.assertRaises(ValueError): validate_report(report, plan, True, True)

    def test_legacy_cannot_be_used_for_lit_workload(self):
        report, plan = fixture(3); plan["localLightRig"] = True
        with self.assertRaises(ValueError): validate_report(report, plan, True, False)

    def test_parent_command_has_no_unsupported_lighting_flags(self):
        p = expected_plan("sponza", "zero", "A")
        cmd = command(Path("/frozen/App"), Path("/run.json"), p, False, False)
        self.assertNotIn("--local-lights", cmd)
        cmd = command(Path("/frozen/App"), Path("/run.json"), p, True, False)
        self.assertIn("--local-lights", cmd)
        self.assertEqual(cmd[cmd.index("--local-lights") + 1], "off")


def main():
    if "--selftest" in sys.argv:
        result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(LightingTests))
        return 0 if result.wasSuccessful() else 1
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--parent", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--control", choices=("local", "zero"), default="local")
    parser.add_argument("--workloads")
    parser.add_argument("--unscored", action="store_true")
    parser.add_argument("--timeout", type=int, default=1200)
    args = parser.parse_args()
    allowed = LOCAL_WORKLOADS if args.control == "local" else ZERO_WORKLOADS
    try:
        workloads = visibility.unique_selection(args.workloads or ",".join(allowed), allowed, "workload")
    except ValueError as exc:
        parser.error(str(exc))
    if args.control == "zero" and args.parent is None:
        parser.error("--parent is required for the zero-light control")
    args.binary = args.binary.resolve(); args.out = args.out.resolve()
    if args.parent: args.parent = args.parent.resolve()
    if not args.binary.is_file() or (args.parent and not args.parent.is_file()) or args.timeout <= 0:
        parser.error("frozen binaries and a positive timeout are required")
    if args.out.exists():
        parser.error("output exists; preserve every collection attempt")
    args.hashes = {"candidate": hashlib.sha256(args.binary.read_bytes()).hexdigest()}
    if args.parent: args.hashes["parent"] = hashlib.sha256(args.parent.read_bytes()).hexdigest()
    args.provenance = {}; args.out.mkdir(parents=True)
    summary = dict(schemaVersion=1, control=args.control, protocol=dict(pairs=REPETITIONS, warmup=WARMUP,
                   frames=FRAMES, bootstrapResamples=visibility.RESAMPLES, seed=visibility.SEED), cells=[],
                   scope="Serialized-retirement diagnostic costs; no default or performance adoption decision.")
    for workload in workloads:
        summary["cells"].append(collect_cell(args, workload))
    summary.update(complete=all(c["complete"] for c in summary["cells"]), provenance=args.provenance,
                   frozenExecutableHashes=args.hashes)
    with (args.out / "summary.json").open("x") as stream:
        json.dump(summary, stream, indent=2); stream.write("\n")
    return 0 if summary["complete"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
