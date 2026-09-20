# GPU visibility and work generation

**Status**: Accepted

CPU classification with indirect submission remains the default. GPU classification is an explicit
alternative that produces draw rows and indexed arguments through the render graph. The
[milestone](../milestones/m7/m7.3.md) owns its verification results and acceptance state; this guide
explains operation and diagnostics. No measurement here selects a new default.

## Run and inspect

```sh
xmake run App --scene visibility-lab --classify gpu --submission indirect
xmake run App --scene visibility-lab --classify gpu --submission batched
xmake run App --scene visibility-lab --classify gpu --submission batched --classify-check
```

The editor opens maximized to usable display bounds. Omit `--windowed` for fullscreen-windowed
visual validation. Expand Rendering in Hierarchy: Visibility selects classifier/culling, Occlusion
keeps HZB controls and readings together, and Submission selects draws beside work/cost counters.
The compact summary shows kept/culled counts, its declared/retired frame and enabled check result.
Hierarchy dims frustum-rejected and occluded objects; visible, bypassed and pending names stay normal.
Names have no status prefixes. Selected rows use normal text for contrast; hover and Inspector retain reasons.
Each topic exposes its detailed fields in compact label/value rows; hover controls for explanations.
Overflow, check failures and fallback reasons remain visible across Rendering topics.
GPU classification requires indirect or batched; direct uses a CPU-known per-draw selector.
`--visibility off` still executes the GPU kernels and records Disabled bypasses.
`--classify-check` requires GPU classification and retains a CPU oracle snapshot for diagnostics.

The CPU supplies candidates in object order for indirect, or stable pipeline/material/mesh order
for batched. It binds each run's textures and pipeline and issues a command for every candidate
slot or run, even if its argument has zero instances. Thus GPU classification does not remove
CPU command encoding. The scene view uses the same jittered five-plane frustum and guard as the
CPU oracle; the shadow view remains unculled. Disabled, unculled, nonfinite-transform and
unreliable-bounds bypasses retain their existing precedence.

## Graph and buffer lifetime

The optional work appears before shadow/scene consumers under `lmx.pass.visibility`:

| Pass | Work |
|---|---|
| `reset` | Zero both views' counters in a copy pass |
| `classify` | Write state/reason and retained counts for 256-candidate chunks |
| `scan` | Batched only: exclusive chunk offsets within each canonical run |
| `emit` | Write rows and raw five-word indexed arguments in stable order |

Compaction uses ordered prefixes, with a carry for runs exceeding 256 chunks. Atomic operations
count outcomes; they never choose an output position. Sparse slots retain their candidate index;
rejected sparse arguments are all zero. Dense runs reserve their full candidate range, pack valid
rows at its beginning, and retain geometry/firstInstance even when instanceCount is zero.

The graph sees CPU-written candidates/runs/chunks, GPU-written states/counters/rows/arguments,
transient counts/offsets, shader reads and indirect reads separately. Physical submission buffers
retain their terminal uses across classifier switches. The existing shared RHI buffer namespace
has sixteen slots; visibility entries use slots 0–12, with source-space occlusion parameters in slot 13.

Every frame uses one of three paced slots. Draw rows/arguments grow together and keep replaced
allocations through last prepared frame + 3. Visibility input/state storage doubles only in the
currently paced slot, whose previous contents have already retired. CPU uploads skip tables whose
bytes match that slot's prior upload. Empty views retain diagnostic sinks and report zero counts.

## Read the correct frame

GPU counters and states normally reach the editor on slot reuse, three device frames after their
submission. The displayed frame label names that original frame. Full generational object IDs
and world bounds are retained from declaration; removing an object and reusing its row cannot
attach the old result to its replacement. A scene switch clears incompatible display context.
A matching timing may be unavailable if the RHI's latest retired publication skipped that frame;
Inspector reports that absence rather than showing another frame's duration.

Each view has candidates, visible, rejected, four bypass counts, emitted rows/commands and
row/command overflow counts. Candidates equal visible + rejected + bypassed. Emitted rows plus
overflowed rows equal visible + bypassed. Emitted commands count nonempty outputs; the separately
reported CPU-issued command count includes fixed zero-instance slots and runs.

Table byte fields are logical candidate/run/chunk payload sizes, not the bytes uploaded that
frame. State/counter byte fields describe one current paced slot; allocated list/argument bytes
cover all three active draw-buffer slots. Metadata spare capacity and retiring allocations are
not totalled. These categories are not total renderer memory.
Retired `listBytes` counts valid emitted rows in both layouts; `reservedListBytes` preserves the
declaration's row span, including rejected GPU slots. Argument payload includes fixed empty slots.

Production allocations cover all candidates. Fixture overrides exercise drop-and-count behavior:
no write crosses a physical capacity; a missing command drops its rows too; missing state entries
are never read by emit. Overflow appears in Inspector/Console, fails a capture sequence and
invalidates scored measurement. Check mode reports state, valid-row, argument and counter mismatch
counts against the declaration's CPU oracle. It is unscored and does not silently change thresholds.

## Capture and measure

```sh
xmake run App --scene visibility-lab --classify gpu --submission batched \
  --classify-check --frames 32 --screenshot /absolute/evidence/gpu.bmp
xmake run App --scene visibility-lab --classify gpu --submission batched \
  --measure /absolute/evidence/gpu.json --warmup 32 --frames 256
MTL_DEBUG_LAYER=1 xmake run App --scene visibility-lab --classify gpu \
  --submission batched --classify-check --unscored \
  --measure /absolute/evidence/diagnostic.json --warmup 1 --frames 4
```

