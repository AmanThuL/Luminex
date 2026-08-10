# M5 — Execution Substrate and Observability — Design Spec

**Date**: 2026-08-11
**Status**: Accepted
**Boundary authority**: `docs/roadmap.md` M5 owns the outcome, deliverables, exit gate, portability
checkpoint A, and deferrals. This spec decomposes that boundary without expanding it; where wording
differs, the roadmap wins.

## 1. Purpose

M5 gives the RHI and render graph the compute and resource execution substrate that later temporal
and GPU-driven features require, and makes the compiled frame observable: the same frame is
inspectable through a deterministic dump and a read-only editor panel, with per-pass timing,
transient memory, and alias savings visible. Histogram exposure and a bloom chain exercise the
substrate end-to-end.

## 2. Scope decisions

- **Indirect arguments**: checkpoint A freezes semantic tests for indirect arguments, so M5 adds
  minimal indirect execution — `dispatchIndirect`, `drawIndirect`, and `drawIndexedIndirect`
  consuming a caller-filled GPU buffer with RHI-defined argument layouts — plus conformance tests
  for layout, alignment, and offset semantics. The indexed variant is included because the existing
  mesh path is indexed and M7's indirect work generation targets indexed meshes. GPU-side indirect
  work generation remains deferred to M7.
- **Exposure default**: scenes open in the existing deterministic manual-exposure mode. Histogram
  auto-exposure is a Render Settings opt-in and is the only intentionally frame-history-dependent
  behavior in M5. Its feedback contract is section 9.
- **Bloom default**: bloom is enabled by default, identically in the interactive editor and
  `--screenshot`. It carries no temporal state, so a frame remains deterministic for fixed
  settings; tests comparing against pre-M5 output pin their toggles explicitly.
- **Staging**: substrate-first. Contracts are designed once at the bottom, conformance tests gate
  each stage, and the deterministic dump lands early as the debugging instrument for pooling,
  culling, and the inspector.

## 3. Stages

| # | Stage | Delivers |
|---|-------|----------|
| 1 | RHI compute and storage | Compute pipelines, compute pass scope and dispatch, storage usages and access-declared bindings, explicit barriers, timing for every pass kind, compute-to-sample and storage-hazard conformance tests |
| 2 | Copies, subresource, indirect | Copy pass scope, general copies and `fillBuffer`, copy-region and buffer-layout contracts, per-mip/per-layer barriers, arbitrary mip/layer readback, indirect execution with argument conformance tests |
| 3 | Graph semantics, culling, dump | Compute/copy passes and subresource uses in `RenderGraph`, buffer export, per-subresource validation, deterministic dead-pass culling, frame-identified `CompiledFrameRecord`, deterministic text dump |
| 4 | Transient ownership and pooling | Graph-created transient resources, lifetime analysis, per-frame-slot pools with conservative aliasing, pooling on/off parity, resize/toggle generations, superseding ADR for the graph's import-only rule |
| 5 | Render Graph inspector | Read-only editor panel over the retained frame records: uses, schedule and culling reasons, lifetimes, transitions, reuse, per-pass timing, transient high-water mark, alias savings |
| 6 | Histogram exposure and bloom | One-frame-feedback histogram exposure with explicit resets; threshold/downsample/upsample bloom chain on per-mip storage views; Render Settings toggles |
| 7 | Checkpoint A freeze and record | Named frozen conformance suite (upload/layout, views, sRGB, reversed-Z, storage hazards, load/store, indirect arguments, frame-slot retirement), `docs/milestones/m5.md`, roadmap baseline update |

## 4. RHI substrate surface

All additions live under `RHI/Include/RHI/`, stay API-neutral, return `Result<T>` from creation,
treat misuse as `LMX_ASSERT`, and label every GPU object.

- `ComputePipeline` with a `ComputePipelineDesc` (shader function, label), compiled through the
  existing Slang → MSL → metallib path with the runtime-MSL fallback intact.
