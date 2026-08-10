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
  minimal indirect execution — `dispatchIndirect` and `drawIndirect` consuming a caller-filled GPU
  buffer with RHI-defined argument layouts — plus conformance tests for layout, alignment, and
  offset semantics. GPU-side indirect work generation remains deferred to M7.
- **Exposure default**: scenes open in the existing deterministic manual-exposure mode. Histogram
  auto-exposure is a Render Settings opt-in and is the only intentionally frame-history-dependent
  behavior in M5.
- **Bloom default**: bloom is enabled by default in the editor. It carries no temporal state, so a
  frame remains deterministic for fixed settings; tests comparing against pre-M5 output pin their
  toggles explicitly.
- **Staging**: substrate-first. Contracts are designed once at the bottom, conformance tests gate
  each stage, and the deterministic dump lands early as the debugging instrument for pooling,
  culling, and the inspector.

## 3. Stages

| # | Stage | Delivers |
|---|-------|----------|
| 1 | RHI compute and storage | Compute pipelines and dispatch, storage buffer/texture usages, texture view contracts, explicit barriers, compute-to-sample and storage-hazard conformance tests |
| 2 | Copies, subresource, indirect | General copies, per-mip/per-layer views and barriers, arbitrary mip/layer readback capture, minimal indirect execution with argument conformance tests |
| 3 | Graph semantics, culling, dump | Compute/copy passes and subresource uses in `RenderGraph`, extended validation, deterministic dead-pass culling, deterministic compiled-frame text dump |
| 4 | Transient pooling | Conservative logical-to-physical aliasing for transient resources, pooling on/off image parity, resize and toggle lifetime safety across three frames in flight |
| 5 | Render Graph inspector | Read-only editor panel over the compiled-frame debug data: uses, schedule and culling reasons, lifetimes, transitions, reuse, per-pass timing, transient high-water mark, alias savings |
| 6 | Histogram exposure and bloom | Compute log-luminance histogram and exposure resolve feeding the existing pre-exposure point; threshold/downsample/upsample bloom chain on per-mip storage views; Render Settings toggles |
| 7 | Checkpoint A freeze and record | Named frozen conformance suite (upload/layout, views, sRGB, reversed-Z, storage hazards, load/store, indirect arguments, frame-slot retirement), `docs/milestones/m5.md`, roadmap baseline update |

## 4. RHI substrate design

All additions live under `RHI/Include/RHI/`, stay API-neutral, return `Result<T>` from creation,
treat misuse as `LMX_ASSERT`, and label every GPU object.

- `ComputePipeline` with a `ComputePipelineDesc` (shader function, label), compiled through the
  existing Slang → MSL → metallib path with the runtime-MSL fallback intact.
- `BufferDesc`/`TextureDesc` gain storage read/write usage flags. A new `TextureViewDesc` names a
  mip range, layer range, and optional sRGB/linear reinterpretation; it is the single subresource
  vocabulary shared by storage binding, copies, readback, and graph declarations.
- `CommandList` gains `dispatch`, `dispatchIndirect`, `drawIndirect`, general copy commands
  (buffer↔buffer, buffer↔texture, texture↔texture, per subresource), and an explicit barrier API
  that is honest about Metal 4's untracked-resource model. Each command documents its visibility
  and synchronization contract.
- Indirect argument structs are RHI-defined (layout, alignment, offset rules), not Metal imports.

## 5. Render graph design

- Passes carry a kind (raster, compute, copy). Declared uses carry subresource ranges through
  `TextureViewDesc`. Read-before-write validation extends to per-subresource granularity; invalid
  declarations still fail before anything reaches the GPU.
- Compilation after validation: deterministic dead-pass culling by reverse reachability from
  exported and presented resources, then lifetime analysis, then conservative transient pooling —
  only transient resources with disjoint lifetimes alias into placement heaps, never imported or
  exported resources, with full barriers at reuse boundaries. Pooling has a runtime toggle and must
  produce identical output on and off.
- Compilation emits one `CompiledFrameDebug` structure — passes, uses, schedule, culling reasons,
  lifetimes, transitions, logical-to-physical assignments, memory totals — consumed by exactly two
  clients: the deterministic text dump (stable ordering, golden-file testable, triggered by an
  environment variable or the editor) and the read-only inspector panel.

## 6. Features

- **Histogram exposure**: one compute pass accumulates a log-luminance histogram of scene color in
  a storage buffer; a resolve pass trims percentiles and produces the exposure value consumed by
  the existing pre-exposure point. Lighting math is unchanged. Metering parameters are visible in
  Render Settings.
- **Bloom**: threshold/prefilter, a downsample chain into the mips of one transient FP16 texture
  through per-mip storage views, an upsample-accumulate pass, and composition before the display
  transform. Both features are ordinary declared graph passes: they appear in the dump, the
  inspector, per-pass timing, and dead-pass culling when toggled off.

## 7. Testing

- Every stage lands with its conformance tests. GPU tests run under `MTL_DEBUG_LAYER=1` on Metal 4
  Apple Silicon; hosted CI compiles and inventories them.
- Graph compilation — culling, lifetimes, pooling assignment — is deterministic CPU logic covered
  in `Tests/unit` without a GPU. Dump output is golden-file tested.
- Pooling parity is an image comparison with pooling forced on and off. A resize/toggle stress test
  cycles window sizes and feature toggles across the three-frame pipeline and asserts no leaks and
  no reuse of live resources.
- Stage 7 consolidates the semantic tests behind checkpoint A into a named conformance suite with a
  short manifest declaring them frozen; a future backend must pass the same suite.

## 8. Process

- Model tiers per the standing subagent policy: stages 1–4 are design-sensitive RHI/graph work at
  the highest implementation tier with main-thread review; stages 5–7 are standard implementation
  reviewed one tier up.
- Integration follows `docs/conventions/commits.md`: each stage is one short-lived outcome branch
  (`feat/<outcome>` or `docs/<outcome>`) that passes formatting, the relevant tests, and policy,
  then merges to `main` through its own pull request. `main` stays always buildable; no
  milestone-wide integration branch exists.

## 9. Non-goals

Per the roadmap's M5 deferrals: motion/history semantics, dynamic resolution, GPU scene ownership,
bindless materials, indirect visibility, and alternative opaque surface paths. Additionally out of
scope: GPU-side indirect work generation (M7), automatic exposure as a default mode, graph
optimization beyond dead-pass culling and conservative pooling, and any M5.1 interface-experiment
work.

## 10. Exit gate

The roadmap's M5 exit gate applies verbatim: compute-to-sample and per-mip hazards pass conformance
tests; resize and feature toggles neither leak nor reuse live resources; pooling on and off produce
the same output; arbitrary intermediate mips and layers can be captured; the same compiled frame is
inspectable through the deterministic dump and the editor; and pass time, transient high-water
marks, and alias savings are visible.
