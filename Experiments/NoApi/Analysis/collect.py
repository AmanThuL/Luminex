#!/usr/bin/env python3
"""M5.1 Stage 4 collection driver (spec section 8, plan Stage 4 item 5).

Runs the frozen paired-repetition protocol for one or more timed workloads (graph, bind1024,
bind4096) by launching `NoApiBench --measure` twice per repetition -- once per adapter, in AB
order on even repetition index and BA order on odd -- assembling the resulting per-repetition
medians into paired_bootstrap.py's input schema, and running that frozen decision statistic.
Writes every raw per-run JSON NoApiBench produced, the assembled pairs, the bootstrap analysis,
and the recorded environment to --out.

Per spec section 8: "one repetition = one fresh process executing both encoders back to back" --
NoApiBench's own process boundary is that fresh process; this script launches it twice per
repetition (once per adapter), which is the two-process reading of that sentence this milestone's
harness uses (a single NoApiBench invocation runs exactly one adapter). "16 warm-up frames then
256 measured frames" is `--warmup`/`--frames` on each invocation; NoApiBench itself computes and
reports the per-repetition median (spec: "the repetition's statistic is the median per-frame
value").

Validation must be off for every measured run (NoApiBench's own --measure refuses otherwise);
this script does not set or touch MTL_DEBUG_LAYER, and unsets it in the child's environment so an
inherited debug session can never silently corrupt a collection.

Output directory: defaults to a path outside the repository tree (docs/conventions/documentation.md:
"Keep raw captures, temporary measurements, and recovery bundles outside the published source
tree"). Pass --out to choose another location; this script only ever refuses to default it inside
the repo, never a caller-chosen path.

Stdlib only (Tools/*.py convention; no third-party JSON/stat libraries).

Usage:
    python3 collect.py --bench <path/to/NoApiBench> [--out DIR] [--repetitions 12]
                        [--workloads graph,bind1024,bind4096] [--warmup 16] [--frames 256]

Exit codes: 0 every requested workload's collection and analysis completed; 1 a NoApiBench
invocation failed (nonzero exit or unparseable JSON) or paired_bootstrap.py failed.
"""
import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

_REPO_ROOT = Path(__file__).resolve().parents[3]
_ANALYSIS_DIR = Path(__file__).resolve().parent
_DEFAULT_WORKLOADS = ["graph", "bind1024", "bind4096"]
_DEFAULT_REPETITIONS = 12
_DEFAULT_WARMUP = 16
_DEFAULT_FRAMES = 256
_ADAPTERS = ("rhi", "noapi")  # incumbent, prototype -- paired_bootstrap.py's [incumbent, prototype].


def _default_out() -> Path:
    # Outside the repo tree by construction: a sibling of the repo root itself, never a subpath of
    # it, so a caller who never passes --out cannot accidentally publish raw captures.
    return _REPO_ROOT.parent / "lmx-m5.1-measurements"


def run_bench(bench: Path, workload: str, adapter: str, warmup: int, frames: int,
              json_path: Path) -> dict:
    """Runs one `NoApiBench --measure` invocation and returns its parsed JSON blob.

    Raises RuntimeError on a nonzero exit or unparseable output -- a collection run that produced
    no usable measurement must stop, not silently drop a repetition.
    """
    env = dict(os.environ)
    env.pop("MTL_DEBUG_LAYER", None)
    cmd = [str(bench), f"--measure={workload}", f"--adapter={adapter}", f"--warmup={warmup}",
           f"--frames={frames}", f"--json={json_path}"]
    # NoApiBench resolves shaders relative to its own working directory (AGENTS.md gotcha).
    proc = subprocess.run(cmd, cwd=bench.parent, env=env, capture_output=True, text=True)
    if proc.returncode != 0:
        raise RuntimeError(
            f"NoApiBench --measure={workload} --adapter={adapter} exited {proc.returncode}\n"
            f"stdout:\n{proc.stdout}\nstderr:\n{proc.stderr}")
    try:
        return json.loads(json_path.read_text())
    except (OSError, json.JSONDecodeError) as exc:
        raise RuntimeError(f"could not read/parse {json_path}: {exc}\nstdout:\n{proc.stdout}")