- `BufferDesc`/`TextureDesc` gain storage read/write usage flags.
- Three subresource vocabularies with distinct jobs: `TextureSubresourceRange` (mip and layer
  ranges) for graph uses, views, and barriers; `TextureCopyRegion` plus `BufferTextureLayout`
  (origin, extent, buffer offset, row and slice pitch) for copies and readback; `TextureViewDesc`
  (a range plus optional sRGB/linear reinterpretation) for binding. View reinterpretation is
  validated against the texture's format family.
- Indirect argument structs are RHI-defined with documented layout, alignment, and offset rules:
  dispatch (threadgroup counts), draw, and indexed draw (matching the existing uint32
  per-draw-address index model).

## 5. Command and binding contract

`CommandList` gains two new pass scopes beside the existing render pass; every command below is
valid only inside its scope, mirroring the render-pass rule:

- `beginComputePass(label)` / `endComputePass`: inside, `bindComputePipeline`,
  `bindStorageBuffer(slot, buffer, access)`, `bindStorageTexture(slot, texture, view, access)`,
  the existing read-only texture/sampler/uniform binds, `dispatch`, and `dispatchIndirect`.
  Bindings declare read or write access so the backend and validation know intent without
  tracking.
- `beginCopyPass(label)` / `endCopyPass`: inside, buffer↔buffer, buffer↔texture, and
  texture↔texture copies addressed by `TextureCopyRegion`/`BufferTextureLayout`, plus
  `fillBuffer(buffer, offset, size, value)` — also the histogram clear primitive.
- `drawIndirect` and `drawIndexedIndirect` record inside render passes.
- The explicit barrier API takes the resource, its subresource range, and before/after uses,
  honest about Metal 4's untracked-resource model; each command documents its visibility and
  synchronization contract.
- Every pass kind gets the same boundary timestamps; retired timing publication carries the frame
  number it belongs to, so observability can join timings to the frame that produced them.

## 6. Subresource model

The graph keeps whole-resource SSA versions with untouched-subresource inheritance:

- Any write to any subresource produces a new version of the whole resource; subresources the pass
  did not write carry the previous version's contents forward.
- Two passes writing non-overlapping ranges of the same version is still a double-write error —
  ordering stays explicit by declaring the second pass over the first's output version.
- One pass may read and write the same texture only through disjoint declared ranges (bloom reads
  mip N while writing mip N+1); overlapping read/write ranges in one pass fail validation with the
  offending ranges in the diagnostic.
- Per-subresource read-before-write validation applies within these rules; whole-resource
  declarations remain the default and existing passes keep them.

## 7. Render graph semantics

- Passes carry a kind (raster, compute, copy) with declaration paths for each; declared uses carry
  `TextureSubresourceRange`s.
