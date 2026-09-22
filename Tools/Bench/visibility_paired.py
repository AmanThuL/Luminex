#!/usr/bin/env python3
"""Collect fresh-process CPU/GPU visibility/submission controls with strict frame joins.

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
import tempfile
from types import SimpleNamespace
from unittest import mock

import lighting_report

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
DEFAULT_WORKLOADS = tuple(WORKLOADS)
WORKLOADS.update({
    "occluded-1024": ("visibility-lab", 1024, True),
    "occluded-16384": ("visibility-lab", 16384, True),
    "occluded-65536": ("visibility-lab", 65536, True),
})
CONTROLS = {
    "cull-off": ((False, "indirect", "cpu"), (True, "indirect", "cpu")),
    "indirect-direct": ((True, "direct", "cpu"), (True, "indirect", "cpu")),
    "batched-direct": ((True, "direct", "cpu"), (True, "batched", "cpu")),
}
CONTROLS.update({
    "gpu-cpu-indirect": ((True, "indirect", "cpu"), (True, "indirect", "gpu")),
    "gpu-cpu-batched": ((True, "batched", "cpu"), (True, "batched", "gpu")),
})
DEFAULT_CONTROLS = tuple(CONTROLS)
CONTROLS.update({
    "occlusion-on-off-indirect": ((True, "indirect", "gpu", False), (True, "indirect", "gpu", True)),
    "occlusion-on-off-batched": ((True, "batched", "gpu", False), (True, "batched", "gpu", True)),
})
BINARY_WORKLOADS = ("sponza", "visibility-16384")
BINARY_CONTROLS = {"binary-" + mode: ((True, mode, "cpu"),) * 2
                   for mode in ("direct", "indirect", "batched")}
CONTROLS.update(BINARY_CONTROLS)
METRICS = ("classifyMs", "prepareMs", "encodeMs", "slotWaitMs", "gpuSumMs", "visibilityGpuMs",
           "hzbGpuMs", "sceneGpuMs", "shadowGpuMs")
ROOT = Path(__file__).resolve().parents[2]


def analyze_pairs(pairs):
    """Absolute costs/deltas always retain valid zero baselines; available means relative %."""
    if not pairs or any(not math.isfinite(a) or not math.isfinite(b) or a < 0 or b < 0
                        for a, b in pairs):
        reason = "missing, nonfinite or negative measurement"
        return {"available": False, "reason": reason,
                "absolute": {"available": False, "reason": reason}}

    def interval(deltas):
        rng = random.Random(SEED)
        estimates = sorted(statistics.median([deltas[rng.randrange(len(deltas))] for _ in deltas])
                           for _ in range(RESAMPLES))
        return [estimates[250], estimates[9749]]

    deltas_ms = [a - b for a, b in pairs]
    result = {"available": False, "pairs": pairs, "pairCount": len(pairs),
              "absolute": {"available": True,
                           "baselineMedianMs": statistics.median(a for a, _ in pairs),
                           "candidateMedianMs": statistics.median(b for _, b in pairs),
                           "pairedDeltasMs": deltas_ms,
                           "medianDeltaMs": statistics.median(deltas_ms),
                           "ci95Ms": interval(deltas_ms)}}
    if any(a == 0 for a, _ in pairs):
        result["reason"] = "relative change unavailable with zero baseline measurement"
        return result
    deltas = [(a - b) / a * 100 for a, b in pairs]
    result.update(available=True, pairedDeltasPct=deltas,
                  medianDeltaPct=statistics.median(deltas), ci95Pct=interval(deltas))
    return result


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
    if type(report.get("schemaVersion")) is not int or report["schemaVersion"] not in (2, 3, 4) or report.get("complete") is not True:
        raise ValueError("incomplete or unsupported report")
    legacy = report["schemaVersion"] == 2
    if legacy and (expected["occlusionEnabled"] or expected["occlusionCheck"] or
                   expected["labOccluders"] or expected["hzbDebugLevel"] >= 0):
        raise ValueError("schema 2 cannot report occlusion workloads")
    if report.get("interactive") is not False or report.get("scored") is not scored:
        raise ValueError("scoring/frontend mismatch")
    if report.get("pacing") != "serialized-retirement":
        raise ValueError("unexpected retirement pacing")
    actual_plan = report.get("plan")
    lighting_plan = lighting_report.comparable_plan(report["schemaVersion"], expected)
    comparison = {k: v for k, v in lighting_plan.items() if not legacy or k not in
                  ("occlusionEnabled", "occlusionCheck", "hzbDebugLevel", "labOccluders")}
    if actual_plan != comparison:
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
    if scored and any((key.startswith(("MTL_", "METAL_", "DYLD_")) or key in ("LMX_CAPTURE_AT_FRAME", "LMX_LIGHT_CHECK_DUMP"))
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
                    "rejected", "sceneCommands", "shadowCommands", "tableBytes", "listBytes", "reservedListBytes", "argumentBytes",
                    "transientBytes", "allocatedListBytes", "allocatedArgumentBytes", "candidateBytes",
                    "runBytes", "chunkBytes", "stateBytes", "counterBytes"):
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
        # Render/Passes/Temporal/Temporal.h orders Raw=0, NativeTaa=1, VendorTemporal=2.
        if sample["effectiveScale"] != 1 or sample["vendorFallback"] != 0 or sample["effectiveReconstruction"] != 1:
            raise ValueError("effective reconstruction differs from frozen Native TAA plan")
        if legacy:
            # Compatibility is derived from the immutable raw pass list; files stay untouched.
            for metric, prefix in (("hzbGpuMs", "lmx.pass.hzb."),
                                   ("sceneGpuMs", "lmx.pass.scene"),
                                   ("shadowGpuMs", "lmx.pass.shadow")):
                sample[metric] = sum(p.get("gpuMs", 0) for p in passes
                                     if isinstance(p, dict) and p.get("label", "").startswith(prefix)
                                     and p.get("label") != "lmx.pass.hzb.debug")
        for metric in METRICS:
            nonnegative_number(sample.get(metric), metric)
        for timing in passes:
            require_dict(timing, "pass timing")
            if not isinstance(timing.get("label"), str) or not timing["label"]:
                raise ValueError("invalid pass label")
            nonnegative_number(timing.get("gpuMs"), "pass GPU timing")
        if not math.isclose(sum(p["gpuMs"] for p in passes), sample["gpuSumMs"], rel_tol=1e-12, abs_tol=1e-12):
            raise ValueError("GPU sum differs from joined passes")
        if sample.get("effectiveSubmission") != expected["submission"]:
            raise ValueError("effective submission differs from request")
        if sample.get("effectiveClassify") != expected["classify"]:
            raise ValueError("effective classifier differs from request")
        visibility = require_dict(sample.get("visibility"), "visibility")
        if visibility.get("frameId") != frame_id or visibility.get("classify") != expected["classify"]:
            raise ValueError("visibility retirement frame/classifier mismatch")
        if expected["classify"] == "gpu" and visibility.get("retired") is not True:
            raise ValueError("missing retired GPU visibility join")
        if visibility.get("overflow") is not False or visibility.get("checkEnabled") is not False:
            raise ValueError("overflow or diagnostic check invalidates production measurement")
        for key in ("stateMismatches", "rowMismatches", "argumentMismatches", "counterMismatches"):
            nonnegative_integer(visibility.get(key), key)
            if visibility[key]:
                raise ValueError("visibility diagnostic mismatch")
        if legacy:
            visibility.update(occlusionEnabled=False, occlusionCheckEnabled=False,
                              occlusionHistoryValid=False, occlusionInvalidReason="Disabled",
                              occlusionSourceFrame=0, pyramidBytes=0)
        if (visibility.get("occlusionEnabled") is not expected["occlusionEnabled"] or
                visibility.get("occlusionCheckEnabled") is not False):
            raise ValueError("occlusion request or diagnostic mode differs")
        if not isinstance(visibility.get("occlusionHistoryValid"), bool) or not isinstance(visibility.get("occlusionInvalidReason"), str):
            raise ValueError("missing occlusion history facts")
        for key in ("occlusionSourceFrame", "pyramidBytes"):
            nonnegative_integer(visibility.get(key), key)
        for view in ("scene", "shadow"):
            counters = require_dict(visibility.get(view), view + " counters")
            if legacy:
                counters.update(occluded=0, occlusionTested=0, historyInvalid=0,
                                nearCrossing=0, outsideSource=0, rectTooLarge=0)
            for key in ("candidates", "visible", "rejected", "emittedRows", "emittedCommands",
                        "overflowedRows", "overflowedCommands", "occluded", "occlusionTested",
                        "historyInvalid", "nearCrossing", "outsideSource", "rectTooLarge"):
                nonnegative_integer(counters.get(key), key)
            bypassed = counters.get("bypassed")
            if not isinstance(bypassed, list) or len(bypassed) != 4:
                raise ValueError("missing bypass reason counters")
            for value in bypassed:
                nonnegative_integer(value, "bypass counter")
            retained = counters["visible"] + sum(bypassed)
            if (counters["candidates"] != retained + counters["rejected"] or
                    counters["emittedRows"] != retained or counters["overflowedRows"] or
                    counters["overflowedCommands"]):
                raise ValueError("visibility counters do not reconcile without overflow")
            if (counters["occluded"] > counters["rejected"] or
                    counters["occlusionTested"] < sum(counters[k] for k in
                        ("occluded", "nearCrossing", "outsideSource", "rectTooLarge"))):
                raise ValueError("occlusion counters do not reconcile")
        if visibility["scene"]["candidates"] != sample["candidates"]:
            raise ValueError("retired candidate count differs from declaration")
        if expected["classify"] == "gpu":
            payload = sum(visibility[view]["emittedRows"] for view in ("scene", "shadow")) * 4
            if sample["listBytes"] != payload or sample["reservedListBytes"] < payload:
                raise ValueError("retired valid row payload differs from emitted counters")
        visibility_sum = sum(p["gpuMs"] for p in passes if p["label"].startswith("lmx.pass.visibility."))
        if not math.isclose(visibility_sum, sample["visibilityGpuMs"], rel_tol=1e-12, abs_tol=1e-12):
            raise ValueError("visibility GPU scope differs from joined passes")
        for metric, prefix in (("hzbGpuMs", "lmx.pass.hzb."),
                               ("sceneGpuMs", "lmx.pass.scene"),
                               ("shadowGpuMs", "lmx.pass.shadow")):
            scope = sum(p["gpuMs"] for p in passes if p["label"].startswith(prefix)
                        and p["label"] != "lmx.pass.hzb.debug")
            if not math.isclose(scope, sample[metric], rel_tol=1e-12, abs_tol=1e-12):
                raise ValueError(metric + " differs from joined passes")
        if report["schemaVersion"] == 4:
            lighting_report.validate_sample(sample, comparison)
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
    visibility, submission, classify = mode[:3]
    occlusion = mode[3] if len(mode) == 4 else False
    return dict(warmupFrames=warmup, measuredFrames=frames, width=1280, height=720,
                labInstances=count, scene=scene, temporal="taa", submission=submission,
                visibilityEnabled=visibility, cameraTrack=track, renderScale=1,
                classify=classify, classifyCheck=False, occlusionEnabled=occlusion,
                occlusionCheck=False, hzbDebugLevel=-1,
                labOccluders=8 if workload.startswith("occluded-") else 0,
                stepSeconds=1 / 60)


def run_side(binary, report_path, plan, unscored, timeout):
    cmd = [str(binary), "--measure", str(report_path), "--scene", plan["scene"],
           "--frames", str(plan["measuredFrames"]), "--warmup", str(plan["warmupFrames"]),
           "--visibility", "cull" if plan["visibilityEnabled"] else "off",
           "--classify", plan["classify"], "--submission", plan["submission"], "--temporal", "taa", "--render-scale", "1",
           "--measure-camera", "track" if plan["cameraTrack"] else "initial"]
    if plan["scene"] == "visibility-lab":
        cmd += ["--lab-instances", str(plan["labInstances"])]
        if plan["labOccluders"]:
            cmd += ["--lab-occluders", str(plan["labOccluders"])]
    if plan["occlusionEnabled"]:
        cmd += ["--occlusion", "on"]
    if "localLightMode" in plan:
        cmd += ["--local-lights", plan["localLightMode"], "--light-view", "off"]
        if plan["scene"] == "sponza":
            cmd += ["--local-light-rig", "on" if plan["localLightRig"] else "off"]
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


def checked_provenance(args, reports):
    """Only the executable hash may differ in a parent/candidate binary control."""
    parent = getattr(args, "parent", None)
    normalized = []
    for side in (0, 1):
        report = reports[side]
        if parent and report.get("schemaVersion") != 4:
            raise ValueError("binary comparison requires schema 4 on both sides")
        provenance = copy.deepcopy(report["provenance"])
        expected_hash = args.parent_hash if parent and side == 0 else args.binary_hash
        if provenance.get("executableHash") != expected_hash:
            raise ValueError("executable changed since collection freeze")
        if parent:
            del provenance["executableHash"]
        normalized.append(provenance)
    if normalized[0] != normalized[1]:
        raise ValueError("runtime provenance changed between paired sides")
    if args.frozen_provenance is not None and normalized[0] != args.frozen_provenance:
        raise ValueError("runtime provenance differs across pairs or workload cells")
    return normalized[0]


def encode_regressions(cells):
    """List only complete cells whose baseline-minus-candidate CI is wholly negative."""
    return [dict(cell=name, metric="encodeMs", **cell["analysis"]["encodeMs"]["absolute"])
            for name, cell in cells.items() if cell["complete"] and
            cell["analysis"]["encodeMs"]["absolute"]["available"] and
            cell["analysis"]["encodeMs"]["absolute"]["ci95Ms"][1] < 0]


def collect_cell(args, workload, control, output):
    pairs = {metric: [] for metric in METRICS}
    failures = []
    observations = {"occluded": [], "rejected": [], "pyramidBytes": []}
    frozen_provenance = None
    for repetition in range(args.repetitions):
        order = (0, 1) if repetition % 2 == 0 else (1, 0)
        reports = {}
        for side in order:
            path = output / f"{workload}.{control}.rep{repetition:02d}.{'A' if side == 0 else 'B'}.json"
            plan = expected_plan(workload, CONTROLS[control][side], args.warmup, args.frames)
            parent = getattr(args, "parent", None)
            if parent:
                plan.update(lighting_report.DEFAULT_PLAN)
                plan["localLightRig"] = plan["scene"] == "sponza"
            binary = parent if parent and side == 0 else args.binary
            report, attempt = run_side(binary, path, plan, args.unscored, args.timeout)
            reports[side] = report
            if report is None:
                failures.append(dict(repetition=repetition, side=side, reason=attempt["failure"]))
        if any(report is None for report in reports.values()):
            continue
        try:
            provenance = checked_provenance(args, reports)
        except ValueError as exc:
            failures.append(dict(repetition=repetition, reason=str(exc)))
            continue
        frozen_provenance = provenance
        args.frozen_provenance = provenance
        for metric in METRICS:
            pairs[metric].append([statistics.median(sample[metric] for sample in reports[side]["samples"])
                                  for side in (0, 1)])
        for name in observations:
            observations[name].append([
                statistics.median(sample["visibility"]["scene"][name] if name != "pyramidBytes"
                                  else sample["visibility"][name] for sample in reports[side]["samples"])
                for side in (0, 1)])
        print(f"{workload}/{control} pair {repetition + 1}/{args.repetitions} order={order}", flush=True)
    complete = all(len(values) == args.repetitions for values in pairs.values())
    return {"complete": complete, "failures": failures, "provenance": frozen_provenance,
            "analysis": {metric: analyze_pairs(values) for metric, values in pairs.items()} if complete else {},
            "retainedPairs": pairs,
            "observations": {name: {
                "unit": "bytes" if name == "pyramidBytes" else "instances",
                "pairs": values,
                "absolute": {key.removesuffix("Ms"): value for key, value in
                             analyze_pairs(values)["absolute"].items()}}
                for name, values in observations.items()}}


def binary_selftest(template):
    """Exercise collection dispatch, report validation and the binary comparison guard."""
    import contextlib
    import io

    args = SimpleNamespace(parent=Path("/parent/App"), binary=Path("/candidate/App"),
                           parent_hash="a" * 64, binary_hash="c" * 64,
                           repetitions=2, warmup=32, frames=256, unscored=False,
                           timeout=10, frozen_provenance=None)
    calls, accepted = [], []

    def fake_run(binary, path, plan, unscored, timeout):
        calls.append((binary, copy.deepcopy(plan)))
        report = copy.deepcopy(template)
        report.update(schemaVersion=4, plan=copy.deepcopy(plan))
        report["provenance"]["executableHash"] = args.parent_hash if binary == args.parent else args.binary_hash
        first = report["samples"][0]
        first.update(effectiveSubmission=plan["submission"], encodeMs=1 if binary == args.parent else 2,
                     localLightMode=plan["localLightMode"], lightingGpuMs=0,
                     liveLightCount=16 if plan["localLightRig"] else 0)
        first["lighting"] = dict(frameId=1, sceneGeneration=1, requestedMode=plan["localLightMode"],
                                  effectiveMode="clustered" if first["liveLightCount"] else "off",
                                  liveLightCount=first["liveLightCount"], retired=True,
                                  counters={k: 0 for k in lighting_report.COUNTERS},
                                  listBytes=0, allocatedListBytes=0, checkEnabled=False,
                                  gridMismatches=0, indexMismatches=0, counterMismatches=0)
        report["samples"] = []
        for ordinal in range(plan["measuredFrames"]):
            sample = copy.deepcopy(first)
            sample.update(ordinal=ordinal, frameId=ordinal + 1,
                          sequenceFrame=plan["warmupFrames"] + ordinal)
            sample["visibility"]["frameId"] = sample["lighting"]["frameId"] = ordinal + 1
            report["samples"].append(sample)
        validate_report(report, plan, not unscored)
        accepted.append(copy.deepcopy(report))
        return report, {"valid": True}

    with tempfile.TemporaryDirectory() as tmp, mock.patch(__name__ + ".run_side", side_effect=fake_run), \
            contextlib.redirect_stdout(io.StringIO()):
        cells = {}
        for workload in BINARY_WORKLOADS:
            for control in BINARY_CONTROLS:
                calls.clear()
                cells[workload + "/" + control] = collect_cell(args, workload, control, Path(tmp))
                assert cells[workload + "/" + control]["complete"]
                assert [x[0] for x in calls] == [args.parent, args.binary, args.binary, args.parent]
                assert all(x[1] == calls[0][1] for x in calls)
                assert (calls[0][1]["warmupFrames"], calls[0][1]["measuredFrames"]) == (32, 256)
                assert calls[0][1]["submission"] == control.removeprefix("binary-")
        assert len(encode_regressions(cells)) == 6
        cell = copy.deepcopy(next(iter(cells.values())))
        cell["analysis"]["encodeMs"] = analyze_pairs([[2, 1]] * 12)
        assert not encode_regressions({"faster": cell})
        cell["analysis"]["encodeMs"] = analyze_pairs([[1, 1]] * 12)
        assert not encode_regressions({"equal": cell})
        with mock.patch(__name__ + ".run_side", return_value=(None, {"failure": "process failed"})):
            incomplete = collect_cell(args, "sponza", "binary-direct", Path(tmp))
            assert not incomplete["complete"] and not incomplete["analysis"]
            assert not encode_regressions({"incomplete": incomplete})
    pair = {0: accepted[0], 1: accepted[1]}
    for side, mutation in (
        (0, lambda r: r.update(schemaVersion=3)),
        (1, lambda r: r["provenance"].update(executableHash="d" * 64)),
        (1, lambda r: r["provenance"].update(device="different")),
        (1, lambda r: r["provenance"]["shaderHashes"].update(scene="e" * 64)),
        (1, lambda r: r["provenance"]["environment"].update(UNEXPECTED="1")),
    ):
        bad = copy.deepcopy(pair); mutation(bad[side])
        try:
            checked_provenance(args, bad)
        except ValueError:
            pass
        else:
            raise AssertionError("binary provenance drift accepted")
    for field in ("device", "os", "buildMode", "shaderHashes", "environment"):
        bad = copy.deepcopy(pair)
        for report in bad.values():
            value = report["provenance"][field]
            if isinstance(value, dict):
                value["unexpected"] = "f" * 64
            else:
                report["provenance"][field] = "different"
        try:
            checked_provenance(args, bad)
        except ValueError:
            pass
        else:
            raise AssertionError("cross-cell provenance drift accepted")


def selftest():
    assert analyze_pairs([[100, 80]] * 12)["ci95Pct"] == [20, 20]
    assert analyze_pairs([[100, 108]] * 12)["ci95Pct"] == [-8, -8]
    zero = analyze_pairs([[0, 0.25]] * 12)
    assert not zero["available"] and "zero baseline" in zero["reason"]
    assert zero["absolute"] == dict(available=True, baselineMedianMs=0,
                                    candidateMedianMs=0.25, pairedDeltasMs=[-0.25] * 12,
                                    medianDeltaMs=-0.25, ci95Ms=[-0.25, -0.25])
    mixed = analyze_pairs([[0, 1], [2, 1]] * 6)
    assert not mixed["available"]
    assert mixed["absolute"]["baselineMedianMs"] == mixed["absolute"]["candidateMedianMs"] == 1
    assert mixed["absolute"]["medianDeltaMs"] == 0
    assert mixed["absolute"]["ci95Ms"] == [-1, 1]
    assert analyze_pairs([[0, 0]] * 12)["absolute"]["ci95Ms"] == [0, 0]
    for invalid in ([], [[-1, 0]], [[1, float("nan")]]):
        assert not analyze_pairs(invalid)["absolute"]["available"]
    varied = [[100, 100 - delta] for delta in (3, -3, 2, -2, 1, -1, 0, 4, -4, 2, -2, 0)]
    assert analyze_pairs(varied)["ci95Pct"] == [-2.0, 2.0]
    assert analyze_pairs(varied)["absolute"]["ci95Ms"] == [-2, 2]
    plan = expected_plan("sponza", CONTROLS["cull-off"][0], 0, 1)
    provenance = dict(device="gpu", os="os", buildMode="release", executableHash="a" * 64,
                      shaderHashes={"scene": "b" * 64}, environment={
                          "MTL_DEBUG_LAYER": "", "MTL_CAPTURE_ENABLED": "",
                          "MTL_SHADER_VALIDATION": "", "LMX_CAPTURE_AT_FRAME": ""})
    sample = dict(ordinal=0, frameId=1, sequenceFrame=0, retired=True, candidates=2, visible=1, rejected=1,
                  passes=[dict(label="scene", gpuMs=1)], renderWidth=1280, renderHeight=720,
                  outputWidth=1280, outputHeight=720, effectiveScale=1, vendorFallback=0,
                  effectiveReconstruction=1, sceneCommands=1, shadowCommands=2, tableBytes=1024,
                  listBytes=8, reservedListBytes=8, argumentBytes=60, transientBytes=4096,
                  **{key: 1 for key in METRICS})
    sample.update(visibilityGpuMs=0, hzbGpuMs=0, sceneGpuMs=0, shadowGpuMs=0, effectiveClassify="cpu", effectiveSubmission="indirect", allocatedListBytes=24,
                  allocatedArgumentBytes=120, candidateBytes=0, runBytes=0, chunkBytes=0,
                  stateBytes=0, counterBytes=0,
                  visibility=dict(frameId=1, classify="cpu", retired=False, overflow=False,
                                  checkEnabled=False, stateMismatches=0, rowMismatches=0,
                                  argumentMismatches=0, counterMismatches=0, occlusionEnabled=False,
                                  occlusionCheckEnabled=False, occlusionHistoryValid=False,
                                  occlusionInvalidReason="Disabled", occlusionSourceFrame=0, pyramidBytes=0))
    for view in ("scene", "shadow"):
        sample["visibility"][view] = dict(candidates=2, visible=1, rejected=1,
                                        bypassed=[0, 0, 0, 0], emittedRows=1,
                                        emittedCommands=1, overflowedRows=0, overflowedCommands=0,
                                        occluded=0, occlusionTested=0, historyInvalid=0,
                                        nearCrossing=0, outsideSource=0, rectTooLarge=0)
    report = dict(schemaVersion=3, complete=True, scored=True, interactive=False,
                  pacing="serialized-retirement", plan=plan, provenance=provenance, samples=[sample])
    validate_report(report, plan, True)
    legacy = copy.deepcopy(report)
    legacy["schemaVersion"] = 2
    for key in ("occlusionEnabled", "occlusionCheck", "hzbDebugLevel", "labOccluders"):
        del legacy["plan"][key]
    validate_report(legacy, plan, True)
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
                     lambda r: r["samples"][0].pop("reservedListBytes"),
                     lambda r: r["samples"][0].update(tableBytes=True),
                     lambda r: r["samples"].__setitem__(0, []),
                     lambda r: r["samples"][0].update(passes=[[]]),
                     lambda r: r["samples"][0]["visibility"].update(frameId=9),
                     lambda r: r["samples"][0]["visibility"].update(overflow=True),
                     lambda r: r["samples"][0]["visibility"].update(checkEnabled=True),
                     lambda r: r["samples"][0]["visibility"].update(rowMismatches=1),
                     lambda r: r["samples"][0]["visibility"]["scene"].update(emittedRows=9)):

        bad = copy.deepcopy(report)
        mutation(bad)
        try:
            validate_report(bad, plan, True)
        except ValueError:
            pass
        else:
            raise AssertionError("invalid report accepted")
    for control in ("gpu-cpu-indirect", "gpu-cpu-batched"):
        gpu_plan = expected_plan("sponza", CONTROLS[control][1], 0, 1)
        gpu = copy.deepcopy(report)
        gpu["plan"] = gpu_plan
        gpu_sample = gpu["samples"][0]
        gpu_sample.update(effectiveClassify="gpu", effectiveSubmission=gpu_plan["submission"],
                          visibilityGpuMs=0.25, gpuSumMs=1.25, reservedListBytes=16)
        gpu_sample["passes"].append(dict(label="lmx.pass.visibility.classify", gpuMs=0.25))
        gpu_sample["visibility"].update(classify="gpu", retired=True)
        validate_report(gpu, gpu_plan, True)
        malformed = copy.deepcopy(gpu)
        malformed["samples"][0]["listBytes"] = 16
        try:
            validate_report(malformed, gpu_plan, True)
        except ValueError:
            pass
        else:
            raise AssertionError("candidate reservation accepted as valid row payload")
        gpu_sample["visibility"]["retired"] = False
        try:
            validate_report(gpu, gpu_plan, True)
        except ValueError:
            pass
        else:
            raise AssertionError("pending GPU counters accepted")
    assert len(DEFAULT_WORKLOADS) == 6
    assert expected_plan("sponza", CONTROLS["cull-off"][0], 32, 256)["labOccluders"] == 0
    for control in ("occlusion-on-off-indirect", "occlusion-on-off-batched"):
        before = expected_plan("occluded-1024", CONTROLS[control][0], 32, 256)
        after = expected_plan("occluded-1024", CONTROLS[control][1], 32, 256)
        assert before["labOccluders"] == after["labOccluders"] == 8
        assert before["occlusionEnabled"] is False and after["occlusionEnabled"] is True
        assert before["classify"] == after["classify"] == "gpu"
    for key in ("hzbGpuMs", "sceneGpuMs", "shadowGpuMs"):
        bad = copy.deepcopy(report)
        bad["samples"][0][key] = 0.5
        try:
            validate_report(bad, plan, True)
        except ValueError:
            pass
        else:
            raise AssertionError("unmatched independent GPU scope accepted")
    assert expected_plan("san-miguel", (True, "direct", "cpu"), 32, 256)["cameraTrack"] is False
    assert [((0, 1) if rep % 2 == 0 else (1, 0)) for rep in range(4)] == [(0, 1), (1, 0), (0, 1), (1, 0)]
    for text, allowed in (("sponza,sponza", WORKLOADS), ("cull-off,cull-off", CONTROLS)):
        try:
            unique_selection(text, allowed, "selector")
        except ValueError:
            pass
        else:
            raise AssertionError("duplicate selector accepted")
    assert unique_selection("sponza,san-miguel", WORKLOADS, "workload") == ["sponza", "san-miguel"]
    binary_selftest(report)
    print("visibility_paired.py --selftest: all checks passed")
    return 0


def main():
    if "--selftest" in sys.argv:
        return selftest()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--parent", type=Path, help="frozen parent App for same-plan binary controls")
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--repetitions", default=12, type=int)
    parser.add_argument("--warmup", default=32, type=int)
    parser.add_argument("--frames", default=256, type=int)
    parser.add_argument("--workloads")
    parser.add_argument("--controls")
    parser.add_argument("--unscored", action="store_true")
    parser.add_argument("--timeout", default=1200, type=int)
    args = parser.parse_args()
    args.binary, args.out = args.binary.resolve(), args.out.resolve()
    args.parent = args.parent.resolve() if args.parent else None
    if args.workloads is None:
        args.workloads = ",".join(BINARY_WORKLOADS if args.parent else DEFAULT_WORKLOADS)
    if args.controls is None:
        args.controls = ",".join(BINARY_CONTROLS if args.parent else DEFAULT_CONTROLS)
    try:
        workloads = unique_selection(args.workloads, WORKLOADS, "workload")
        controls = unique_selection(args.controls, CONTROLS, "control")
    except ValueError as exc:
        parser.error(str(exc))
    if any((control in BINARY_CONTROLS) != bool(args.parent) for control in controls):
        parser.error("binary controls require --parent; --parent accepts only binary controls")
    if args.parent and not args.parent.is_file():
        parser.error("parent binary must exist")
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
    args.parent_hash = hashlib.sha256(args.parent.read_bytes()).hexdigest() if args.parent else None
    args.frozen_provenance = None
    collection = dict(schemaVersion=3, scoredProtocol=frozen and not args.unscored,
                      binary=str(args.binary), binarySha256=args.binary_hash,
                      seed=SEED, resamples=RESAMPLES, confidence=0.95,
                      direction="positive means candidate costs less", adoptionRule=None,
                      pacing="serialized-retirement; does not measure throughput",
                      cpuCommandScope="GPU indirect: one command per candidate slot; GPU batched: one per run, including empty slots/runs",
                      repetitions=args.repetitions, warmup=args.warmup, frames=args.frames,
                      environment={k: v for k, v in os.environ.items() if k.startswith(("MTL_", "METAL_", "DYLD_", "LMX_"))},
                      cells={})
    if args.parent:
        collection.update(parent=str(args.parent), parentSha256=args.parent_hash,
                          comparison="same settings, parent A and candidate B",
                          regressionMetric="encodeMs", regressions=[],
                          cpuCommandScope="CPU classification; identical submission mode on both binaries")
    destination = args.out / "analysis.json"
    for workload in workloads:
        for control in controls:
            collection["cells"][workload + "/" + control] = collect_cell(args, workload, control, raw)
            collection["complete"] = len(collection["cells"]) == len(workloads) * len(controls) and all(
                cell["complete"] for cell in collection["cells"].values())
            if args.parent:
                collection["regressions"] = encode_regressions(collection["cells"])
            destination.write_text(json.dumps(collection, indent=2) + "\n")
    complete = all(cell["complete"] for cell in collection["cells"].values())
    print(f"Wrote {destination}; complete={complete}")
    if args.parent:
        print("encodeMs regressions: " + ", ".join(row["cell"] for row in collection["regressions"]))
    return 0 if complete else 1


if __name__ == "__main__":
    raise SystemExit(main())
