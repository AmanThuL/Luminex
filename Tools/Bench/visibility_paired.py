#!/usr/bin/env python3
"""Collect fresh-process CPU visibility/submission controls with strict frame joins.

The frozen 12-pair AB/BA protocol uses W32/N256, Native TAA, 1280x720, scale 1,
10,000 paired-median bootstrap resamples and seed 0x4C4D5836. Positive relative
change means the candidate costs less. No adoption or performance decision is made.
All attempts, including failed processes and malformed reports, remain on disk.
"""
import argparse
import copy
import hashlib
import json
import math
import os
from pathlib import Path
import random
import statistics
import subprocess
import sys

SEED = 0x4C4D5836
RESAMPLES = 10000
WORKLOADS = {
    "visibility-1024": ("visibility-lab", 1024, True),
    "visibility-16384": ("visibility-lab", 16384, True),
    "visibility-65536": ("visibility-lab", 65536, True),
    "sponza": ("sponza", 4096, False),
    "san-miguel": ("san-miguel", 4096, False),
    "temporal-lab": ("temporal-lab", 4096, True),
}
CONTROLS = {
    "cull-off": ((False, "indirect"), (True, "indirect")),
    "indirect-direct": ((True, "direct"), (True, "indirect")),
    "batched-direct": ((True, "direct"), (True, "batched")),
}
METRICS = ("classifyMs", "prepareMs", "encodeMs", "slotWaitMs", "gpuSumMs")
ROOT = Path(__file__).resolve().parents[2]


def analyze_pairs(pairs):
    """Median paired relative change and two-sided 95% percentile bootstrap interval."""
    if not pairs or any(not math.isfinite(a) or not math.isfinite(b) or a <= 0 or b < 0
                        for a, b in pairs):
        return {"available": False, "reason": "missing, nonfinite or zero baseline measurement"}
    deltas = [(a - b) / a * 100 for a, b in pairs]
    rng = random.Random(SEED)
    estimates = sorted(statistics.median([deltas[rng.randrange(len(deltas))] for _ in deltas])
                       for _ in range(RESAMPLES))
    return {"available": True, "pairs": pairs, "pairedDeltasPct": deltas,
            "medianDeltaPct": statistics.median(deltas),
            "ci95Pct": [estimates[250], estimates[9749]], "pairCount": len(pairs)}


