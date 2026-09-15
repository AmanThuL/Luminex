# GPU visibility and work generation

**Status**: Accepted

CPU classification with indirect submission remains the default. GPU classification is an explicit
alternative that produces draw rows and indexed arguments through the render graph. The
[milestone](../milestones/m7.3.md) owns its verification results and acceptance state; this guide
explains operation and diagnostics. No measurement here selects a new default.

## Run and inspect

```sh
xmake run App --scene visibility-lab --classify gpu --submission indirect
xmake run App --scene visibility-lab --classify gpu --submission batched
xmake run App --scene visibility-lab --classify gpu --submission batched --classify-check
```

The editor opens maximized to usable display bounds. Omit `--windowed` for fullscreen-windowed
visual validation. Rendering Inspector > Visibility selects the classifier, culling and submission.
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
has sixteen slots; visibility entries use slots 0–12 without changing scene shader bindings.

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
measurement remains unscored. Schema 2 separates declaration CPU metrics from exact frame-keyed
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
