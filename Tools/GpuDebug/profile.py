#!/usr/bin/env python3
"""Record a Metal System Trace of the built App under `xcrun xctrace`, and turn its per-encoder
timings into timings.json.

Drives the App binary directly (LMX_MAX_FRAMES bounds the run so it exits on its own; no
`--screenshot` is used, so a real window opens for the duration of the recording), then exports
and parses the `metal-application-encoders-list` table documented by xctracelib.py.

Usage: python3 profile.py [--app <path>] [--scene <scene-id>] [--frames 300] [--out <dir>]
  --app     defaults to the newest build/macosx/*/*/App under the repo root (build one with
            `xmake` first if none exists)
  --scene   stable scene ID; defaults to "sponza"
  --frames  LMX_MAX_FRAMES for the recorded run (default 300)
  --out     output directory for profile.trace, the exported encoder-intervals XML, and
            timings.json (default ./lmx-profile)

Exit codes: 0 success. 2 no App binary found. 3 an xctrace/xcrun step itself failed (missing
xcrun, `record`/`export` returned nonzero, either step timed out -- see XctraceTimeoutError,
_RECORD_TIMEOUT_SECONDS/_EXPORT_TIMEOUT_SECONDS -- or the expected table is absent from this
trace; see xctracelib.ENCODER_TABLE_SCHEMA). 4 the recording completed and parsed
cleanly but zero encoder labels contained "lmx." (anomaly `no-encoders-matched`) -- a profile that
saw none of our own passes is a failed profile, not a partial one.

`encoders` in timings.json is keyed by the RHI render-pass label. Repeated invocations of the same
pass intentionally aggregate into one bucket.

Uses stdlib `xml.etree.ElementTree` (see xctracelib.py's module docstring for why, given that
module's XXE caveat): the only XML here is `xctrace export`'s stdout from a subprocess this same
process just launched locally, never untrusted/network input.
"""
import argparse
import json
import pathlib
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET

import xctracelib

_DEFAULT_SCENE = "sponza"
_DEFAULT_FRAMES = 300
_XCODE_SELECT_HINT = ("xctrace needs full Xcode (not just the Command Line Tools): "
                      "xcode-select -s /Applications/Xcode.app, or install Xcode from the "
                      "App Store, then retry")

# A wedged `xctrace record` can otherwise hang indefinitely when a modal dialog steals focus or
# Instruments is already recording. `record` gets ten minutes so a large cold scene has margin;
# the two `export` steps, which do no rendering, get two.
_RECORD_TIMEOUT_SECONDS = 600
_EXPORT_TIMEOUT_SECONDS = 120

_ENCODERS_NOTE = (
    "Buckets are keyed by the subsystem-qualified RHI pass label. Repeated invocations of one "
    "label aggregate; lmx.pass.shadow, lmx.pass.scene, and lmx.pass.ui remain distinct."
)


class XctraceTimeoutError(RuntimeError):
    """A `_run` subprocess call exceeded its timeout. Carries which xctrace step wedged in the
    message -- a bare `subprocess.TimeoutExpired` only carries the argv, and "record" vs. "export"
    timing out point at different problems (a hung App/dialog vs. a huge/corrupt trace)."""


def _default_app_binary(repo_root: pathlib.Path):
    """Newest build/macosx/*/*/App under `repo_root`, by mtime. This glob already excludes the
    Tests binary layout (build/macosx/<arch>/<mode>/test/Tests -- a different filename one
    directory deeper), so no extra filtering is needed."""
    matches = list(repo_root.glob("build/macosx/*/*/App"))
    if not matches:
        return None
    return max(matches, key=lambda p: p.stat().st_mtime)


def _run(cmd: list, step: str, timeout: float, cwd=None):
    try:
        return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired as exc:
        raise XctraceTimeoutError(
            f"{step} timed out after {timeout:g}s with no output -- xctrace may be wedged; check "
            "Instruments isn't already recording, and that the App isn't stuck on a modal dialog "
            "or a debugger") from exc


def _record(app_path: pathlib.Path, scene: str, frames: int, trace_path: pathlib.Path):
    """`xcrun xctrace record`, launching `app_path` with LMX_MAX_FRAMES=`frames` so the App exits
    on its own (no --time-limit needed). Run with cwd=app_path.parent: xctrace launches the
    target from the CURRENT working directory, and the App resolves its shaders relative to CWD
    (the same rule `xmake run App` and the screenshot path already follow)."""
    if trace_path.exists():
        shutil.rmtree(trace_path)
    cmd = ["xcrun", "xctrace", "record", "--template", "Metal System Trace",
           "--output", str(trace_path), "--env", f"LMX_MAX_FRAMES={frames}",
           "--env", "MTL_DEBUG_LAYER=0", "--launch", "--", str(app_path), "--scene", scene]
    return _run(cmd, step="xctrace record", timeout=_RECORD_TIMEOUT_SECONDS, cwd=app_path.parent)


def _toc_schemas(trace_path: pathlib.Path):
    """The set of table `schema` names this trace actually has, via `xctrace export --toc`."""
    result = _run(["xcrun", "xctrace", "export", "--input", str(trace_path), "--toc"],
                  step="xctrace export --toc", timeout=_EXPORT_TIMEOUT_SECONDS)
    if result.returncode != 0:
        return None, result
    root = ET.fromstring(result.stdout)
    return {table.get("schema") for table in root.iter("table") if table.get("schema")}, result


