"""Build the standalone Metal 4 runtime probe; run only in an exclusive GPU window."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

p = argparse.ArgumentParser()
p.add_argument("--slangc", type=Path, required=True)
p.add_argument("--metal-cpp", type=Path, required=True)
p.add_argument("--output", type=Path, required=True)
p.add_argument("--frames", type=int, default=1)
p.add_argument("--capture", action="store_true")
p.add_argument("--build-only", action="store_true")
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=True)
source = Path(__file__).resolve().parent
commands = []

def run(argv, name, env=None):
    result = subprocess.run(list(map(str, argv)), text=True, capture_output=True, env=env, timeout=60)
    (a.output / (name + ".log")).write_text("COMMAND " + " ".join(map(str, argv)) + "\n" + result.stdout + result.stderr + "\nEXIT " + str(result.returncode) + "\n")
    commands.append({"name": name, "command": list(map(str, argv)), "exit": result.returncode})
    (a.output / "commands.json").write_text(json.dumps(commands, indent=2) + "\n")
    print(name, result.returncode, result.stdout, flush=True)
    if result.returncode:
        print(result.stderr)
        raise SystemExit(result.returncode)

for name in ["Generate", "ArrayDraw", "FixedDraw"]:
    metal = a.output / (name + ".metal")
    run([a.slangc, source / (name + ".slang"), "-target", "metal", "-o", metal], name + "-slang")
    run(["xcrun", "-sdk", "macosx", "metal", "-std=metal4.0", "-frecord-sources", "-gline-tables-only", metal, "-o", a.output / (name + ".metallib")], name + "-metal")
run(["xcrun", "clang++", "-std=c++23", "-fblocks", "-I", a.metal_cpp, source / "Runtime.cpp", "-framework", "Foundation", "-framework", "Metal", "-framework", "QuartzCore", "-o", a.output / "Runtime"], "host-build")
if not a.build_only:
    env = os.environ.copy()
    env["MTL_DEBUG_LAYER"] = "1"
    if a.capture:
        env["MTL_CAPTURE_ENABLED"] = "1"
    else:
        env.pop("MTL_CAPTURE_ENABLED", None)
    (a.output / "environment.json").write_text(json.dumps({k: v for k, v in env.items() if k.startswith(("MTL_", "METAL_"))}, indent=2) + "\n")
    command = [a.output / "Runtime", a.output, a.output / "images", str(a.frames)]
    if a.capture:
        command.append(a.output / "indexed.gputrace")
    run(command, "runtime", env)
manifest = {str(f.relative_to(a.output)): hashlib.sha256(f.read_bytes()).hexdigest() for f in sorted(a.output.rglob("*")) if f.is_file() and f.name != "sha256.json"}
(a.output / "sha256.json").write_text(json.dumps(manifest, indent=2) + "\n")