def validate_report(report, expected, scored):
    """Reject incomplete reports, wrong invocations, missing frames or invalid provenance."""
    def require_dict(value, name):
        if not isinstance(value, dict):
            raise ValueError("expected object: " + name)
        return value

    def nonnegative_integer(value, name):
        if type(value) is not int or value < 0:
            raise ValueError("expected nonnegative integer: " + name)

    def nonnegative_number(value, name):
        if type(value) not in (float, int) or not math.isfinite(value) or value < 0:
            raise ValueError("invalid measurement: " + name)

    require_dict(report, "report")
    if type(report.get("schemaVersion")) is not int or report["schemaVersion"] != 1 or report.get("complete") is not True:
        raise ValueError("incomplete or unsupported report")
    if report.get("interactive") is not False or report.get("scored") is not scored:
        raise ValueError("scoring/frontend mismatch")
    if report.get("pacing") != "serialized-retirement":
        raise ValueError("unexpected retirement pacing")
    if report.get("plan") != expected:
        raise ValueError("run plan differs from requested invocation")
    provenance = require_dict(report.get("provenance"), "provenance")
    for key in ("device", "os", "buildMode", "executableHash"):
        if not isinstance(provenance.get(key), str) or not provenance[key]:
            raise ValueError("missing provenance: " + key)
    if scored and provenance["buildMode"] != "release":
        raise ValueError("scored collection requires a release binary")
    shader_hashes = require_dict(provenance.get("shaderHashes"), "shaderHashes")
    environment = require_dict(provenance.get("environment"), "environment")
    if not shader_hashes or any(not isinstance(key, str) or not key for key in shader_hashes):
        raise ValueError("missing shader inventory")
    if any(not isinstance(key, str) or not isinstance(value, str) for key, value in environment.items()):
        raise ValueError("invalid environment inventory")
    for key in ("MTL_DEBUG_LAYER", "MTL_CAPTURE_ENABLED", "MTL_SHADER_VALIDATION", "LMX_CAPTURE_AT_FRAME"):
        if key not in environment:
            raise ValueError("missing instrumentation flag: " + key)
    if scored and any((key.startswith(("MTL_", "METAL_", "DYLD_")) or key == "LMX_CAPTURE_AT_FRAME")
                      and value not in ("", "0") for key, value in environment.items()):
        raise ValueError("scored report contains enabled validation/capture instrumentation")
    hashes = [provenance["executableHash"], *shader_hashes.values()]
    if any(not isinstance(value, str) or len(value) != 64 or
           any(c not in "0123456789abcdef" for c in value) for value in hashes):
        raise ValueError("invalid runtime SHA-256 provenance")
    samples = report.get("samples")
    if not isinstance(samples, list) or len(samples) != expected["measuredFrames"]:
        raise ValueError("missing measured frame")
    previous = 0
    for ordinal, sample in enumerate(samples):
        require_dict(sample, "sample")
        for key in ("ordinal", "frameId", "sequenceFrame", "renderWidth", "renderHeight", "outputWidth",
                    "outputHeight", "effectiveReconstruction", "vendorFallback", "candidates", "visible",
                    "rejected", "sceneCommands", "shadowCommands", "tableBytes", "listBytes", "argumentBytes",
                    "transientBytes"):
            nonnegative_integer(sample.get(key), key)
        if sample["ordinal"] != ordinal or sample["sequenceFrame"] != expected["warmupFrames"] + ordinal:
            raise ValueError("missing or unordered plan frame")
        frame_id = sample["frameId"]
        if frame_id <= previous or (previous and frame_id != previous + 1):
            raise ValueError("missing or duplicate RHI frame")
        previous = frame_id
        passes = sample.get("passes")
        if sample.get("retired") is not True or not isinstance(passes, list) or not passes:
            raise ValueError("missing retired GPU join")
        if sample["outputWidth"] != expected["width"] or sample["outputHeight"] != expected["height"]:
            raise ValueError("actual output extent changed")
        if sample["renderWidth"] != expected["width"] or sample["renderHeight"] != expected["height"]:
            raise ValueError("actual raster extent differs from frozen Native TAA scale 1")
        nonnegative_number(sample.get("effectiveScale"), "effectiveScale")
        # Render/Temporal.h orders Raw=0, NativeTaa=1, VendorTemporal=2.
        if sample["effectiveScale"] != 1 or sample["vendorFallback"] != 0 or sample["effectiveReconstruction"] != 1:
            raise ValueError("effective reconstruction differs from frozen Native TAA plan")
        for metric in METRICS:
            nonnegative_number(sample.get(metric), metric)
        for timing in passes:
            require_dict(timing, "pass timing")
            if not isinstance(timing.get("label"), str) or not timing["label"]:
                raise ValueError("invalid pass label")
            nonnegative_number(timing.get("gpuMs"), "pass GPU timing")
        if not math.isclose(sum(p["gpuMs"] for p in passes), sample["gpuSumMs"], rel_tol=1e-12, abs_tol=1e-12):
            raise ValueError("GPU sum differs from joined passes")
        if sample["candidates"] != sample["visible"] + sample["rejected"]:
            raise ValueError("visibility count mismatch")
    return provenance


def unique_selection(text, allowed, label):
    """Validate selectors before any raw directory or attempt path can be created."""
    values = text.split(",")
    if any(value not in allowed for value in values):
        raise ValueError("unknown " + label)
    if len(set(values)) != len(values):
        raise ValueError("duplicate " + label)
    return values


def expected_plan(workload, mode, warmup, frames):
    scene, count, track = WORKLOADS[workload]
    visibility, submission = mode
    return dict(warmupFrames=warmup, measuredFrames=frames, width=1280, height=720,
                labInstances=count, scene=scene, temporal="taa", submission=submission,
                visibilityEnabled=visibility, cameraTrack=track, renderScale=1,
                stepSeconds=1 / 60)


def run_side(binary, report_path, plan, unscored, timeout):
    cmd = [str(binary), "--measure", str(report_path), "--scene", plan["scene"],
           "--frames", str(plan["measuredFrames"]), "--warmup", str(plan["warmupFrames"]),
           "--visibility", "cull" if plan["visibilityEnabled"] else "off",
           "--submission", plan["submission"], "--temporal", "taa", "--render-scale", "1",
           "--measure-camera", "track" if plan["cameraTrack"] else "initial"]
    if plan["scene"] == "visibility-lab":
        cmd += ["--lab-instances", str(plan["labInstances"])]
    if unscored:
        cmd += ["--unscored"]
    attempt = {"command": cmd}
    try:
        proc = subprocess.run(cmd, cwd=binary.parent, capture_output=True, text=True, timeout=timeout)
        attempt.update(returncode=proc.returncode, stdout=proc.stdout, stderr=proc.stderr)
        if proc.returncode:
            raise ValueError("process returned " + str(proc.returncode))
        report = json.loads(report_path.read_text())
        validate_report(report, plan, not unscored)
        attempt["valid"] = True
        return report, attempt
    except (OSError, ValueError, TypeError, KeyError, subprocess.TimeoutExpired) as exc:
        attempt.update(valid=False, failure=str(exc))
        if isinstance(exc, subprocess.TimeoutExpired):
            attempt["stdout"] = str(exc.stdout or "")
            attempt["stderr"] = str(exc.stderr or "")
        return None, attempt
    finally:
        report_path.with_suffix(".attempt.json").write_text(json.dumps(attempt, indent=2) + "\n")


