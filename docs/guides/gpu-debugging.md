# GPU Debugging

**Status**: Implemented

Use a capture when rendered output is wrong, a timing trace when the question is performance, and a
render-graph dump when the question is what the frame declared. All three depend on meaningful GPU
object and pass labels.

Choose a catalog scene with File > Open Scene; the Hierarchy panel selects subjects using
collapsible groups, search and keyboard navigation. Nonobvious controls show delayed contextual
tips while disabled controls retain visible reasons. The Inspector’s Rendering subject has a
read-only Display details section: output domain, encoded-space
SDR UI rule, framebuffer scale, display target extent and current 1:1 image mapping. A resize may
briefly stretch the prior image while debounce settles. PNG screenshots preserve the display
domain and frame facts in metadata; BMP remains available for exact historical parity.

## Restore editor settings

Inspector Reset actions affect the named group. Camera Reset restores the scene's initial pose
and lens and stops camera-rail following. Light Reset restores authored direction and scene-linear
radiance. Object Reset restores its authored transform or samples that object's animated transform
at the current playback time, preserving other object edits. Pause scene to retain a manual edit
to an animated transform; playback replaces it on the next track sample.

Rendering groups restore these editor defaults without resetting playback or another group:

| Group | Defaults |
|---|---|
| Exposure | Manual 0 EV; auto off; percentiles 50–95%; target grey 0.18; automatic EV range −8 to 8; compensation 0 EV; adaptation up/down 3/1.5 stops per second |
| Bloom | Enabled; linear threshold 1; intensity 0.2 |
| Shadows | PCF |
| Reconstruction | Temporal inputs and jitter enabled; Native TAA; Final diagnostic view |
| Resolution | Scale 1; dynamic resolution off; timed-pass budget 16 ms |
| Display & Details | Encoded sRGB clear RGBA (0.05, 0.07, 0.10, 1); wireframe off; transient pooling on |

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

Reach for a graph dump first when a pass appears to do nothing: a pass listed under `culled`
never ran, and its reason explains why. The detached Render Graph window displays one coherent
frame as a grouped node canvas with selection-scoped details. Live publishes a complete owned
record and its matched timing set four times per second, using Performance's 0.25-second interval.
Graph timing is the exact latest value for that displayed frame, not a rolling average. The
Live/Frozen label identifies it; node labels, details and Dump read the same publication. First
data and Resume publish immediately; later topology and timing updates wait for the next
publication boundary. Freeze keeps the displayed publication through later frames and scene
switches. Unavailable matched timing is N/A.
Fit graph, Fit selection and 100% change navigation explicitly. Dump frame writes the currently
displayed record to `graph-dump-frame-<id>.txt` next to the binary, including while frozen.
The result reports success or a recoverable write failure, with an absolute path and Copy path/
Reveal controls. This text dump still contains graph declarations, not GPU timing measurements.

## Console and editor selection diagnostics

Window > Console opens the bounded read-only log viewer. Its default tab sits beside Performance;
workspace schema 2 adds Console visibility without rebuilding existing saved docking. The store
retains at most 2,000 messages and 2 MiB of payload, truncating each message at 16 KiB on a UTF-8
boundary. UTC timestamps, Trace/Debug/Info/Warning/Error/Critical levels, and eviction/truncation
counts remain visible. Minimum severity and case-insensitive message search filter only display.
Freeze display retains the shown messages and counters while logging continues. Resume shows
current retained history. Clear empties retained/displayed messages and counters, retaining filters
and freeze state. Copy visible copies exactly the matching displayed messages, timestamps and
severity. Follow newest scrolls only when already at the end; the search field executes no commands.

Viewport's selection outline follows visible selected geometry, including depth occlusion and
masked cutouts; Frame selected separately fits reliable object bounds. The outline is an editor
presentation aid that can be hidden. Foreground occlusion cuts never become silhouette edges;
border source and destination are depth-tested. App declares `lmx.pass.selection.coverage`,
`lmx.pass.selection.visibility` and
`lmx.pass.selection.outline` through Render's `SelectionOutline` utility, then samples its separate
SDR output target. Their GPU costs appear in graph/timing records. They never write scene targets
or temporal history, and ordinary offscreen screenshots/sequences do not invoke them. See the
[UX1 design](../specs/2026-09-14-ux1-editor-experience-design.md) and
[active acceptance record](../milestones/ux1.md) for scope and validation status.

## Capture and inspect a frame

For an interactive capture, launch with `MTL_CAPTURE_ENABLED=1`, then use Debug > Capture Next
GPU Frame, C outside text entry, or the Viewport GPU capture controls. They share one pending/result
state. Startup without capture support explains how to enable it; changing the environment requires
a relaunch. A successful result retains the output path with Copy path and Reveal. On failure,
read the reason, correct the path or process capability and retry. Graph/metric freeze do not pause
the rendered scene; pause the scene transport separately when a stable pose is needed.

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

Metal4 argument tables clear texture slots at each render/compute pass before binding the
resources that pass uses. This prevents unused slots from naming retired transient textures
when Xcode enumerates bindings. Draw/dispatch snapshots preserve earlier work. On Xcode 26.6,
use **Bound** resources for inspection; **Accessed** mode is unavailable for Metal4. Scene draws
show shared geometry at b0, the 16-byte draw selector at b1, instances at b5 and materials at b6.

Keep captures and dump directories outside the repository. A postmortem records only the durable
symptom, evidence, root cause, correction, and prevention.

## Inspect vendor temporal reconstruction

`--temporal metalfx` opts into the capability-selected scaler. In Rendering > Reconstruction,
compare the requested algorithm with the effective summary and fallback reason. Temporal inputs
off retains that request while execution is Off at scale 1. Resolution separates the live retired
timed-pass sum and its frame ID from the controller's last observation. Disable dynamic resolution
for a controlled fixed-scale capture. History & vendor details retain the last reset event,
declared-frame provenance, vendor generation and supported scale range. Native-only diagnostics
explain why device reconstruction cannot provide them; choose Native TAA for rejection, blend
weight and accumulation age. Native TAA remains the default. For a windowed TemporalLab capture
with dynamic resolution:

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

### Editor UI scale

Use the main bar's minus/percentage/plus buttons or Layout > UI Scale to change fonts and
controls together (75–150%). Click the percentage or press Cmd+0 for 100%; Cmd+- shrinks and
Cmd++ / Cmd+= grows. Text editing, active widget drags and popups suppress these shortcuts.
The detached graph shares this preference, while its canvas zoom remains independent.
`UiScalePercent` persists in the existing workspace section of `imgui.ini`; old files default
to 100% without redocking. Reset Default Layout keeps the UI-scale preference.

The editor uses bundled Inter Regular (16 logical points at 100%, 12 at 75%), with fixed-width
digits for stable diagnostic columns. `xmake setup` fetches the pinned Inter 4.1 TrueType source
and SIL license; building App stages `Fonts/` beside the executable. A missing font logs a
warning and uses the embedded fallback; rerun setup and rebuild to restore Inter. UI zoom still
controls all panels together and restores independently of docking.
