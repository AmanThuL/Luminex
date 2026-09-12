# GPU Debugging

**Status**: Implemented

Use a capture when rendered output is wrong, a timing trace when the question is performance, and a
render-graph dump when the question is what the frame declared. All three depend on meaningful GPU
object and pass labels.

## Dump the compiled frame

```bash
LMX_GRAPH_DUMP=/tmp/luminex-frame.txt xmake run App
```

The path must be absolute. The first frame the run compiles is written and no later one is, so the
file is the same whether the run lasted one frame or ten thousand. It lists the imported resources,
the declared sinks, each scheduled pass with its uses in schedule order, each culled pass with the
reason it was dropped, and the barriers the graph derived. It carries no GPU timing and no
driver-reported value, so it answers "was this pass declared, ordered, and kept?" — not "how long did
it take", which is the timing trace's question.

Reach for it first when a pass appears to do nothing: a pass listed under `culled` never ran, and
its reason says whether it produces nothing or no sink reaches it. The editor's Render Graph panel,
opened in its own detached OS window, shows the same compiled record live as a grouped, wrapped
node canvas with a selection-scoped details pane, and its Dump frame button renders this same text
format for the currently displayed frame to `graph-dump-frame-<id>.txt` next to the binary, without
setting the environment variable.

## Capture and inspect a frame

```bash
MTL_CAPTURE_ENABLED=1 \
LMX_CAPTURE_AT_FRAME=30 \
LMX_MAX_FRAMES=40 \
LMX_CAPTURE_PATH=/tmp/luminex-frame.gputrace \
xmake run App

python3 Tools/GpuDebug/gputrace_dump.py \
  /tmp/luminex-frame.gputrace \
  --out /tmp/luminex-frame-dump
```

`LMX_CAPTURE_PATH` must be absolute. The capture covers one frame. To reproduce an upscaled frame,
add `--render-scale <0.5..1.0>` (offscreen) or `LMX_DYNAMIC_RESOLUTION_BUDGET_MS=<ms>` (a windowed
run, letting the controller pick the scale the captured frame lands at); the manifest and dumped
images then show `lmx.pass.temporal.upscale`'s inputs and the scene pass's render area/scissor
instead of the native resolve's. Inspect the generated manifest first, then decoded uniforms and
resource images. The capture's schema sidecar records one
`frameDataUploads` entry per `bindFrameData` call in the captured frame — page label, slot, offset,
size, alignment, and GPU address — which is what to check when tracing an upload to its page; the
dump tool's decoded output remains `uniforms.json`. Treat label joins and positional frame-data-page
joins with the confidence recorded in the manifest; the Metal capture bundle is not a documented
interchange format.

Escalate in this order:

1. Confirm the selected scene, camera, render settings, and capture frame.
2. Check manifest anomalies and finiteness/range of decoded uniforms.
3. Inspect pass inputs and outputs, beginning at the earliest incorrect resource.
4. Reproduce with Metal validation enabled.
5. Add a narrow engine-side diagnostic or inspect the capture in Xcode when the dump cannot establish
   command order or binding state.

Keep captures and dump directories outside the repository. A postmortem records only the durable
symptom, evidence, root cause, correction, and prevention.

## Inspect vendor temporal reconstruction

`--temporal metalfx` opts into the capability-selected scaler; check effective mode and fallback
in the Inspector before interpreting a capture. Native TAA remains the default. For a windowed
TemporalLab capture with dynamic resolution:

```bash
MTL_DEBUG_LAYER=1 MTL_CAPTURE_ENABLED=1 \
LMX_CAPTURE_AT_FRAME=30 LMX_MAX_FRAMES=40 \
LMX_DYNAMIC_RESOLUTION_BUDGET_MS=8 \
LMX_GRAPH_DUMP=/tmp/luminex-vendor-frame.txt \
LMX_CAPTURE_PATH=/tmp/luminex-vendor.gputrace \
xmake run App --scene temporal-lab --temporal metalfx

python3 Tools/GpuDebug/gputrace_dump.py \
  /tmp/luminex-vendor.gputrace --out /tmp/luminex-vendor-dump
```

