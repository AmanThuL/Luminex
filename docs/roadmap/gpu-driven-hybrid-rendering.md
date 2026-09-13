# GPU-Driven Hybrid Rendering

**Status**: Accepted

Part II of the [rendering roadmap](../roadmap.md): M7–M11 and independently gated research grow
scene scale and visible rendering quality on the contracts in
[Rendering Foundations](rendering-foundations.md). These are accepted future boundaries, not
shipped capabilities. Numerical order alone does not determine when an area may start.

## Entry from the rendering foundation

The next planned slice is M7.1.
[Interface gate B](rendering-foundations.md#m6--temporal-and-display-foundation) explicitly
approves entry in its [2026-09-13 review](../milestones/interface-gate-b.md), after the structural
[R1](codebase-refactoring.md#r1--module-boundaries-and-shared-foundations) milestone.
The review verifies ADR 0010 conformance and the temporal, root-data, binding, synchronization
and capability contracts; [ADR 0021](../decisions/0021-gpu-scene-handoff-contract.md) records the
minimal scene-identity/update semantics the first consumers must implement. M6.5 closure and EDR
DEFER did not by themselves pass the review. M7.1 still requires its own implementation plan.

The present `SceneView` borrows a frame-local span of `DrawItem`s with mesh/texture references and
current/previous transforms. The renderer binds per-object frame data and encodes indexed draws;
the scene exposes three directional lights. M7.1 therefore first makes the retained CPU draw path
consume shared GPU identities/tables. M7.2 can then compare CPU and GPU visibility over the same
scene representation; M7.4 adds the point/spot light model before clustered assignment.

Carry forward the [foundation's acceptance limits](rendering-foundations.md#foundation-and-handoff).
M5.6 does not establish a production speedup or select ICB. The M6.4 manual/capture checks and
M6.5 parity investigation remain open follow-ups; entering M7 does not resolve them. Gate B owns
entry requirements; these follow-ups do not silently become additional milestone prerequisites.

## Dependency map

This table summarizes the prerequisites below; each area's own gate governs acceptance. Eligible
areas can be ordered independently, while only one implementation plan may be active.

| Area | Required foundation | Independent ordering |
|---|---|---|
| M7 scene, visibility and lighting | Gate B → M7.1; M7.2 → M7.3 | M7.4 can follow M7.1 before occlusion |
| M8 shadows and composition | M7 scene/lighting, M6 temporal; depth/HZB and surface guides for screen-space effects | Basic transparency precedes transparent fog/refraction integration |
| M9 geometry and surface paths | M7 scene/visibility/Forward+, M6 temporal | Ordinary LOD first; M8 atmosphere/fog completion is unnecessary |
| M10 query and transport reference | M7 scene/light data, M6 temporal/capture | Query/reference work need not wait for M9 or all of M8 |
| M10 real-time reflections | Query/reference gates, then M8 SSR/probe fallback | Budget tracing, denoising and composition together |
| M11 GI | M10 query/reference/denoising and shared temporal/light contracts | Accepted separately from content residency |
| M11 residency | M7 identity/safe updates and measured content pressure; M9 coarse LOD for geometry streaming | Ordinary loading/mip streaming need not wait for M10 or GI |
| Independent extensions/research | Their own fallback, oracle, budget and host gates below | Area lights follow M7.4; a bounded neural-shader study need not wait for M8–M11 |

The first scaling reference is the current raster/material/temporal frame. Later surface, ray and
cache paths must consume the same meanings for material, light, motion and exposure. Introduce a
shared guide or RHI capability with its first real consumer and keep its fallback inspectable.

## M7 — Scalable scene and direct lighting

**Outcome:** the existing scenes use shared GPU data, validated GPU visibility and bounded local lighting. M7 finishes when these four slices pass; LOD, transparency and advanced command mechanisms do not extend its completion boundary.

**Deliver:** shared GPU scene data, frustum visibility and measured submission, conservative occlusion, and clustered point/spot lighting through M7.1–M7.4.

**Sequence:** gate B → M7.1 → M7.2 → M7.3; M7.4 can follow M7.1 without waiting for occlusion. M7.2 reuses M5.6's workload/oracle contract in a production benchmark; leave frozen experiment artifacts untouched and do not require an unaccepted result or unavailable final tag. Native-host timings never establish production speedups. Validate GPU classification, CPU command count and full-frame benefit separately. Adopt a GPU-generated default only with representative production evidence; preserve a CPU/batched fallback and disclose remaining per-object encoding. ICB is optional. Each implemented backend passes the same semantic cases or declares a tested capability fallback.

**Exit gate:** all four slices pass their individual gates and the shared production evidence rules. No LOD, transparency or ICB work extends M7's completion boundary.

**Defer:** ordinary LOD and its offline data to M9; basic transparency to M8; area lights to the independent light-model extension below; streaming/general residency to M11. Also defer meshlets, two-phase occlusion optimization, cascaded/cached shadows, ray queries and dynamic GI.

## M7.1 — GPU scene foundation

**Outcome:** Existing scenes draw correctly from shared GPU identities and tables through the retained CPU path.

**Deliver:** Minimal stable instance/mesh/material/texture IDs and GPU tables, material indexing and the binding/update path current scenes need; retain CPU drawing as the first consumer.

**Exit gate:** Existing scenes preserve material and temporal output; bounded update/remap/fallback fixtures keep identities and resources valid across three frames; no general scene or residency system is required.

**Defer:** GPU visibility/submission to M7.2, occlusion to M7.3, and local lighting to M7.4; general scene/residency systems remain out of scope.

## M7.2 — GPU visibility and submission

**Outcome:** GPU frustum visibility and work generation have a validated production path and a measured CPU/batched reference.

**Deliver:** A maintained CPU visibility oracle and competent batched/indirect production baseline, then GPU frustum culling, compaction and work generation through supported execution.

**Exit gate:** CPU/GPU visible sets and final images match within declared boundary rules; counters reconcile candidates and emitted work; bounded capacity overflow is safe; paired production measurements report CPU work/calls, GPU preparation, full-frame cost and memory.

**Defer:** Temporal occlusion to M7.3 and LOD to M9; ICB remains optional and native-host results do not select a production default.

## M7.3 — Conservative occlusion

**Outcome:** Conservative temporal occlusion rejects hidden work without losing newly visible geometry.

**Deliver:** Current/previous HZB and conservative temporal occlusion on fixed geometry; first validate each pyramid mip, then consume it for rejection in VisibilityLab.

**Exit gate:** Reduction is conservative under reversed-Z; camera cuts, teleports, new objects and moving occluders do not lose visible geometry; rejected bounds and bypass reasons are inspectable against an unculled reference.

**Defer:** Ordinary LOD to M9, and two-phase occlusion optimization beyond M7.

## M7.4 — Clustered local lighting

**Outcome:** Opaque scenes support bounded point/spot lighting through a validated clustered Forward+ path.

**Deliver:** Point/spot units, attenuation and a small direct-loop reference first, then clustered Forward+ opaque shading with explicit list capacities and overflow policy; LightLab exercises scale.

**Exit gate:** Clustered results agree with the direct-loop reference; overlapping/moving lights remain bounded with visible overflow; light assignment and full lighting cost are inspectable; no local shadows or area-light model is required.

**Defer:** Local shadows and basic transparency to M8; area lights to the independent light-model extension.

## M8 — Shadows, indirect-lighting floor and environment

**Outcome:** dependable shadows, screen-space/probe lighting, atmosphere and transparent composition extend the shared frame with visible quality gains and bounded costs.

**Deliver:**

- Validate stable sun cascades/per-cascade culling, PCF and corrected PCSS, then a budgeted local-light shadow atlas/cache.
- Add GTAO and SSR with local reflection-probe/sky fallback, shared surface guides and confidence/history views.
- Establish basic sorted premultiplied Forward+ transparency with shared lighting, followed by refraction/reactive masks and baseline particles.
- Add environment/atmosphere LUTs and analytic height fog before temporal froxel fog and opaque/transparent transmittance integration.

**Prerequisites:** use M7 scene/lighting and M6 temporal contracts. The screen-space/probe area needs depth/HZB and its actual surface guides, not M9's meshlets or an alternative opaque path. Establish basic transparency here before any transparent fog/refraction integration; it is no longer a M7 gate.

**Exit gate:** cascades remain stable and bias is inspectable; atlas allocation, eviction and invalidation are deterministic; AO does not double-darken indirect energy; invalid SSR always has a declared fallback. Transparency shares light evaluation, fog composition agrees across surfaces, and temporal effects expose rejection/coverage without persistent trails. Alpha-test coverage agrees across supported depth, shadow, color and motion passes. Extend LightLab, TransportLab, TransparencyLab and a bounded OutdoorLab only for these checks.

**Defer:** area lights, VSM, clouds/weather, advanced OIT, opaque-path replacement, hardware ray effects, stochastic direct lighting and full specialist material systems.

## M9 — Geometry LOD and surface-path experiments

**Outcome:** measured geometry and opaque-path choices improve representative workloads while ordinary raster and shared material semantics remain a reliable reference.

**Deliver:** ordinary offline LOD plus runtime selection/transition first; then meshlet data, bounds/cones and ordinary indirect cluster raster; optional mesh-shader execution; separately compare compact/tile-local deferred and visibility-buffer material reconstruction/classification. Use required attachment/load-store/tile support only for the measured path, with materialized fallbacks. Extend MipLab and VisibilityLab for derivatives, alpha coverage, LOD and tiny geometry.

**Prerequisites:** M7 scene/visibility and Forward+ plus M6 temporal contracts. M8 screen-space consumers can pressure-test shared surface outputs; completing atmosphere/fog is not an entry gate. GTAO and SSR/probes belong to M8 and do not depend on an opaque-path experiment winning.

**Exit gate:** ordinary LOD selection and transitions meet declared error/stability limits; supported surface paths agree on material references. Measure cull/raster/resolve/shading and downstream cost, memory, overdraw and available bandwidth/tile/occupancy counters; mark unavailable metrics explicitly. Record adopt/retain/defer per platform/workload, retain the Forward+ oracle, and rerun the suite on representative Windows hardware once D3D12 exists.

**Defer:** virtualized geometry streaming, mandatory mesh shaders, general asset tooling, scene-query implementation and GI; those separate work areas do not wait for M9 to select an opaque winner.

## M10 — Hybrid scene query and reference transport

**Outcome:** optional scene queries and a deterministic transport oracle validate shared materials and lighting before a real-time ray signal is adopted.

**Deliver:** after M7 shared scene/light data and M6 temporal/capture contracts, first validate AS build/refit/compaction, inline queries, proxy classes and mismatch views; then canonical BSDF/light sampling and a progressive path-trace oracle in TransportLab. Neither waits for M9 or all of M8. Real-time reflections follow those gates and M8's SSR/probe fallback, with an inspectable native temporal/spatial denoiser and a capability-selected vendor-adapter boundary.

**Exit gate:** AS time/memory are bounded before real-time reflections begin; unsupported geometry has visible fallback; raster and reference transport agree on controlled scenes. Capture noisy signals, guides, variance/history, source/confidence and final output; budget AS, tracing, hit shading, denoising and composition together. Motion/disocclusion reject invalid history, and disabling RT preserves material/light semantics.

**Defer:** ray-only default rendering, stochastic direct-light replacement, production dynamic GI, mandatory vendor denoising and sparse residency.

## M11 — Dynamic GI and content residency

**Outcome:** independently accepted GI and content-scaling work extends the shared frame; neither area makes high-end hardware capabilities mandatory.

**Deliver:**

- **GI:** after M10 query/reference/denoising and the shared temporal/light contracts, establish a portable probe-volume floor, then evaluate one cascaded-probe, surfel or sparse-world radiance cache with explicit time, memory and update budgets. Capture source coverage, age, leaks, rejected history and complete composite cost in dynamic-light/occluder TransportLab sequences. Record adopt/retain/defer; adoption must materially improve on the floor and define transparent/froxel fallback.

- **Residency:** can start after M7 identity, safe updates and measurable content pressure, without waiting for M10 or GI. Add background I/O, ordinary loading and mip streaming before geometry streaming, which also needs M9's ordinary LOD/coarse assets. Track uploaded/queued bytes, residency, eviction, stalls and fallback use. Sparse pages require evidence that ordinary streaming is insufficient; no GI-cache implementation is a prerequisite.

**Exit gate:** each area meets its own quality/cost and failure checks. Forced content budgets degrade to resident coarse data without holes or use-after-free; dynamic GI meets its recorded decision gate. Accept areas separately; start detailed plans only when their prerequisites exist.

**Defer:** sparse pages until ordinary streaming proves insufficient; geometry streaming until M9 supplies ordinary LOD/coarse assets.

## Independent research and graduation gates

A bounded neural-shader study is eligible alongside the post-M5.6 direction once its own CPU numerical oracle, ordinary-shader fallback and reproducible host exist; successful M5.6 collection or M8–M11 completion is not a dependency. Compare a fixed tiny network against a supported Metal tensor path, using existing Slang facilities where possible; include rendered quality, conversion, whole-pass cost and memory. Add D3D12 only on a validated host; no bespoke IR or general compiler.

Neural material distillation needs a useful material quality/cost result, analytic/texture baselines and training/export accounting. Hybrid mesh/splat selection needs paired representations, error oracles and depth/transparency/temporal composition contracts. Splat conformance remains a separate pinned-specification corpus/reference tool. These are optional studies, not commitments to all ideas.

Area lights are a separate light-model extension after M7.4: select one shape, evaluation method, units and numerical/image reference before implementation; shadow support needs its own boundary. They are not implied by point/spot clustering and are not a condition of finishing M7 or M8.

D3D12 Work Graphs and Vulkan DGC need the basic submission benchmark; mesh/task studies need M9 geometry controls. Async scheduling needs measured overlap plus a matching serial fallback. VSM, virtualized geometry, stochastic lighting, ReSTIR and frame generation remain research; frame generation also requires stable real-frame timing, frame identity, latency and UI pacing. Each study freezes a target, fallback, budget, oracle and debugging surface before measurement. Compare APIs on the same device where possible; cross-device results describe systems. Preserve immutable `exp/<topic>` evidence and graduate production changes through explicit roadmap/ADR decisions.
