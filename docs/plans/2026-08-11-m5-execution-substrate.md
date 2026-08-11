# M5 — Execution Substrate and Observability

**Status**: Implemented (2026-08-11)

This plan executes the accepted design in
`docs/specs/2026-08-11-m5-execution-substrate-design.md`. `docs/roadmap.md` owns the M5 outcome,
deliverables, exit gate, portability checkpoint A, and deferrals; this plan decomposes that
boundary without expanding it.

## Outcome

The RHI and render graph can express, validate, inspect, and safely reuse the compute and resource
workloads required by later temporal and GPU-driven features. The same compiled frame is
inspectable through a deterministic dump and a read-only editor panel; histogram exposure and a
bloom chain exercise the substrate while the deterministic manual-exposure path remains the
default.

## Integration

Each stage is one short-lived outcome branch that passes `xmake format --check`, `xmake policy`,
the relevant unit and GPU tests, and its stage evidence, then merges to `main` through its own
pull request per `docs/conventions/commits.md`. GPU coverage runs as
`MTL_DEBUG_LAYER=1 xmake test Tests/gpu` on Metal 4 Apple Silicon before each merge.

## Stage 1 — RHI compute and storage (`feat/rhi-compute-storage`)

1. Add `ComputePipeline` and `ComputePipelineDesc` (shader function, label) to `RHI/Include/RHI/`,
   created through the device with `Result<T>` and a mandatory label, compiled via the existing
   Slang → MSL → metallib path with the runtime-MSL fallback intact.
2. Extend `BufferDesc`/`TextureDesc` with storage read/write usage flags and add
   `TextureSubresourceRange` (mip/layer ranges) plus `TextureViewDesc` (a range with optional
   sRGB/linear reinterpretation, validated against the format family) for binding.
3. Extend `CommandList` with a compute pass scope — `beginComputePass`/`endComputePass`,
   `bindComputePipeline`, `bindStorageBuffer`/`bindStorageTexture` with declared read/write
   access, `dispatch` — and an explicit range-aware barrier API whose visibility contract is
   documented per command and maps to Metal 4's untracked-resource barrier primitives.
4. Extend per-pass GPU timing to every pass kind and make retired timing publication carry the
   frame number it measured.
5. Add a compute test oracle shader and GPU conformance cases: dispatch writing a storage buffer
   with readback, storage-texture write then compute read, and compute write then fragment sample
   across a barrier (compute-to-sample hazard).

Exit: new conformance cases pass under Metal validation; core RHI headers still compile in
isolation without Metal or ImGui types; all prior coverage stays green.

## Stage 2 — Copies, subresource uses, indirect (`feat/rhi-copy-subresource-indirect`)

1. Add a copy pass scope — `beginCopyPass`/`endCopyPass` — with general copy commands
   (buffer↔buffer, buffer↔texture, texture↔texture) addressed by `TextureCopyRegion` and
   `BufferTextureLayout` (origin, extent, buffer offset, row/slice pitch), `fillBuffer`, and
   per-mip/per-layer barriers.
2. Support readback of arbitrary intermediate mips and layers, with a GPU test capturing a chosen
   mip/layer of a rendered chain and asserting its contents.
3. Define RHI-owned indirect argument structs with documented layout, alignment, and offset rules;
   add `dispatchIndirect`, `drawIndirect`, and `drawIndexedIndirect` consuming a caller-filled
   GPU buffer.
4. Add conformance cases for indirect arguments written by the CPU and by a prior compute pass,
   including nonzero offsets and the indexed variant.

Exit: copy, subresource, and indirect conformance cases pass under Metal validation; argument
layout rules are documented in the public headers.

## Stage 3 — Graph semantics, culling, dump (`feat/graph-compute-culling-dump`)

1. Give `RenderGraph` passes a kind (raster, compute, copy) and declaration paths for compute and
   copy passes; declared uses carry `TextureSubresourceRange`s under whole-resource versions with
   untouched-subresource inheritance; same-pass read/write ranges must be disjoint;
   read-before-write validation becomes per-subresource. Existing passes keep whole-resource
   declarations.
2. Add `exportBuffer` beside `exportTexture`, and deterministic dead-pass culling by reverse
   reachability from explicit sinks only — exports, presentation, declared readback — with stable
   ordering and a recorded reason per culled pass. No generic side-effect flag.
3. Emit a `CompiledFrameDebug` from compilation — passes, uses, schedule, culling reasons,
   transitions — wrapped in a `CompiledFrameRecord` carrying a monotonic `frameId`; the App
   retains records for the three frames that can be in flight and joins retired GPU timings to
   the record they measured.
4. Add a deterministic text dump of a `CompiledFrameRecord` with stable ordering and no GPU
   timings or driver-reported values, triggered by an environment variable or the editor, and
   golden-file unit tests over representative graphs, including a culled-pass graph.

Exit: graph unit tests cover per-subresource validation, culling determinism, and dump golden
files; declared frames render unchanged; all GPU coverage stays green.

## Stage 4 — Transient ownership and pooling (`feat/graph-transient-pooling`)

