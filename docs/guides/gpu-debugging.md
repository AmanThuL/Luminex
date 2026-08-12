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
its reason says whether it produces nothing or no sink reaches it.

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

`LMX_CAPTURE_PATH` must be absolute. The capture covers one frame. Inspect the generated manifest
first, then decoded uniforms and resource images. The capture's schema sidecar records one
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