- Culling roots are explicit sinks only: `exportTexture`, a new `exportBuffer` (the exposure
  buffer's persistence mechanism), swapchain presentation, and declared readback destinations.
  Dead-pass culling removes passes that cannot reach a sink, deterministically and with a recorded
  reason. There is no generic side-effect flag to bypass culling.
- Compilation emits a `CompiledFrameDebug` — passes, uses, schedule, culling reasons, transitions,
  and (from stage 4) lifetimes, logical-to-physical assignments, and memory totals — wrapped in a
  frame-identified record (section 8).

## 8. Frame observability identity

- Every compiled frame gets a monotonically increasing `frameId`. Compilation produces a
  `CompiledFrameRecord { frameId, CompiledFrameDebug }`; the App retains records for at least the
  three frames that can be in flight.
- RHI timing publication names the retired frame it measured; the inspector joins timings to the
  matching retained record and displays the newest retired frame. The dump snapshots that same
  record, so both observability clients present one frame by construction.
- The dump contains only values the compiler produces deterministically — declarations, schedule,
  culling, lifetimes, planned assignments under the RHI's documented alignment policy. GPU
  timings and driver-reported values are inspector-only and never enter golden files.

## 9. Exposure feedback

- Scene color is pre-exposed, so auto exposure is a one-frame feedback loop: the histogram pass
  reads frame N's pre-exposed scene color, and the resolve pass — knowing frame N's `preExposure`
  — reconstructs scene-referred luminance, trims percentiles, and writes the exposure applied at
  frame N+1's pre-exposure point via the exported exposure buffer.
- M5 resolves an instantaneous target exposure only; adaptation, smoothing, and the history-reset
  framework are M6 per the roadmap.
- The loop initializes from, and resets to, the manual EV value on the first frame, scene switch,
  auto-exposure enable, and resize. Manual mode never reads the feedback buffer.

## 10. Features

- **Histogram exposure**: a compute pass accumulates a log-luminance histogram of scene color into
  a storage buffer (cleared each frame by `fillBuffer`); a resolve pass produces the target
  exposure under the section 9 contract. Lighting math is unchanged; metering parameters are
  visible in Render Settings.
- **Bloom**: threshold/prefilter defined on pre-exposed luminance — bloom tracks what the display
  sees, so manual and auto exposure shift it consistently — then a downsample chain into the mips
  of one transient FP16 texture through per-mip storage views, an upsample-accumulate pass, and
  composition before the display transform. Both features are ordinary declared graph passes: they
  appear in the dump, the inspector, per-pass timing, and dead-pass culling when toggled off.

## 11. Transient ownership

- The graph gains `createTexture`/`createBuffer` declarations for transient resources it owns for
  exactly one frame; imported and exported resources are never pooled or aliased. This supersedes
  the import-only rule of ADR 0005 by a new ADR recorded when the capability lands.
- Compilation produces a logical alias plan; physical memory is a placement-heap pool owned per
  frame slot, reused only after `beginFrame` confirms that slot retired. Aliasing is conservative:
  disjoint lifetimes within one frame, full barriers at reuse boundaries, compatibility
  constrained by format, usage, storage mode, size, and alignment.
- Resize and feature toggles create a new pool generation; the old generation is released only
  after its last in-flight frame retires. Pooling has a runtime toggle and must produce identical
  output on and off.

## 12. Testing

- Every stage lands with its conformance tests. GPU tests run under `MTL_DEBUG_LAYER=1` on Metal 4
  Apple Silicon; hosted CI compiles and inventories them.
- Graph compilation — validation, culling, lifetimes, pooling assignment — is deterministic CPU
  logic covered in `Tests/unit` without a GPU. Dump output is golden-file tested.
- Pooling parity is an image comparison with pooling forced on and off. A resize/toggle stress
  test cycles window sizes and feature toggles across the three-frame pipeline and asserts no
  leaks and no reuse of live resources.
- Exposure feedback is tested against a CPU reference: a known image at a known manual EV must
  converge to the expected target exposure in one frame, and every reset rule in section 9 has a
  case.
- Stage 7 consolidates the semantic tests behind checkpoint A into a named conformance suite with
  a short manifest declaring them frozen; a future backend must pass the same suite.

## 13. Non-goals

Per the roadmap's M5 deferrals: motion/history semantics, dynamic resolution, GPU scene ownership,
bindless materials, indirect visibility, and alternative opaque surface paths. Additionally out of
scope: GPU-side indirect work generation (M7), exposure adaptation and smoothing (M6), automatic
exposure as a default mode, graph optimization beyond dead-pass culling and conservative pooling,
and any M5.1 interface-experiment work.

## 14. Exit gate

The roadmap's M5 exit gate applies verbatim: compute-to-sample and per-mip hazards pass conformance
tests; resize and feature toggles neither leak nor reuse live resources; pooling on and off produce
the same output; arbitrary intermediate mips and layers can be captured; the same compiled frame is
inspectable through the deterministic dump and the editor; and pass time, transient high-water
marks, and alias savings are visible.
