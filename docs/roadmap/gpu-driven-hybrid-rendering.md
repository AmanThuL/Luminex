# GPU-Driven Hybrid Rendering

**Status**: Accepted

Part II of the [rendering roadmap](../roadmap.md): M7–M11 and independently gated research grow
scene scale and visible rendering quality on the contracts in
[Rendering Foundations](rendering-foundations.md). These are accepted future boundaries, not
shipped capabilities. Numerical order alone does not determine when an area may start.

## Entry from the rendering foundation

M7.1 is implemented and owner-accepted; [its record](../milestones/m7.1.md) retains evidence and
limits. [Editor Experience](editor-experience.md#placement-and-ownership)
owns the accepted UX1-before-M7.1 delivery order, and
[Neural and Learned Rendering](neural-rendering.md#placement-and-ownership) owns the accepted
order after M7: N1 → M9 → M8 → M10 → M11. That order sets priority; the gates below are unchanged.
[Interface gate B](rendering-foundations.md#m6--temporal-and-display-foundation) explicitly
approves entry in its [2026-09-13 review](../milestones/interface-gate-b.md), after the structural
[R1](codebase-refactoring.md#r1--module-boundaries-and-shared-foundations) milestone.
The review verifies ADR 0010 conformance and the temporal, root-data, binding, synchronization
and capability contracts; [ADR 0021](../decisions/0021-gpu-scene-handoff-contract.md) records the
minimal scene-identity/update semantics the first consumers must implement. M6.5 closure and EDR
DEFER did not by themselves pass the review. M7.1 implementation and validation, including Xcode
Replay, are complete. On 2026-09-15 the owner accepted the result after manual verification,
including the scoped MetalFX profile (15/15); original image criteria remain 11/15.
[Its record](../milestones/m7.1.md) owns both results and limits. The executor plan is closed;
the owner approved main integration on 2026-09-15. [M7.2 acceptance](../milestones/m7.2-validation.md#owner-acceptance-and-integration) also authorizes integration after manual review on that date, retaining original/revised failed image gates (13/15 and 9/15); its plan is closed.

The current implementation supplies shared scene identities and paced GPU tables to the retained
CPU draw path. `SceneView` borrows row selectors, mesh ranges and resolved texture pointers;
instance/material rows carry transforms, motion and material factors. ShadowStage and SceneStage
retain one indexed command and texture binding set per object, with shared geometry and table
bindings. The [architecture](../architecture/overview.md) owns the current implementation details.
M7.2 implements the CPU visibility reference and indirect baseline, owner-accepted for integration; GPU frustum classification
remains M7.3 scope and measures against that reference; M7.5 adds point/spot lights before clustered
assignment. Existing three-directional-light semantics and temporal inputs remain the reference.

Carry forward the [foundation's acceptance limits](rendering-foundations.md#foundation-and-handoff).
M5.6 does not establish a production speedup or select ICB. The M6.4 manual/capture checks and
M6.5 parity investigation remain open follow-ups; entering M7 does not resolve them. Gate B owns
technical entry requirements; these follow-ups do not silently become additional milestone
prerequisites. The separately accepted editor delivery order does not reopen gate B.

## Dependency map

This table summarizes the prerequisites below; each area's own gate governs acceptance. Eligible
areas can be ordered independently within the accepted delivery order, while only one
implementation plan may be active. The entry row records technical dependencies; the intervening
editor work is defined only in [Part IV](editor-experience.md#placement-and-ownership).

| Area | Required foundation | Independent ordering |
|---|---|---|
| M7 scene, visibility and lighting | Gate B → M7.1; M7.2 → M7.3 → M7.4 | M7.5 can follow M7.1 before the visibility slices |
| M8 shadows and composition | M7 scene/lighting, M6 temporal; depth/HZB and surface guides for screen-space effects | M8.1 → M8.2; M8.3 and M8.4 need only M7; M8.5 follows M8.4; a page-cached (VSM-style) atlas is eligible inside M8.2 |
| M9 geometry and surface paths | M7 scene/visibility/Forward+, M6 temporal | Cluster LOD and cluster culling first; M8 is not a prerequisite and follows M9 in the accepted order |
| M10 query and transport reference | M7 scene/light data, M6 temporal/capture | Query/reference work need not wait for M9 or all of M8 |
| M10 real-time reflections | Query/reference gates, then M8 SSR/probe fallback | Budget tracing, denoising and composition together |
| M11 GI | M10 query/reference/denoising and shared temporal/light contracts | Accepted separately from content residency |
| M11 residency | M7 identity/safe updates and measured content pressure; M9 coarse LOD for geometry streaming | Ordinary loading/mip streaming need not wait for M10 or GI |
| Independent extensions/research | Their own fallback, oracle, budget and host gates below | Area lights and stochastic direct lighting follow M7.5; learned-rendering slices are owned by [Part V](neural-rendering.md#dependency-map) |

**Hardware floor.** Mesh shaders and hardware ray tracing require Apple M3/A17 Pro or later; GPU
neural acceleration requires M5/A19 Pro or later. Every slice below that depends on one of these
says so and keeps a path for earlier Apple Silicon.

The first scaling reference is the current raster/material/temporal frame. Later surface, ray and
cache paths must consume the same meanings for material, light, motion and exposure. Introduce a
shared guide or RHI capability with its first real consumer and keep its fallback inspectable.

## M7 — Scalable scene and direct lighting

**Outcome:** the existing scenes use shared GPU data, validated GPU visibility and bounded local lighting. M7 finishes when these five slices pass; LOD, transparency and advanced command mechanisms do not extend its completion boundary.

**Deliver:** shared GPU scene data, a CPU visibility reference with an indirect-draw baseline, GPU frustum visibility with measured work generation, conservative occlusion, and clustered point/spot lighting through M7.1–M7.5.

**Sequence:** gate B → M7.1 → M7.2 → M7.3 → M7.4; M7.5 can follow M7.1 without waiting for the visibility slices. M7.2 reuses M5.6's workload/oracle contract in a production benchmark; leave frozen experiment artifacts untouched and do not require an unaccepted result or unavailable final tag. Native-host timings never establish production speedups. Validate GPU classification, CPU command count and full-frame benefit separately. Adopt a GPU-generated default only with representative production evidence; preserve a CPU/batched fallback and disclose remaining per-object encoding. ICB is optional. Each implemented backend passes the same semantic cases or declares a tested capability fallback.

**Exit gate:** all five slices pass their individual gates and the shared production evidence rules. No LOD, transparency or ICB work extends M7's completion boundary.

**Defer:** cluster LOD and its offline data to M9; basic transparency to M8; area lights to the independent light-model extension below; streaming/general residency to M11. Also defer meshlets, two-phase occlusion optimization, cascaded/cached shadows, ray queries and dynamic GI.

## M7.1 — GPU scene foundation

**Outcome:** Existing scenes draw correctly from shared GPU identities and tables through the retained CPU path.

**Deliver:** Minimal stable instance/mesh/material/texture IDs and GPU tables, material indexing and the binding/update path current scenes need, inside the R1.4 shadow/scene stages; tables carry every per-draw input those stages already consume, including alpha mode, cutoff, double-sided flag, previous transform and motion class. Retain CPU drawing as the first consumer and turn the [ADR 0021](../decisions/0021-gpu-scene-handoff-contract.md) requirements into the identity-sharing, update/reorder, remove/reuse, scene-replacement, texture-fallback, capacity-growth and three-frame-overlap fixtures.

**Exit gate:** Existing scenes preserve material and temporal output; bounded update/remap/fallback fixtures keep identities and resources valid across three frames; masked coverage and motion classes agree with the pre-table path; no general scene or residency system is required.

**Defer:** Visibility to M7.2–M7.3, occlusion to M7.4 and local lighting to M7.5; general scene/residency systems remain out of scope.

## M7.2 — CPU visibility reference and indirect baseline

**Integration decision:** Owner-accepted on 2026-09-15 after manual review; [durable acceptance](../milestones/m7.2-validation.md#owner-acceptance-and-integration) retains the original 13/15 and revised 9/15 image gates as failures. This permits integration without relabelling those gates, introducing tolerance or claiming performance adoption. M7.3 is owner-accepted for integration on 2026-09-16.

**Outcome:** A maintained CPU frustum-visibility oracle and a competent indirect-draw production baseline over the shared tables give M7.3 a measured reference.

**Deliver:** Per-instance world bounds from the M7.1 tables, CPU frustum classification with an unculled bypass, an indirect-draw baseline that consumes the existing RHI argument layouts, VisibilityLab with deterministic camera tracks and controlled instance counts, and the paired measurement harness that reuses M5.6's workload/oracle contract; report CPU work/calls, GPU preparation, full-frame cost and memory.

**Exit gate:** The oracle's visible set and image match the unculled reference within declared boundary rules; the indirect baseline reproduces the per-object path's images on every catalog scene; measurements are paired on the frozen device with variation reported; rejected instances and bypass reasons are inspectable.

**Defer:** GPU classification, compaction and work generation to M7.3; occlusion to M7.4; cluster LOD to M9.

## M7.3 — GPU visibility and work generation

**Implementation state:** Implemented; [owner-accepted for integration](../milestones/m7.3-validation.md#owner-acceptance-and-integration) on 2026-09-16. The [validation record](../milestones/m7.3-validation.md) retains both 14/15 failed GPU/CPU exact-image gates and the incomplete ICB capture gate. CPU/indirect remains the default.

**Outcome:** GPU frustum visibility and work generation have a validated production path measured against the M7.2 reference.

**Deliver:** GPU frustum culling, compaction and work generation over the shared tables through supported execution, with explicit capacities, overflow policy and counters; the M7.2 harness compares both paths under the same sequences.

**Exit gate:** CPU/GPU visible sets and final images match within declared boundary rules; counters reconcile candidates and emitted work; bounded capacity overflow is safe; paired production measurements report CPU work/calls, GPU preparation, full-frame cost and memory against M7.2.

**Defer:** Temporal occlusion to M7.4 and cluster LOD to M9; ICB remains optional and native-host results do not select a production default.

## M7.4 — Conservative occlusion

**Implementation state:** [Implemented](../milestones/m7.4.md); automated and [native verification](../milestones/m7.4-editor-validation.md) are recorded; owner review remains pending. The [validation record](../milestones/m7.4-validation.md) retains the failed 13/15 exact-image gate and mixed performance costs. Occlusion is opt-in and off by default; no integration approval follows.

**Outcome:** A conservative HZB and spatial occlusion test reject work hidden in the previous frame; temporal validity is approximate, strict where view and coverage are unchanged or invalidated, and bounded by a recovery deadline under continuous camera motion.

**Deliver:** Current/previous HZB and previous-view occlusion on fixed geometry; first validate each pyramid mip, then consume it for rejection in VisibilityLab. An independent unculled ID reference measures falsely rejected visible instances, affected pixels and consecutive missing frames.

**Exit gate:** Reported separately: every pyramid level is conservative under reversed-Z; a static camera, camera cuts, teleports, new objects and moved, removed or coverage-changed occluders lose no visible geometry, with occluder changes invalidating the affected history; under continuous camera motion a reference-visible instance is drawn within the declared recovery deadline, and persistent omissions fail; rejected bounds and bypass reasons are inspectable against the unculled reference; paired performance is reported without adopting a default.

**Defer:** Cluster LOD to M9. Two-phase occlusion, the mechanism that recovers newly visible geometry within the current frame, stays beyond M7, so disocclusion latency during continuous motion is accepted and disclosed.

## M7.5 — Clustered local lighting

**Outcome:** Opaque scenes support bounded point/spot lighting through a validated clustered Forward+ path.

**Deliver:** Point/spot units, attenuation and a small direct-loop reference first, then clustered Forward+ opaque shading with explicit list capacities and overflow policy; LightLab exercises scale.

**Exit gate:** Clustered results agree with the direct-loop reference; overlapping/moving lights remain bounded with visible overflow; light assignment and full lighting cost are inspectable; no local shadows or area-light model is required.

**Defer:** Local shadows and basic transparency to M8; area lights to the independent light-model extension; stochastic direct lighting (ReSTIR-DI/MegaLights-class) to the independent study below, which uses this clustered path as its reference.

## M8 — Shadows, indirect-lighting floor and environment

**Outcome:** dependable shadows, screen-space/probe lighting, atmosphere and transparent composition extend the shared frame with visible quality gains and bounded costs. M8 finishes when the five slices below pass.

**Deliver:** through M8.1–M8.5:

- Validate stable sun cascades/per-cascade culling, PCF and corrected PCSS, then a budgeted local-light shadow atlas/cache; a page-cached (VSM-style) atlas with ordinary fallback is an eligible form of that cache, since the technique is documented down to Apple M2.
- Add GTAO-class ambient occlusion (a visibility-bitmask implementation is preferred) and SSR with local reflection-probe/sky fallback, shared surface guides and confidence/history views.
- Establish basic sorted premultiplied Forward+ transparency with shared lighting, followed by refraction/reactive masks and baseline particles.
- Add environment/atmosphere LUTs and analytic height fog before temporal froxel fog and opaque/transparent transmittance integration.

**Prerequisites:** use M7 scene/lighting and M6 temporal contracts. The screen-space/probe area needs the M7.4 HZB and its actual surface guides; in the accepted order M9's cluster geometry ships first, and M8 consumes whichever opaque path M9 retains without depending on its experiments. Establish basic transparency here before any transparent fog/refraction integration; it is no longer a M7 gate.

**Sequence:** M8.1 → M8.2; M8.3 and M8.4 need only the M7 contracts and may follow M8.1 in either order; M8.5 follows M8.4.

**Exit gate:** cascades remain stable and bias is inspectable; atlas allocation, eviction and invalidation are deterministic; AO does not double-darken indirect energy; invalid SSR always has a declared fallback. Transparency shares light evaluation, fog composition agrees across surfaces, and temporal effects expose rejection/coverage without persistent trails. Alpha-test coverage agrees across supported depth, shadow, color and motion passes. Extend LightLab, TransportLab, TransparencyLab and a bounded OutdoorLab only for these checks.

**Defer:** area lights, virtualized shadow caches without an ordinary fallback, clouds/weather, advanced OIT (adaptive voxel OIT is a named later study), opaque-path replacement, hardware ray effects, stochastic direct lighting and full specialist material systems.

## M8.1 — Sun cascades

**Outcome:** The directional caster's shadow is stable, culled per cascade and filtered with corrected PCF/PCSS.

**Deliver:** Stable cascade splits and texel snapping, per-cascade culling over the M7.3 visibility path, PCF and corrected PCSS with inspectable bias/normal-offset controls; OutdoorLab exercises cascade transitions on a fixed camera rail.

**Exit gate:** Cascade edges and bias are stable under camera motion within frozen tolerances; per-cascade culling agrees with the unculled reference; masked coverage agrees between depth and shadow passes; cascade cost is reported per split.

**Defer:** Local-light shadows to M8.2; cached or virtualized sun shadows without an ordinary fallback.

## M8.2 — Local-light shadow atlas

**Outcome:** Point/spot lights from M7.5 cast budgeted shadows through a deterministic atlas/cache.

**Deliver:** A budgeted atlas with allocation, eviction and invalidation rules over the clustered light set; a page-cached (VSM-style) form is eligible with the ordinary atlas as fallback and reference; LightLab exercises overlap and churn.

**Exit gate:** Allocation, eviction and invalidation are deterministic and inspectable; overflow degrades to unshadowed lights without holes; both atlas forms agree on the reference scenes; atlas cost and memory are bounded and reported.

**Defer:** Area-light shadows to the independent light-model extension; ray-traced shadows to the stochastic direct-lighting study, whose shadowed variant needs M10 queries.

## M8.3 — Screen-space and probe lighting

**Outcome:** GTAO-class occlusion and SSR with probe/sky fallback give an indirect-lighting floor over shared surface guides.

**Deliver:** Visibility-bitmask GTAO, SSR over the M7.4 HZB with local reflection-probe/sky fallback, the shared depth/normal/roughness guides those consumers need, and confidence/history views; TransportLab compares against the ambient and IBL baseline.

**Exit gate:** AO does not double-darken indirect energy; invalid SSR always has a declared fallback; temporal effects expose rejection/coverage without persistent trails; guides are grown only for these consumers.

**Defer:** Ray-traced reflections to M10; probe volumes and dynamic GI to M11.

## M8.4 — Transparency

**Outcome:** Sorted premultiplied Forward+ transparency shares the opaque light evaluation, followed by refraction, reactive masks and baseline particles.

**Deliver:** glTF BLEND materials as sorted premultiplied surfaces lit through the M7.5 clustered path, reactive masks for the temporal contract, then refraction and baseline particles; TransparencyLab exercises ordering, overlap and motion.

**Exit gate:** Transparent surfaces share light evaluation with opaque ones; ordering and reactive rejection are inspectable; alpha-test and blend coverage agree across supported depth, shadow, color and motion passes; [ADR 0018](../decisions/0018-masked-material-coverage.md) is superseded explicitly.

**Defer:** Order-independent transparency (adaptive voxel OIT is a named later study); transparent fog and transmittance integration to M8.5.

## M8.5 — Atmosphere and fog

**Outcome:** Environment/atmosphere LUTs and fog compose consistently over opaque and transparent surfaces.

**Deliver:** Sky/atmosphere LUTs, analytic height fog, then temporal froxel fog with opaque/transparent transmittance integration; a bounded OutdoorLab covers time of day and camera motion.

**Exit gate:** Fog composition agrees across surfaces; froxel history exposes rejection and coverage; atmosphere output respects scene-linear pre-exposure and the display transform.

**Defer:** Clouds/weather; volumetric shadows beyond froxel transmittance.

## M9 — Geometry LOD and surface-path experiments

**Outcome:** cluster geometry with GPU cluster culling and a measured opaque-path choice improve representative workloads on native Metal, with published measurements, while ordinary raster and shared material semantics remain a reliable reference.

**Deliver:** offline cluster LOD through a maintained clusterization library (meshlets, bounds/cones, hierarchical simplification) with runtime selection/transition; GPU cluster culling and ordinary indirect cluster raster over the M7 visibility path; optional mesh-shader execution on M3/A17 Pro and later with the vertex/compute cluster path as the fallback and reference; separately compare compact/tile-local deferred and visibility-buffer material reconstruction/classification. Use required attachment/load-store/tile support only for the measured path, with materialized fallbacks. Extend MipLab and VisibilityLab for derivatives, alpha coverage, LOD and tiny geometry. Publish a native-Metal measurement table for cluster culling and raster on the frozen device and workloads; such numbers are nearly absent from the public corpus.

**Prerequisites:** M7 scene/visibility (M7.1–M7.4) and Forward+ (M7.5) plus M6 temporal contracts. M8 is not a prerequisite and follows M9 in the accepted order; its screen-space consumers later pressure-test the retained surface outputs. GTAO and SSR/probes belong to M8 and do not depend on an opaque-path experiment winning.

**Exit gate:** cluster LOD selection and transitions meet declared error/stability limits; supported surface paths agree on material references; the mesh-shader path, when built, matches the fallback path's visible set and image. Measure cull/raster/resolve/shading and downstream cost, memory, overdraw and available bandwidth/tile/occupancy counters; mark unavailable metrics explicitly. Record adopt/retain/defer per platform/workload, retain the Forward+ oracle, and rerun the suite on representative Windows hardware once D3D12 exists.

**Defer:** Nanite-class virtualized geometry streaming and software rasterization, mandatory mesh shaders, general asset tooling, scene-query implementation and GI; those separate work areas do not wait for M9 to select an opaque winner.

## M10 — Hybrid scene query and reference transport

**Outcome:** optional scene queries and a deterministic transport oracle validate shared materials and lighting before a real-time ray signal is adopted.

**Deliver:** after M7 shared scene/light data and M6 temporal/capture contracts, first validate AS build/refit/compaction, inline queries, proxy classes and mismatch views; then canonical BSDF/light sampling and a progressive path-trace oracle in TransportLab. Neither waits for M9 or all of M8. Real-time reflections follow those gates and M8's SSR/probe fallback, with an inspectable native temporal/spatial denoiser and a capability-selected vendor-adapter boundary; [N3](neural-rendering.md#n3--denoiser-and-reconstruction-adapters) supplies the MetalFX denoising adapter over M10's signal. Hardware ray tracing requires M3/A17 Pro or later, and Slang's Metal target does not yet generate ray-tracing code; the M10 plan records how it reaches queries before measurement.

**Exit gate:** AS time/memory are bounded before real-time reflections begin; unsupported geometry has visible fallback; raster and reference transport agree on controlled scenes. Capture noisy signals, guides, variance/history, source/confidence and final output; budget AS, tracing, hit shading, denoising and composition together. Motion/disocclusion reject invalid history, and disabling RT preserves material/light semantics.

**Defer:** ray-only default rendering, stochastic direct-light replacement, production dynamic GI, mandatory vendor denoising and sparse residency.

## M11 — Dynamic GI and content residency

**Outcome:** independently accepted GI and content-scaling work extends the shared frame; neither area makes high-end hardware capabilities mandatory.

**Deliver:**

- **GI:** after M10 query/reference/denoising and the shared temporal/light contracts, establish a portable probe-volume floor (DDGI-class irradiance/distance probes need no hardware ray tracing), then evaluate one cascaded-probe, surfel or hashed world-space radiance cache with explicit time, memory and update budgets; radiance cascades and learned caches remain research until that comparison exists. Capture source coverage, age, leaks, rejected history and complete composite cost in dynamic-light/occluder TransportLab sequences. Record adopt/retain/defer; adoption must materially improve on the floor and define transparent/froxel fallback.

- **Residency:** can start after M7 identity, safe updates and measurable content pressure, without waiting for M10 or GI. Add background I/O through Metal fast resource loading, ordinary loading and mip streaming with a renderer-written tile-feedback buffer before geometry streaming, which also needs M9's cluster LOD/coarse assets. Track uploaded/queued bytes, residency, eviction, stalls and fallback use. Sparse pages require evidence that ordinary streaming is insufficient; no GI-cache implementation is a prerequisite.

**Exit gate:** each area meets its own quality/cost and failure checks. Forced content budgets degrade to resident coarse data without holes or use-after-free; dynamic GI meets its recorded decision gate. Accept areas separately; start detailed plans only when their prerequisites exist.

**Defer:** sparse pages until ordinary streaming proves insufficient; geometry streaming until M9 supplies cluster LOD/coarse assets.

## Independent research and graduation gates

Learned-rendering studies, including the tiny-network inference lab, learned reconstruction, denoiser adapters and material distillation, are owned by [Part V](neural-rendering.md) with their own gates; they are not conditions of any M-slice. Hybrid mesh/splat selection needs paired representations, error oracles and depth/transparency/temporal composition contracts after M8 and M9; the ratified glTF splat extension and compressed interchange formats do not change that. Splat conformance remains a separate pinned-specification corpus/reference tool. These are optional studies, not commitments to all ideas.

Area lights are a separate light-model extension after M7.5: select one shape, evaluation method, units and numerical/image reference before implementation; shadow support needs its own boundary. They are not implied by point/spot clustering and are not a condition of finishing M7 or M8. Stochastic direct lighting (ReSTIR-DI/MegaLights-class) is a separate study after M7.5 with the clustered path as reference; its shadowed variant needs M10 queries, and the shadow-map variant may precede them.

D3D12 Work Graphs are reported to have stopped advancing beyond Shader Model 6.9, with a spec-only "Work Lists" successor targeting a 2027 preview; Vulkan device-generated commands have uneven driver support. Either study needs the basic submission benchmark and a same-device comparison, and Metal indirect command buffers already cover the GPU-generated-command case. Mesh/task studies need M9 geometry controls. Async scheduling needs measured overlap plus a matching serial fallback. Virtualized geometry streaming, adaptive voxel OIT, ReSTIR and frame generation remain research; frame generation also requires stable real-frame timing, frame identity, latency and UI pacing, with MetalFX Frame Interpolation as the concrete Metal candidate once those exist. Each study freezes a target, fallback, budget, oracle and debugging surface before measurement. Compare APIs on the same device where possible; cross-device results describe systems. Preserve immutable `exp/<topic>` evidence and graduate production changes through explicit roadmap/ADR decisions.
