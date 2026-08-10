# M5 — Execution Substrate and Observability

**Status**: In progress

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
2. Extend `BufferDesc`/`TextureDesc` with storage read/write usage flags and add `TextureViewDesc`
   (mip range, layer range, optional sRGB/linear reinterpretation) as the shared subresource
   vocabulary. Views of storage-usable textures bind as storage images.
3. Extend `CommandList` with `dispatch` and an explicit barrier API whose visibility contract is
   documented per command and maps to Metal 4's untracked-resource barrier primitives.
4. Add a compute test oracle shader and GPU conformance cases: dispatch writing a storage buffer
   with readback, storage-texture write then compute read, and compute write then fragment sample
   across a barrier (compute-to-sample hazard).

Exit: new conformance cases pass under Metal validation; core RHI headers still compile in
isolation without Metal or ImGui types; all prior coverage stays green.

## Stage 2 — Copies, subresource uses, indirect (`feat/rhi-copy-subresource-indirect`)

1. Add general copy commands — buffer↔buffer, buffer↔texture, texture↔texture — addressing
   subresources through the `TextureViewDesc` vocabulary, plus per-mip/per-layer barriers.
2. Support readback of arbitrary intermediate mips and layers, with a GPU test capturing a chosen
   mip/layer of a rendered chain and asserting its contents.
3. Define RHI-owned indirect argument structs with documented layout, alignment, and offset rules;
   add `dispatchIndirect` and `drawIndirect` consuming a caller-filled GPU buffer.
4. Add conformance cases for indirect arguments written by the CPU and by a prior compute pass,
   including nonzero offsets.

Exit: copy, subresource, and indirect conformance cases pass under Metal validation; argument
layout rules are documented in the public headers.

## Stage 3 — Graph semantics, culling, dump (`feat/graph-compute-culling-dump`)

1. Give `RenderGraph` passes a kind (raster, compute, copy) and declaration paths for compute and
   copy passes; declared uses carry subresource ranges; read-before-write validation becomes
   per-subresource. Existing passes keep whole-resource declarations.
2. Add deterministic dead-pass culling by reverse reachability from exported and presented
   resources, with stable ordering and a recorded reason per culled pass.
3. Emit one `CompiledFrameDebug` structure from compilation — passes, uses, schedule, culling
   reasons, lifetimes, transitions, logical-to-physical assignments, memory totals.
4. Add a deterministic text dump of `CompiledFrameDebug` with stable ordering, triggered by an
   environment variable or the editor, and golden-file unit tests over representative graphs,
   including a culled-pass graph.

Exit: graph unit tests cover per-subresource validation, culling determinism, and dump golden
files; declared frames render unchanged; all GPU coverage stays green.

## Stage 4 — Transient pooling (`feat/graph-transient-pooling`)

1. Add lifetime analysis over the compiled schedule and conservative transient pooling: only
   transient resources with disjoint lifetimes alias into placement heaps, never imported or
   exported resources, with full barriers at reuse boundaries.
2. Add a runtime pooling toggle; record logical-to-physical assignments, transient high-water
   mark, and alias savings in `CompiledFrameDebug` and the dump.
3. Add an image-comparison test asserting identical output with pooling forced on and off, and a
   resize/feature-toggle stress test cycling window sizes and pass sets across the three-frame
   pipeline asserting no leaks and no reuse of live resources.

Exit: pooling parity and stress tests pass under Metal validation; pooled frames respect
frame-slot retirement; dump reflects assignments deterministically.

## Stage 5 — Render Graph inspector (`feat/editor-graph-inspector`)

1. Add a read-only editor panel over the newest retired frame's `CompiledFrameDebug` and per-pass
   GPU timing: pass/resource uses, schedule order, culled passes with reasons, lifetimes,
   transitions, logical-to-physical reuse, transient high-water mark, and alias savings.
2. Keep the panel a pure consumer: no graph mutation, no new data sources beyond
   `CompiledFrameDebug` and existing timing.
3. Add focused tests for the panel's data-shaping logic in `Tests/unit` and a GPU smoke case
   confirming the inspector-visible frame matches the dump for the same frame.

Exit: the same compiled frame is inspectable through the dump and the editor with matching
content; pass time, transient high-water marks, and alias savings are visible in the panel.

## Stage 6 — Histogram exposure and bloom (`feat/histogram-exposure-bloom`)

1. Add a compute log-luminance histogram pass over scene color into a storage buffer and a resolve
   pass trimming percentiles into an exposure value consumed by the existing pre-exposure point;
   lighting math is unchanged. Manual exposure stays the default; auto-exposure is a Render
   Settings opt-in with visible metering parameters.
2. Add the bloom chain — threshold/prefilter, downsample through per-mip storage views of one
   transient FP16 texture, upsample-accumulate, composite before the display transform — enabled
   by default and declared as ordinary graph passes.
3. Add tests: a CPU-reference histogram comparison on a known image, deterministic exposure
   resolve, a bloom threshold/energy oracle, culling of toggled-off feature passes, and parity
   with pre-M5 output when both features are disabled.

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