def _export_table(trace_path: pathlib.Path, schema: str):
    xpath = f'/trace-toc/run[@number="1"]/data/table[@schema="{schema}"]'
    return _run(["xcrun", "xctrace", "export", "--input", str(trace_path), "--xpath", xpath],
               step=f"xctrace export (table {schema})", timeout=_EXPORT_TIMEOUT_SECONDS)


def _build_timings(app_path, scene: str, frames: int, encoders: dict, anomalies: list) -> dict:
    """Assemble the timings.json document. Factored out of main() so its shape -- in particular
    `encodersNote` -- is unit-testable without mocking
    subprocess/xctrace (see tests/test_profile.py)."""
    # Approximate only: divide aggregate encoder time by the requested frame count rather than
    # reconstructing frame boundaries from interval timestamps.
    frame_total_ms_approx = (round(sum(stats["totalMs"] for stats in encoders.values()) / frames, 3)
                             if encoders else None)
    return {
        "version": 1,
        "app": str(app_path),
        "scene": scene,
        "frames": frames,
        "encoders": encoders,
        "encodersNote": _ENCODERS_NOTE,
        "frameTotalMsApprox": frame_total_ms_approx,
        "frameTotalMsApproxNote": "sum(lmx.* encoders' totalMs) / frames (requested LMX_MAX_FRAMES)",
        "anomalies": anomalies,
    }


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--app", type=pathlib.Path, default=None,
                        help="App binary to profile (default: newest build/macosx/*/*/App)")
    parser.add_argument("--scene", default=_DEFAULT_SCENE,
                        help=f"stable scene ID to profile (default: {_DEFAULT_SCENE})")
    parser.add_argument("--frames", type=int, default=_DEFAULT_FRAMES,
                        help=f"LMX_MAX_FRAMES for the recorded run (default: {_DEFAULT_FRAMES})")
    parser.add_argument("--out", type=pathlib.Path, default=pathlib.Path("lmx-profile"),
                        help="output directory (default: ./lmx-profile)")
    args = parser.parse_args(argv)

    if shutil.which("xcrun") is None:
        print(f"error: {_XCODE_SELECT_HINT}", file=sys.stderr)
        return 3

    repo_root = pathlib.Path(__file__).resolve().parents[2]
    app_path = args.app if args.app is not None else _default_app_binary(repo_root)
    if app_path is None or not app_path.is_file():
        print(f"error: no App binary found (looked for {repo_root}/build/macosx/*/*/App); "
              "build it with `xmake` first, or pass --app <path>", file=sys.stderr)
        return 2
    app_path = app_path.resolve()

    out_dir = args.out.resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    trace_path = out_dir / "profile.trace"

    try:
        record_result = _record(app_path, args.scene, args.frames, trace_path)
        if record_result.returncode != 0:
            print(f"error: xctrace record failed (exit {record_result.returncode}):\n"
                  f"{record_result.stderr}", file=sys.stderr)
            return 3
        print(f"recorded {trace_path}")

        present_schemas, toc_result = _toc_schemas(trace_path)
        if present_schemas is None:
            print(f"error: xctrace export --toc failed (exit {toc_result.returncode}):\n"
                  f"{toc_result.stderr}", file=sys.stderr)
            return 3
        if xctracelib.ENCODER_TABLE_SCHEMA not in present_schemas:
            # xctrace schema/table names can drift across Xcode versions. Name every table the
            # trace has rather than fail with a bare "table not found", so a
            # reader can spot the renamed/replacement schema without re-running xctrace by hand.
            print(f"error: table {xctracelib.ENCODER_TABLE_SCHEMA!r} not found in this trace "
                  "(Xcode version drift? see xctracelib.py's module docstring). Tables present: "
                  f"{', '.join(sorted(present_schemas))}", file=sys.stderr)
            return 3

        export_result = _export_table(trace_path, xctracelib.ENCODER_TABLE_SCHEMA)
        if export_result.returncode != 0:
            print(f"error: xctrace export of {xctracelib.ENCODER_TABLE_SCHEMA} failed "
                  f"(exit {export_result.returncode}):\n{export_result.stderr}", file=sys.stderr)
            return 3
    except XctraceTimeoutError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 3

    xml_path = out_dir / "encoder-intervals.xml"
    xml_path.write_text(export_result.stdout)

    resolved = xctracelib.resolve_refs(export_result.stdout)
    intervals = xctracelib.encoder_intervals(resolved)
    full_aggregate = xctracelib.aggregate(intervals)
    encoders = {label: stats for label, stats in full_aggregate.items()
               if xctracelib.LMX_LABEL_MARKER in label}
    anomalies = xctracelib.check_anomalies(full_aggregate)

    timings = _build_timings(app_path, args.scene, args.frames, encoders, anomalies)
    timings_path = out_dir / "timings.json"
    timings_path.write_text(json.dumps(timings, indent=2) + "\n")
    print(f"wrote {timings_path}")

    counts = {severity: sum(1 for a in anomalies if a["severity"] == severity)
             for severity in ("error", "warning", "info")}
    print(f"anomalies: {counts['error']} error, {counts['warning']} warning, "
          f"{counts['info']} info")

    if any(a["check"] == "no-encoders-matched" for a in anomalies):
        return 4
    return 0


if __name__ == "__main__":
    sys.exit(main())
