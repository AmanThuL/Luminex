"""Reproduce bounded native interop compiler probes with an externally supplied pinned toolchain."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

p = argparse.ArgumentParser()
p.add_argument("--slangc", type=Path, required=True)
p.add_argument("--output", type=Path, required=True)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=True)
source = Path(__file__).resolve().parent
results = []

def run(argv, name):
    result = subprocess.run(list(map(str, argv)), text=True, capture_output=True)
    (a.output / (name + ".log")).write_text("COMMAND " + " ".join(map(str, argv)) + "\n" + result.stdout + result.stderr + "\nEXIT " + str(result.returncode) + "\n")
    results.append({"name": name, "command": list(map(str, argv)), "exit": result.returncode})
    return result.returncode

run([a.slangc, "-version"], "slang-version")
run(["xcrun", "-sdk", "macosx", "metal", "--version"], "metal-version")
run(["sw_vers"], "os")
run(["xcodebuild", "-version"], "xcode-version")
for stem in ["Probe", "ProbeBridge", "ProbePrelude", "Textures"]:
    metal = a.output / (stem + ".metal")
    run([a.slangc, source / (stem + ".slang"), "-target", "metal", "-o", metal], stem + "-slang")
    run(["xcrun", "-sdk", "macosx", "metal", "-std=metal4.0", "-c", metal, "-o", a.output / (stem + ".air")], stem + "-metal")
(a.output / "compile-results.json").write_text(json.dumps(results, indent=2) + "\n")
(a.output / "slangc.sha256").write_text(hashlib.sha256(a.slangc.read_bytes()).hexdigest() + "\n")
expected = {"Probe-metal": 1, "ProbeBridge-metal": 1}
assert all(r["exit"] == expected.get(r["name"], 0) for r in results), results
print("Expected matrix reproduced: Slang 4/4; Metal 2/4, with native-prelude and texture arrays passing.")