def collect_workload(bench: Path, workload: str, repetitions: int, warmup: int, frames: int,
                      raw_dir: Path) -> list[list[float]]:
    """Runs `repetitions` paired repetitions of `workload`, returning [[incumbentNs, prototypeNs], ...].

    Repetition parity decides launch order only (AB on even index, BA on odd); the measurement
    itself -- each adapter's own process, own setup, own warm-up -- is identical either way, which
    is what makes the order swap a check against scheduling/thermal drift rather than a second
    experimental variable.
    """
    pairs: list[list[float]] = []
    for repetition in range(repetitions):
        order = _ADAPTERS if repetition % 2 == 0 else tuple(reversed(_ADAPTERS))
        medians: dict[str, float] = {}
        for adapter in order:
            json_path = raw_dir / f"{workload}.{adapter}.rep{repetition:02d}.json"
            blob = run_bench(bench, workload, adapter, warmup, frames, json_path)
            medians[adapter] = blob["medianNs"]
            if not blob.get("countersStableAcrossFrames", True):
                print(f"WARNING: {workload}/{adapter} repetition {repetition}: binding counters "
                      f"varied across measured frames (reported, not silently accepted)",
                      file=sys.stderr)
        pairs.append([medians["rhi"], medians["noapi"]])
        print(f"{workload} repetition {repetition:02d} order={''.join(order)}: "
              f"rhi={medians['rhi']:.0f}ns noapi={medians['noapi']:.0f}ns")
    return pairs


def collect_environment(out_dir: Path) -> None:
    """Records the section 8 environment block: hardware, OS/toolchain versions, power/thermal
    state, and the exact source commit -- every scored run must have this recorded (spec section
    8's "Environment" bullet)."""
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
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--bench", required=True, type=Path,
                        help="path to the built NoApiBench binary")
    parser.add_argument("--out", type=Path, default=_default_out(),
                        help="output directory (default: outside the repo tree)")
    parser.add_argument("--repetitions", type=int, default=_DEFAULT_REPETITIONS,
                        help=f"paired repetitions per workload (spec: exactly {_DEFAULT_REPETITIONS} "
                             "for a scored round; pass a smaller number for an explicitly unscored "
                             "trial)")
    parser.add_argument("--warmup", type=int, default=_DEFAULT_WARMUP)
    parser.add_argument("--frames", type=int, default=_DEFAULT_FRAMES)
    parser.add_argument("--workloads", default=",".join(_DEFAULT_WORKLOADS),
                        help="comma-separated workload list")
    parser.add_argument("--label", default="analysis",
                        help="basename for the written analysis JSON (e.g. 'trial' vs 'scored')")
    args = parser.parse_args()

    args.bench = args.bench.resolve()
    if not args.bench.is_file():
        print(f"collect.py: --bench '{args.bench}' does not exist or is not a file",
              file=sys.stderr)
        return 1
    if args.repetitions != _DEFAULT_REPETITIONS:
        print(f"NOTE: --repetitions={args.repetitions} != the spec's frozen {_DEFAULT_REPETITIONS} "
              "-- this run's results are UNSCORED and must not feed the ADR (spec section 2).",
              file=sys.stderr)

    raw_dir = args.out / "raw"
    raw_dir.mkdir(parents=True, exist_ok=True)
    collect_environment(args.out)

    workloads = [w.strip() for w in args.workloads.split(",") if w.strip()]
    metrics = {}
    for workload in workloads:
        pairs = collect_workload(args.bench, workload, args.repetitions, args.warmup, args.frames,
                                 raw_dir)
        metrics[f"{workload}.medianNs"] = {"pairs": pairs}

    bootstrap_input = args.out / f"{args.label}.bootstrap_input.json"
    bootstrap_input.write_text(json.dumps({"metrics": metrics}, indent=2) + "\n")

    bootstrap_script = _ANALYSIS_DIR / "paired_bootstrap.py"
    proc = subprocess.run([sys.executable, str(bootstrap_script), str(bootstrap_input)],
                          capture_output=True, text=True)
    if proc.returncode != 0:
        print(f"collect.py: paired_bootstrap.py failed:\n{proc.stderr}", file=sys.stderr)
        return 1

    analysis_path = args.out / f"{args.label}.json"
    analysis_path.write_text(proc.stdout)
    print(f"\nWrote raw per-run JSONs to {raw_dir}")
    print(f"Wrote bootstrap input to {bootstrap_input}")
    print(f"Wrote analysis to {analysis_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