Scored headless measurement refuses validation/capture instrumentation and check mode; editor
measurement remains unscored. Schema 3 separates declaration CPU metrics from exact frame-keyed
retired GPU visibility and timings. A post-waitIdle drain joins final results without rendering
three invented frames. Measurement retains the existing serialized-retirement pacing, which
measures isolated frame costs rather than overlapped throughput.

The paired collector adds GPU/CPU controls under indirect and batched to the existing workload
protocol. Keep every sample, failure and interval; disclose fixed CPU command counts. Use the
absolute millisecond summaries when the CPU visibility-pass baseline is zero; relative percentage
change is undefined in that case. Absolute paired deltas still retain their bootstrap intervals.
Use the
[image comparison guide](screenshot-comparison.md) for the frozen fifteen-image gates. BMP hashes
exclude metadata differences; PNG capture metadata truthfully records requested mode and retired
results. Sequence metadata is compared explicitly before any image-only comparison.

ICB submission is unavailable in this implementation. The pinned
[ICB spike](../research/2026-09-15-m7.3-icb-runtime-spike.md) passed native execution, texture parity
and 900-frame reliability, but its required labelled capture gate remains unresolved. That result
neither proves ICB impossible nor enables a production adapter or default switch.

## Previous-frame occlusion

Enable occlusion only with GPU classification and culling; direct submission remains unavailable:

```sh
xmake run App --scene visibility-lab --lab-instances 16384 --lab-occluders 8 \
  --classify gpu --submission batched --occlusion on
MTL_DEBUG_LAYER=1 xmake run App --scene visibility-lab --lab-occluders 8 \
  --classify gpu --occlusion on --occlusion-check --classify-check --unscored \
  --measure /absolute/evidence/check.json --warmup 0 --frames 720
xmake run App --classify gpu --occlusion on --hzb-level 2
```

The default stays off. `--lab-occluders` defaults to zero and only accepts VisibilityLab; positive
counts append deterministic slabs with gaps and masked coverage. Existing workloads keep their
original geometry when it is zero. `--hzb-level` requires occlusion, selects a nearest-expanded
raw reversed-depth view, and clamps to the available last level at the current output extent.
White is near and black far; low reversed depth naturally looks dark. Inspector offers the actual
available levels. Diagnostic views and ID checks require unscored measurement.

Each frame builds a half-resolution R32Float minimum pyramid after scene depth. Odd active
extents use ceil coverage; padded allocation depends only on output extent. Each level is a
separate compute pass; a small `lmx.pass.hzb.publish` read preserves every level and leaves a
sampled terminal state. `hzbGpuMs` includes reductions and publication, excludes visualization,
and remains separate from `visibilityGpuMs`. The two pyramids alternate independently of
reconstruction and consume the preceding device frame without CPU readback.

The box test uses the source frame's jittered matrix and active extent. It rounds outward with a
one-source-texel guard and reads at most four integer mip texels. Near-plane crossings, rectangles
outside source coverage and rectangles too large for the top level are retained. The fixed depth
guard biases toward retention. The shadow view always stays unculled.

History invalidates globally on missing/nonadjacent sources, toggles, scene/output changes,
camera-cut events, per-frame movement above 1 m or 10 degrees, and any coverageEpoch change.
Coverage includes instance identity/transform/mesh/material assignment and masked alpha, texture,
UV, cutoff and sidedness. Previous-pose refresh and emissive-only edits do not invalidate it.
Wireframe never rejects by occlusion. A continuously animated scene therefore retains all
candidates while its coverage changes. Projection and render-scale changes use the stored source
projection instead of resetting evidence.

The Occlusion topic shows source frame, validity/reason, counters, memory, check results and matched HZB
pass timings. Selected-object fields describe the retired source rectangle, mip and nearest box
depth. Viewport outlines use that source view, with a disclosed cap for rejected bounds; the
source-frame label matters when the current camera has moved. Hierarchy's occlusion tip reads
"Occluded (previous-frame HZB)".

`--occlusion-check` renders all candidates through independent direct ID draws with private depth
and shared masked coverage. It joins exact retired frame IDs and full generational instance
identities. Reports retain visible false rejections, affected pixels, and per-instance missing
streaks. An unchanged view requires zero false rejections; continuous camera motion allows one
missing frame and fails at two. Geometry recovery does not imply immediate temporal image recovery.
Check data never controls rejection. A failed capture sequence is incomplete; diagnostic
measurements retain the full requested interval and report the first reference failure at drain.

The paired collector adds `occlusion-on-off-indirect` and `occlusion-on-off-batched` controls and
occluded lab workloads at 1,024, 16,384 and 65,536 instances. Its original six workloads and
12 repetitions / 32 warmup / 256 measured-frame protocol remain unchanged. Every attempt and
confidence interval is evidence; no control automatically selects a default.

For explicit offscreen validation only, set both `LMX_OCCLUSION_SCALE_CHANGE_FRAME` (zero-based
simulation frame) and `LMX_OCCLUSION_SCALE_CHANGE_VALUE` (0.5–1.0) to step the active scale during
a rail. Temporal reconstruction must be enabled and measurements must be unscored. Reports keep
these environment values and actual frame extents. This scripted step exercises source-extent
handling; it is not a dynamic-resolution controller performance result.

Inspector Visibility, Occlusion and Submission readings refresh together every 250 ms, matching
Performance and Render Graph. Each publication owns one frame's counters and exact joined GPU
times; these are sampled latest values, not rolling averages. Object lookup, renderer checks and
measurement collection still run at their original rate, and failure warnings use the latest result.