def collect_cell(args, workload, control, output):
    pairs = {metric: [] for metric in METRICS}
    failures = []
    frozen_provenance = None
    for repetition in range(args.repetitions):
        order = (0, 1) if repetition % 2 == 0 else (1, 0)
        reports = {}
        for side in order:
            path = output / f"{workload}.{control}.rep{repetition:02d}.{'A' if side == 0 else 'B'}.json"
            plan = expected_plan(workload, CONTROLS[control][side], args.warmup, args.frames)
            report, attempt = run_side(args.binary, path, plan, args.unscored, args.timeout)
            reports[side] = report
            if report is None:
                failures.append(dict(repetition=repetition, side=side, reason=attempt["failure"]))
        if any(report is None for report in reports.values()):
            continue
        provenance = reports[0]["provenance"]
        if provenance != reports[1]["provenance"] or (frozen_provenance and provenance != frozen_provenance):
            failures.append(dict(repetition=repetition, reason="runtime provenance changed"))
            continue
        if provenance["executableHash"] != args.binary_hash:
            failures.append(dict(repetition=repetition, reason="executable changed since collection freeze"))
            continue
        if args.frozen_provenance is not None and provenance != args.frozen_provenance:
            failures.append(dict(repetition=repetition, reason="runtime provenance differs across workload cells"))
            continue
        frozen_provenance = provenance
        args.frozen_provenance = provenance
        for metric in METRICS:
            pairs[metric].append([statistics.median(sample[metric] for sample in reports[side]["samples"])
                                  for side in (0, 1)])
        print(f"{workload}/{control} pair {repetition + 1}/{args.repetitions} order={order}", flush=True)
    complete = all(len(values) == args.repetitions for values in pairs.values())
    return {"complete": complete, "failures": failures, "provenance": frozen_provenance,
            "analysis": {metric: analyze_pairs(values) for metric, values in pairs.items()} if complete else {},
            "retainedPairs": pairs}