The graph contains `lmx.pass.temporal.vendor.pack` (compute) followed by
`lmx.pass.temporal.vendor` (external), with no native resolve, upscale or commit. The first-frame
graph dump can have a different content extent from the later capture if the controller moved it.
Find `lmx.render.vendorMotion`, `lmx.render.vendorReactive` and `lmx.render.vendorExposure` beside
scene colour, current depth and the output colour-history slot. The exposure texel is **reciprocal
applied exposure**; zero packed motion with reactive 1 represents the invalid-motion sentinel.

The vendor pass's command-buffer groups name the pass and `lmx.temporal.vendor.scaler MetalFX
Temporal`; its owned fence has a `.handoff` suffix. These labels identify the opaque algorithm;
the current automated dump does not recover MetalFX's private encoder names. There is no public
scaler label property. CPU-readable outputs additionally show `.privateOutput`
and `.outputCopy`; that copy belongs to the external pass's GPU timing. Graph dumps describe the
declared external operation, not the vendor's private encoders. Use Xcode to inspect opaque
encoder ordering or fence state when the decoded manifest cannot establish them.

The dump tool currently cannot decode the HDR, motion or reactive formats or recover packed
transients from placement-heap contents. Its legacy expected scene-colour label and shadow-depth
recompute may also report errors on this renderer. Preserve these findings with the capture;
they are distinct from runtime Metal validation and do not establish a reconstruction failure.

Motion, reprojection error and reprojected history are engine diagnostics. The latter adds
`lmx.pass.temporal.reprojectedHistory` under vendor mode; rejection, blend weight and per-pixel
history age are native-only. CLI combinations of those views with `--temporal metalfx` are usage
errors. `--temporal-view reprojected` is valid; its output is a corrected engine-history diagnostic,
not a view into MetalFX's private history.

Run the frozen vendor scenarios with `MTL_DEBUG_LAYER=1 xmake test Tests/gpu` as part of GPU
validation, or build Tests and run `./Tests "[gpu][temporal][vendor]"` from its build directory.
Record the printed static, motion and exposure metrics separately from native parity evidence.

## Collect encoder timings

```bash
python3 Tools/GpuDebug/profile.py --help
```

The profiler wraps Instruments export and reports encoder-granularity intervals. Record device, OS,
build mode, resolution, validation state, scene, sample count, and cold/warm classification with any
performance claim. A missing optimized-away empty encoder is not a zero-duration measurement.

## Automated tool tests

```bash
python3 -m unittest discover -s Tools/GpuDebug/tests -v
```

These tests validate the parsers and report generation without requiring a GPU capture session.

## Parity checks

M5 added histogram auto-exposure and bloom as passes declared every frame; dead-pass culling keeps
only the features that are enabled. With both features off, `--screenshot` output must stay
byte-identical to what the pre-M5 tip rendered.
There is no golden-image automation for this -- the procedure below, re-run by hand, is the
accepted mechanism. Auto-exposure is off by default already; bloom is not, so disabling it needs
`LMX_SCREENSHOT_NO_BLOOM=1` (`Source/App/Screenshot.cpp`), an undocumented-to-users env var that
exists solely for this check.

Build the baseline from the commit before the change under test (substitute the actual parent
commit), then the tip, capturing all three scenes both times:

```bash
git worktree add /tmp/lmx-baseline <baseline-commit>
cd /tmp/lmx-baseline && xmake setup -P . && xmake -P .
for scene in sponza damaged-helmet material-lab; do
  LMX_SCREENSHOT_NO_BLOOM=1 xmake run -P . App --scene "$scene" \
    --screenshot "/tmp/lmx-baseline-$scene.bmp"
done

cd <worktree-under-test> && xmake -P .
for scene in sponza damaged-helmet material-lab; do
  LMX_SCREENSHOT_NO_BLOOM=1 xmake run -P . App --scene "$scene" \
    --screenshot "/tmp/lmx-tip-$scene.bmp"
done

for scene in sponza damaged-helmet material-lab; do
  cmp "/tmp/lmx-baseline-$scene.bmp" "/tmp/lmx-tip-$scene.bmp" && echo "$scene: IDENTICAL"
done
```

`cmp` exits non-zero and names the first differing byte offset on any mismatch, so silence-implies-
identical does not apply -- check that all three scenes actually printed `IDENTICAL`. Bloom's own
effect is checked the opposite way: capture once more without `LMX_SCREENSHOT_NO_BLOOM` and confirm
the file differs from the bloom-off capture (`cmp` reports a byte offset) and opens as a plausible
image (no full-screen white, no NaN speckle) rather than asserting a specific diff.
