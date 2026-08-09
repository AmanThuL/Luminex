#!/bin/bash
# End-to-end smoke for the GPU debug flow: build, capture a frame of the default scene, parse,
# assert the dump is usable. Needs a display and MTL_CAPTURE support; exits 0 on success.
set -euo pipefail
cd "$(dirname "$0")/../.."
OUT="${TMPDIR:-/tmp}/lmx-gpudebug-smoke"
case "$OUT" in /*) ;; *) echo "FAIL: TMPDIR is not absolute ($OUT)"; exit 1 ;; esac
rm -rf "$OUT" && mkdir -p "$OUT"
xmake >/dev/null
# LMX_CAPTURE_PATH is resolved relative to the App's own CWD (its build dir), not this script's
# CWD -- `xmake run` sets that up, but the path itself must still be absolute, hence $OUT.
MTL_CAPTURE_ENABLED=1 LMX_CAPTURE_AT_FRAME=30 LMX_MAX_FRAMES=40 \
  LMX_CAPTURE_PATH="$OUT/smoke.gputrace" xmake run App
test -d "$OUT/smoke.gputrace" || { echo "FAIL: no bundle"; exit 1; }
test -f "$OUT/smoke.gputrace.schema.json" || { echo "FAIL: no sidecar"; exit 1; }
python3 Tools/GpuDebug/gputrace_dump.py "$OUT/smoke.gputrace" --out "$OUT/dump"
python3 - "$OUT/dump" <<'EOF'
import json, pathlib, sys

dump = pathlib.Path(sys.argv[1])
manifest = json.loads((dump / "manifest.json").read_text())
assert manifest["capture"]["sceneName"], "manifest carries no scene context"
assert manifest["resources"]["matched"], "nothing joined bundle<->schema"

uploads = json.loads((dump / "uniforms.json").read_text())["uploads"]
assert any(u["structName"] == "PassUniforms" for u in uploads), "no PassUniforms decoded"

assert (dump / "shadow-map.png").is_file(), "no shadow-map.png in dump dir"
assert (dump / "scene-color.png").is_file(), "no scene-color.png in dump dir"

errors = [a for a in manifest["anomalies"] if a["severity"] == "error"]
assert not errors, f"error-severity anomalies in a healthy capture: {errors}"

scene_color = next(img for img in manifest["images"] if img["label"] == "lmx.render.sceneColor")
assert not scene_color["stats"]["flat"], "scene-color.png is flat -- nothing rendered?"

print("smoke OK:", len(manifest["resources"]["matched"]), "matched,",
      len(uploads), "uploads,", len(manifest["anomalies"]), "anomalies")
EOF
echo "PASS"
