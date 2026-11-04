#!/bin/bash
# End-to-end smoke for the GPU debug flow: build, capture a frame of the default scene, parse,
# assert the dump is usable. Needs a display and MTL_CAPTURE support; exits 0 on success.
set -euo pipefail
cd "$(dirname "$0")/../.."
OUT="${TMPDIR:-/tmp}/lmx-gpudebug-smoke"
case "$OUT" in /*) ;; *) echo "FAIL: TMPDIR is not absolute ($OUT)"; exit 1 ;; esac
rm -rf "$OUT" && mkdir -p "$OUT"
xmake -P . >/dev/null
# LMX_CAPTURE_PATH is resolved relative to the App's own CWD (its build dir), not this script's
# CWD -- `xmake run` sets that up, but the path itself must still be absolute, hence $OUT.
MTL_CAPTURE_ENABLED=1 LMX_CAPTURE_AT_FRAME=30 LMX_MAX_FRAMES=40 \
  LMX_CAPTURE_PATH="$OUT/smoke.gputrace" xmake run -P . App
test -d "$OUT/smoke.gputrace" || { echo "FAIL: no bundle"; exit 1; }
test -f "$OUT/smoke.gputrace.schema.json" || { echo "FAIL: no sidecar"; exit 1; }
python3 Tools/GpuDebug/gputrace_dump.py "$OUT/smoke.gputrace" --out "$OUT/dump"
python3 - "$OUT/dump" <<'EOF'
import json, pathlib, sys

dump = pathlib.Path(sys.argv[1])
manifest = json.loads((dump / "manifest.json").read_text())
assert manifest["capture"]["sceneName"], "manifest carries no scene context"
assert manifest["resources"]["matched"], "nothing joined bundle<->schema"

roots = json.loads((dump / "uniforms.json").read_text())["roots"]
summary = manifest["frameRoots"]
assert summary["registered"] > 0, "the capture registered no frame roots"
assert not summary["undecoded"], f"frame roots left undecoded: {summary['undecoded']}"
assert any(r["name"] == "PassUniforms" for r in roots), "no PassUniforms decoded"

assert (dump / "shadow-map.png").is_file(), "no shadow-map.png in dump dir"
assert (dump / "display-color.png").is_file(), "no display-color.png in dump dir"

# The expected-label check still names v1's lmx.render.sceneColor, which v2 does not create.
errors = [a for a in manifest["anomalies"] if a["severity"] == "error"
          and a["subject"] != "lmx.render.sceneColor"]
assert not errors, f"error-severity anomalies in a healthy capture: {errors}"

display = next(img for img in manifest["images"] if img["label"] == "lmx.render.displayColor")
assert not display["stats"]["flat"], "display-color.png is flat -- nothing rendered?"

print("smoke OK:", len(manifest["resources"]["matched"]), "matched,",
      len(roots), "of", summary["registered"], "roots decoded,",
      len(manifest["anomalies"]), "anomalies")
EOF
echo "PASS"
