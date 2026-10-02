#!/usr/bin/env python3
"""Exercise every read-only query and every higher-tier refusal in a real editor."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time


ROOT = Path(__file__).resolve().parents[2]
CLIENT = ROOT / "Tools/Session/lmx_session.py"
BUILD = ROOT / "build/macosx/arm64/release"
QUERIES = ("status", "hierarchy", "selection", "camera", "settings", "readings",
           "performance", "graph", "console", "proposals", "log")
REFUSALS = (
    ("propose", "edits", "--summary", "Check", "--edit", "object:0", "enabled", "false"),
    ("propose", "withdraw", "1"),
    ("settings", "set", "temporal", "off"),
    ("debugview", "set", "final"),
    ("scene", "open", "temporal-lab"),
    ("measure", "run", "0", "1", "check.json"),
    ("capture", "gpu"),
    ("capture", "screenshot", "check", "1"),
    ("capture", "sequence", "check", "1", "0"),
    ("graph", "dump", "check.txt"),
    ("plan", "submit"),
)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", type=Path, default=BUILD / "App")
    parser.add_argument("--max-frames", type=int, default=900)
    parser.add_argument("--app-log", type=Path)
    args = parser.parse_args()
    if args.max_frames <= 0:
        parser.error("--max-frames must be positive")
    results = {"queries": {}, "refusals": {}, "graph": "not run"}
    with tempfile.TemporaryDirectory(prefix="lmx-session-check-") as temp:
        socket_path = Path(temp) / "luminex-session-unknown.sock"
        plan = Path(temp) / "plan.json"
        plan.write_text('{"summary":"Check","steps":[{"command":"settings.set",'
                        '"args":{"temporal":"off"}}]}', encoding="utf-8")
        env = dict(os.environ, TMPDIR=temp, LMX_MAX_FRAMES=str(args.max_frames))
        if args.app_log:
            args.app_log.parent.mkdir(parents=True, exist_ok=True)
            output = args.app_log.open("w", encoding="utf-8")
        else:
            output = (Path(temp) / "app.log").open("w", encoding="utf-8")
        with output:
            app = subprocess.Popen([str(args.app.resolve()), "--session", "--windowed",
                                    "--scene", "temporal-lab"], cwd=BUILD, env=env,
                                   stdout=output, stderr=subprocess.STDOUT)
            socket_path = Path(temp) / f"luminex-session-{app.pid}.sock"
            try:
                deadline = time.monotonic() + 20
                while not socket_path.exists() and app.poll() is None and time.monotonic() < deadline:
                    time.sleep(0.05)
                if not socket_path.exists():
                    raise AssertionError("App did not create a session socket")

                def invoke(words):
                    command = [sys.executable, str(CLIENT), "--socket", str(socket_path),
                               "--timeout", "4", *words]
                    result = subprocess.run(command, capture_output=True, text=True, timeout=6)
                    try:
                        payload = json.loads(result.stdout)
                    except json.JSONDecodeError as error:
                        raise AssertionError(f"Invalid client output for {words}: {result.stdout}") from error
                    return result.returncode, payload

                for query in QUERIES:
                    code, payload = invoke(("query", query))
                    if query == "graph" and code == 2 and payload.get("error", {}).get("code") == "unavailable":
                        results["graph"] = "unavailable: no published graph"
                    else:
                        if code != 0 or not payload.get("ok"):
                            raise AssertionError(f"query.{query}: exit {code}, {payload}")
                        if query == "graph":
                            results["graph"] = "published"
                    results["queries"][query] = payload
                if results["queries"]["status"]["result"].get("tier") != 0:
                    raise AssertionError(f"Connection did not start read-only: {results['queries']['status']}")

                for words in REFUSALS:
                    command = (*words, str(plan)) if words[:2] == ("plan", "submit") else words
                    code, payload = invoke(command)
                    name = ".".join(words[:2])
                    if code != 2 or payload.get("error", {}).get("code") != "tier":
                        raise AssertionError(f"{name}: expected tier/2, got {code}, {payload}")
                    results["refusals"][name] = payload
                app.wait(timeout=40)
                if app.returncode != 0:
                    raise AssertionError(f"App exited {app.returncode}; see {output.name}")
                if socket_path.exists():
                    raise AssertionError(f"Socket remained after App exit: {socket_path}")
                results["appExit"] = app.returncode
                results["socketRemoved"] = True
            finally:
                if app.poll() is None:
                    app.terminate()
                    try:
                        app.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        app.kill()
                        app.wait(timeout=5)
    print(json.dumps(results, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
