#!/usr/bin/env python3
"""Launch independent SDR and EDR editor probes, retaining logs and float capture sidecars."""
import argparse
import hashlib
import json
import os
import plistlib
import tempfile
from pathlib import Path
import shutil
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--output", type=Path, required=True)
parser.add_argument("--app", type=Path, default=Path(__file__).resolve().parents[2] / "build/macosx/arm64/release/App")
args = parser.parse_args()
output, app = args.output.resolve(), args.app.resolve()
if output.exists() and any(output.iterdir()):
    parser.error("output must be new or empty")
if not app.is_file() or not (app.parent / "Shaders").is_dir():
    parser.error("build App before launching the probe")
output.mkdir(parents=True, exist_ok=True)
environment = os.environ.copy()
environment["MTL_DEBUG_LAYER"] = "1"
environment["LMX_EDR_PROBE_SIDE"] = "1"
for key in ("LMX_MAX_FRAMES", "LMX_EDR_PROBE", "LMX_DYNAMIC_RESOLUTION_BUDGET_MS", "LMX_SCREENSHOT_NO_BLOOM"):
    environment.pop(key, None)
runtime_root = Path(tempfile.mkdtemp(prefix="luminex-edr-", dir="/tmp"))
records = []
for mode in ("sdr", "edr"):
    capture = output / mode
    capture.mkdir()
    bundle = runtime_root / ("Luminex " + mode.upper() + " Probe.app")
    runtime = bundle / "Contents/MacOS"
    runtime.mkdir(parents=True)
    executable = "Luminex" + mode.upper() + "Probe"
    shutil.copy2(app, runtime / executable)
    shutil.copytree(app.parent / "Shaders", runtime / "Shaders")
    (bundle / "Contents/Info.plist").write_bytes(plistlib.dumps({
        "CFBundleExecutable": executable, "CFBundleIdentifier": "dev.luminex.probe." + mode,
        "CFBundleName": "Luminex " + mode.upper() + " Probe", "CFBundlePackageType": "APPL",
        "NSHighResolutionCapable": True}))
    environment["LMX_EDR_CAPTURE_DIR"] = str(capture)
    command = [str(runtime / executable), "--windowed", "--scene", "material-lab", "--calibration"]
    if mode == "edr":
        command.append("--edr")
    with (capture / "probe.log").open("w") as log:
        process = subprocess.Popen(command, cwd=runtime, env=environment, stdout=log,
                                   stderr=subprocess.STDOUT, start_new_session=True)
    records.append({"view": mode, "pid": process.pid, "command": command, "cwd": str(runtime), "bundle": str(bundle), "captures": str(capture)})
    print(mode, process.pid, capture / "probe.log", flush=True)
(output / "launch.json").write_text(json.dumps({"appSha256": hashlib.sha256(app.read_bytes()).hexdigest(),
    "environment": {"MTL_DEBUG_LAYER": "1"}, "runs": records}, indent=2) + "\n")