1. Add graph `createTexture`/`createBuffer` declarations for one-frame transient resources, and
   record the superseding ADR that replaces ADR 0005's import-only rule.
2. Add lifetime analysis over the compiled schedule and conservative transient pooling: only
   graph-created resources with disjoint lifetimes alias into placement heaps — never imported or
   exported resources — with full barriers at reuse boundaries and compatibility constrained by
   format, usage, storage mode, size, and alignment.
3. Own physical pools per frame slot, reused only after `beginFrame` confirms the slot retired;
   resize and feature toggles create a new pool generation released after its last in-flight
   frame retires.
4. Add a runtime pooling toggle; record lifetimes, logical-to-physical assignments, transient
   high-water mark, and alias savings in `CompiledFrameDebug` and the dump.
5. Add an image-comparison test asserting identical output with pooling forced on and off, and a
   resize/feature-toggle stress test cycling window sizes and pass sets across the three-frame
   pipeline asserting no leaks and no reuse of live resources.

Exit: pooling parity and stress tests pass under Metal validation; pooled frames respect
frame-slot retirement; dump reflects assignments deterministically.

## Stage 5 — Render Graph inspector (`feat/editor-graph-inspector`)

1. Add a read-only editor panel over the newest retired frame's `CompiledFrameRecord` joined with
   its published GPU timings: pass/resource uses, schedule order, culled passes with reasons,
   lifetimes, transitions, logical-to-physical reuse, transient high-water mark, and alias
   savings.
2. Keep the panel a pure consumer: no graph mutation, no new data sources beyond the retained
   records and published timing.
3. Add focused tests for the panel's data-shaping logic in `Tests/unit` and a GPU smoke case
   confirming the inspector-visible frame matches the dump for the same frame.

Exit: the same compiled frame is inspectable through the dump and the editor with matching
content; pass time, transient high-water marks, and alias savings are visible in the panel.

## Stage 6 — Histogram exposure and bloom (`feat/histogram-exposure-bloom`)

1. Add a compute log-luminance histogram pass over frame N's pre-exposed scene color (its storage
   buffer cleared each frame by `fillBuffer`) and a resolve pass that compensates for frame N's
   `preExposure`, trims percentiles, and writes the instantaneous target exposure applied at frame
   N+1 through the exported exposure buffer; lighting math is unchanged. Manual exposure stays the
   default; auto-exposure is a Render Settings opt-in with visible metering parameters, and the
   loop initializes from and resets to the manual EV on first frame, scene switch, enable, and
   resize. Adaptation and smoothing stay in M6.
2. Add the bloom chain — threshold/prefilter defined on pre-exposed luminance, downsample through
   per-mip storage views of one graph-created FP16 texture, upsample-accumulate, composite before
   the display transform — enabled by default in editor and `--screenshot` alike, declared as
   ordinary graph passes.
3. Add tests: a CPU-reference histogram comparison on a known image, one-frame convergence of the
   target exposure at a known manual EV, every reset rule, a bloom threshold/energy oracle,
   culling of toggled-off feature passes, and parity with pre-M5 output when both features are
   disabled.

Exit: features render correctly in all scenes and appear in dump, inspector, timing, and culling;
disabled features reproduce pre-M5 output; all coverage stays green.

## Stage 7 — Checkpoint A freeze and record (`feat/checkpoint-a-conformance`)

1. Consolidate the semantic conformance cases — upload and layout, resource views, sRGB,
   reversed-Z, storage hazards, load/store behavior, indirect arguments, frame-slot retirement —
   into a named suite any future backend must pass unchanged.
2. Record the freeze as an ADR in `docs/decisions/` naming the suite and its covered semantics.
3. Write `docs/milestones/m5.md` with shipped behavior, evidence, and deliberate limits; update
   the roadmap's current-baseline section; refresh `AGENTS.md` where commands or architecture
   contracts changed; close this plan per `docs/conventions/documentation.md`.

Exit: the frozen suite passes; documentation checks pass; the milestone record truthfully reflects
the exit gate evidence.

## Non-goals

Per the roadmap's M5 deferrals: motion/history semantics, dynamic resolution, GPU scene ownership,
bindless materials, indirect visibility, and alternative opaque surface paths. Additionally out of
scope: GPU-side indirect work generation, automatic exposure as a default mode, graph optimization
beyond dead-pass culling and conservative pooling, and any M5.1 interface-experiment work.

## Validation

- `xmake format --check` and `xmake policy`
- `xmake test Tests/unit` and `xmake test`
- `MTL_DEBUG_LAYER=1 xmake test Tests/gpu` on Metal 4 Apple Silicon
- Offscreen captures of Sponza, Damaged Helmet, and MaterialLab with features on and off
- Dump golden files and pooling on/off image parity

## Exit gate

The roadmap's M5 exit gate applies verbatim: compute-to-sample and per-mip hazards pass
conformance tests; resize and feature toggles neither leak nor reuse live resources; pooling on
and off produce the same output; arbitrary intermediate mips and layers can be captured; the same
compiled frame is inspectable through a deterministic dump and the editor; pass time, transient
high-water marks, and alias savings are visible.