def selftest():
    assert analyze_pairs([[100, 80]] * 12)["ci95Pct"] == [20, 20]
    assert analyze_pairs([[100, 108]] * 12)["ci95Pct"] == [-8, -8]
    assert not analyze_pairs([[0, 1]])["available"]
    varied = [[100, 100 - delta] for delta in (3, -3, 2, -2, 1, -1, 0, 4, -4, 2, -2, 0)]
    assert analyze_pairs(varied)["ci95Pct"] == [-2.0, 2.0]
    plan = expected_plan("sponza", CONTROLS["cull-off"][0], 0, 1)
    provenance = dict(device="gpu", os="os", buildMode="release", executableHash="a" * 64,
                      shaderHashes={"scene": "b" * 64}, environment={
                          "MTL_DEBUG_LAYER": "", "MTL_CAPTURE_ENABLED": "",
                          "MTL_SHADER_VALIDATION": "", "LMX_CAPTURE_AT_FRAME": ""})
    sample = dict(ordinal=0, frameId=1, sequenceFrame=0, retired=True, candidates=2, visible=1, rejected=1,
                  passes=[dict(label="scene", gpuMs=1)], renderWidth=1280, renderHeight=720,
                  outputWidth=1280, outputHeight=720, effectiveScale=1, vendorFallback=0,
                  effectiveReconstruction=1, sceneCommands=1, shadowCommands=2, tableBytes=1024,
                  listBytes=12, argumentBytes=60, transientBytes=4096,
                  **{key: 1 for key in METRICS})
    report = dict(schemaVersion=1, complete=True, scored=True, interactive=False,
                  pacing="serialized-retirement", plan=plan, provenance=provenance, samples=[sample])
    validate_report(report, plan, True)
    for mutation in (lambda r: r["samples"].clear(), lambda r: r["samples"][0].update(frameId=0),
                     lambda r: r["samples"][0].update(sequenceFrame=3),
                     lambda r: r["samples"][0].update(gpuSumMs=float("nan")),
                     lambda r: r["plan"].update(width=640),
                     lambda r: r["provenance"].update(executableHash="unavailable"),
                     lambda r: r.update(scored=False),
                     lambda r: r["samples"][0].update(effectiveReconstruction=0),
                     lambda r: r["provenance"]["environment"].update(MTL_DEBUG_LAYER="1"),
                     lambda r: r["provenance"].update(shaderHashes=["bad"]),
                     lambda r: r["provenance"].update(environment=[]),
                     lambda r: r["samples"][0].pop("sceneCommands"),
                     lambda r: r["samples"][0].update(argumentBytes=-1),
                     lambda r: r["samples"][0].update(tableBytes=True),
                     lambda r: r["samples"].__setitem__(0, []),
                     lambda r: r["samples"][0].update(passes=[[]])):
        bad = copy.deepcopy(report)
        mutation(bad)
        try:
            validate_report(bad, plan, True)
        except ValueError:
            pass
        else:
            raise AssertionError("invalid report accepted")
    assert expected_plan("san-miguel", (True, "direct"), 32, 256)["cameraTrack"] is False
    assert [((0, 1) if rep % 2 == 0 else (1, 0)) for rep in range(4)] == [(0, 1), (1, 0), (0, 1), (1, 0)]
    for text, allowed in (("sponza,sponza", WORKLOADS), ("cull-off,cull-off", CONTROLS)):
        try:
            unique_selection(text, allowed, "selector")
        except ValueError:
            pass
        else:
            raise AssertionError("duplicate selector accepted")
    assert unique_selection("sponza,san-miguel", WORKLOADS, "workload") == ["sponza", "san-miguel"]
    print("visibility_paired.py --selftest: all checks passed")
    return 0


def main():
    if "--selftest" in sys.argv:
        return selftest()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--repetitions", default=12, type=int)
    parser.add_argument("--warmup", default=32, type=int)
    parser.add_argument("--frames", default=256, type=int)
    parser.add_argument("--workloads", default=",".join(WORKLOADS))
    parser.add_argument("--controls", default=",".join(CONTROLS))
    parser.add_argument("--unscored", action="store_true")
    parser.add_argument("--timeout", default=1200, type=int)
    args = parser.parse_args()
    args.binary, args.out = args.binary.resolve(), args.out.resolve()
    try:
        workloads = unique_selection(args.workloads, WORKLOADS, "workload")
        controls = unique_selection(args.controls, CONTROLS, "control")
    except ValueError as exc:
        parser.error(str(exc))
    if not args.binary.is_file() or args.repetitions < 1 or args.warmup < 0 or args.frames < 1:
        parser.error("binary must exist; repetitions/frames positive and warmup nonnegative")
    if args.out == ROOT or ROOT in args.out.parents:
        parser.error("raw output must be outside the source tree")
    if args.out.exists() and any(args.out.iterdir()):
        parser.error("output must be new or empty; previous attempts are never replaced")
    frozen = (args.repetitions, args.warmup, args.frames) == (12, 32, 256)
    if not frozen and not args.unscored:
        parser.error("non-frozen protocol requires --unscored")
    args.out.mkdir(parents=True, exist_ok=True)
    raw = args.out / "raw"
    raw.mkdir()
    args.binary_hash = hashlib.sha256(args.binary.read_bytes()).hexdigest()
    args.frozen_provenance = None
    collection = dict(schemaVersion=1, scoredProtocol=frozen and not args.unscored,
                      binary=str(args.binary), binarySha256=args.binary_hash,
                      seed=SEED, resamples=RESAMPLES, confidence=0.95,
                      direction="positive means candidate costs less", adoptionRule=None,
                      pacing="serialized-retirement; does not measure throughput",
                      repetitions=args.repetitions, warmup=args.warmup, frames=args.frames,
                      environment={k: v for k, v in os.environ.items() if k.startswith(("MTL_", "METAL_", "DYLD_", "LMX_"))},
                      cells={})
    destination = args.out / "analysis.json"
    for workload in workloads:
        for control in controls:
            collection["cells"][workload + "/" + control] = collect_cell(args, workload, control, raw)
            destination.write_text(json.dumps(collection, indent=2) + "\n")
    complete = all(cell["complete"] for cell in collection["cells"].values())
    print(f"Wrote {destination}; complete={complete}")
    return 0 if complete else 1


if __name__ == "__main__":
    raise SystemExit(main())
