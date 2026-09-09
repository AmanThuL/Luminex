# Rendering Roadmap

**Status**: Accepted

This roadmap owns current milestone identifiers, boundaries, dependencies, outcomes, gates and deferrals. Frozen research preserves the evidence behind these decisions; this roadmap governs current scope. Starting a milestone requires an `In progress` plan under `docs/plans/` that decomposes its boundary without expanding it.

## Current baseline

The shipped baseline is [M6.3](milestones/m6.3.md) over [M6.2](milestones/m6.2.md) over [M6.1](milestones/m6.1.md): a scene pass that rasterises into a render rectangle smaller than the output extent, temporal upscaling that reconstructs the output-extent image from jittered lower-resolution samples, and a pure GPU-time-driven controller that proposes the render scale, with every temporal target still allocating at the output extent so a scale change reallocates nothing (ADR 0016), layered over M6.2's native-resolution TAA that reprojects, rejects, clips and accumulates over engine-owned rigid-object motion, a per-pixel reactive weight, and exposure that adapts at a bounded rate and corrects history for the brightness it was recorded at (ADR 0014), over M6.1's opt-in engine-owned rigid-object motion with Renderer-owned camera history, a declared motion-vector convention, and one Renderer-owned, reset-aware history texture (ADR 0013), over M5.5's scene-linear PBR/HDR, M5's validated compute/copy/barrier graph, exposure, bloom, transient pooling and timing, M5.2's ADR 0010 frame-data path, and the M5.3–M5.5 selection workspace and detached graph view. At render scale 1.0 the frame declaration and native kernel are unchanged from M6.2; with temporal inputs off, the frame declaration and fixed-camera output are unchanged from M5.5. The retained object RHI uses `bindFrameData` over growable frame-slot pages and `bindBuffer` for static reuse; `setUniforms` is removed. NoApi evidence remains at immutable tag `m5.1-noapi-evidence`, with no experimental source on `main`. The completed boundaries below and their milestone records retain the detailed behavior, evidence and limits.

## Project direction and delivery

Luminex is a Metal 4-first modern rendering playground and portfolio: visible image quality and verifiable graphics engineering are both outcomes. The [foundation goals](specs/2026-08-07-luminex-upgrade-design.md) and [research synthesis](research/2026-08-09-rendering-pipeline-synthesis.md) motivate a graph-scheduled, GPU-driven hybrid renderer whose raster, screen-space, ray, reconstruction and cache paths share scene, material, light and temporal semantics. Grow the thin RHI through actual consumers. ADR 0007 governs production backends: D3D12 second when a validated host exists; Vulkan is research only. A benchmark adapter is not a production backend, and additional hardware is not a renderer gate.

The next visible outcome is temporal upscaling and dynamic resolution, followed by a replaceable MetalFX temporal adapter and a bounded display-path evaluation. GPU visibility and bounded local lighting follow the temporal slices. M5.6 closed as reliability failure / DEFER with no accepted performance conclusion (ADR 0012); it does not block M6 or preselect ICB. The [project-fit assessment](research/2026-09-06-graphics-paradigm-project-fit.md) also opens bounded neural-shader research without requiring completion of M8–M11.

Each milestone has one recognizable completion outcome. Use a few independently accepted slices; implementation steps belong in a just-in-time plan or PR, not an expanding series of milestone IDs. M6's five slices and M7's four are fixed below; M8–M11 retain bounded work areas until planned. Identifiers organize work; the stated prerequisites, rather than numerical order, determine entry. Only one implementation plan is active at a time; independent entry does not start another plan.

Every rendering slice includes a diagnostic fixture, relevant intermediate views, deterministic seeds/camera tracks and declared temporal warmup, plus raw/reference and final captures. Retain Sponza and Helmet integration checks. Freeze a device, resolution, content and CPU/GPU/memory budget before measuring; 60 real frames/s is a planning target, not an unmeasured performance claim. Report full update/render/reconstruction/composite cost, timing variation and unavailable counters. Track pipeline variants, cache misses and compilation stalls when a slice introduces them. Grow depth, normal, roughness, motion and identity outputs only for real consumers with shared meaning; add no speculative G-buffer. Each feature states its fallback, overflow and reset behavior.

## M4 — Correct image formation

**Outcome:** the current frame runs through a small validating render graph and produces a scene-linear HDR image with physically based glTF materials.

**Deliver:**

- Introduce logical texture and buffer handles, imported and exported resources, declared pass uses, read-before-write validation, a serial topological schedule, capture-readable labels, and GPU timestamps. Migrate the shadow, scene, UI, and new output passes without changing ownership beyond what the frame needs.
- Add deterministic filtered mip and IBL assets, inverse-transpose normal and tangent-frame handling, full glTF metallic-roughness inputs, GGX direct lighting, diffuse and specular IBL, and reversed-Z depth.
- Render into FP16 scene color with pre-exposure, manual exposure, a neutral tone map, and a replaceable SDR display transform. Add `MaterialLab` for deterministic material, light, mip, depth, and color checks.

**Exit gate:** Damaged Helmet and Sponza use their authored material inputs; mip, furnace, dielectric/conductor, depth-reconstruction, gradient, and known-color tests pass; scene shaders do not manually encode sRGB; undeclared graph use fails validation; unchanged passes match the M3 reference; every pass reports a visible GPU timestamp.

**Defer:** compute and storage execution, transient pooling, graph optimization, automatic exposure, bloom, temporal reconstruction, local-light scaling, advanced material lobes, and ray tracing.

## M4.1 — RHI foundation and reference lookdev

**Outcome:** the shipped M4 renderer gains a clearer RHI component boundary and a readable, license-clean material reference scene without changing its rendering model.

**Deliver:**

- Move the RHI to the repository-root `RHI/` component, with public headers under `RHI/Include/RHI/`, implementation in `RHI/Source/`, and the Metal 4 backend in `RHI/Backends/Metal4/Source/`. Keep Metal types out of public headers and preserve the existing caller-facing contracts.
- Give the component its own build description, keep backend implementation dependencies private, and split the Dear ImGui adapter into the optional `RHIMetal4ImGui` target.
- Keep `std::expected` as the common mechanism while each domain owns its error vocabulary; expose the existing RHI `Error` and `Result<T>` through a focused, self-contained public header.
- Make project headers self-contained, remove unnecessary or accidental transitive includes, and document every project C++ file and public API under the checked comment convention.
- Replace MaterialLab's flat blue environment with a pinned CC0 neutral studio HDRI that drives the visible sky and IBL together, while retaining an explicit deterministic asset-free fallback.
- Reframe MaterialLab's complete roughness/metallic grid as the default view and arrange the remaining diagnostics in horizontal lanes reachable without rotating the camera.

**Exit gate:** M4 unit, GPU, and scene-smoke coverage remains green; core public RHI headers compile in isolation and expose no Metal or ImGui dependency; the optional adapter owns its ImGui-dependent include path; the core RHI builds without that adapter; MaterialLab opens with every material sphere prominent under a neutral studio environment and reaches its other visible diagnostics through horizontal translation; formatting, policy, include, and documentation checks pass.

**Defer:** new compute, storage, copy, barrier, view, graph, or rendering capabilities; a production second backend; publishing RHI as a standalone repository; a shared cross-domain error type; and the M5.1 API-model experiment.

## M5 — Execution substrate and observability

**Outcome:** the RHI and render graph can express, validate, inspect, and safely reuse the compute and resource workloads required by later temporal and GPU-driven features.

**Deliver:**

- Add compute pipelines and dispatch, storage buffers and textures, general copies and barriers, subresource uses, and the required view and synchronization contracts.
- Add dead-pass culling and conservative transient pooling, with deterministic graph dumps and a read-only editor Render Graph inspector for pass/resource uses, schedule and culling, lifetimes, transitions, logical-to-physical reuse, exact per-frame timing, and transient memory; add a stable rolling timing summary for ongoing performance observation.
- Exercise the substrate with histogram exposure and a bloom chain while preserving a deterministic manual-exposure path.

**Exit gate:** compute-to-sample and per-mip hazards pass conformance tests; resize and feature toggles neither leak nor reuse live resources; pooling on and off produces the same output; arbitrary intermediate mips and layers can be captured; the same compiled frame is inspectable through a deterministic dump and the editor; pass time, transient high-water marks, and alias savings are visible.

**Portability checkpoint A:** freeze semantic tests for upload and layout, resource views, sRGB, reversed-Z, storage hazards, load/store behavior, indirect arguments, and frame-slot retirement.

**Defer:** motion/history semantics, dynamic resolution, GPU scene ownership, bindless materials, indirect visibility, and alternative opaque surface paths.

## M5.1 — RHI execution-model decision

**Outcome:** a measured prototype decides whether to retain, partially reshape, or replace the object-shaped RHI with a GPU-address-first interface honest to Metal 4 and D3D12.

**Deliver:**

- Before measuring, freeze the baseline, hypotheses, representative graph, bounded workloads, rubric, and adoption thresholds; the design spec owns concrete scales and prototype architecture.
- Compare the maintained RHI with Sebastian Aaltonen's data-oriented "No Graphics API" model from the [article](https://www.sebastianaaltonen.com/blog/no-graphics-api), [extended presentation](https://www.youtube.com/watch?v=aQv9pUl9PBM), and [SIGGRAPH slides](https://community.arm.com/cfs-file/__key/communityserver-blogs-components-weblogfiles/00-00-00-20-66/6763.2026_2D00_mmg_2D00_seb_2D00_gfx_2D00_api.pdf), covering memory/root data, bindings, pipelines/commands, synchronization, residency/capabilities, debugging, and failure behavior while the render graph retains logical ownership.
- Translate one representative raster/compute/copy graph through an isolated non-default Metal 4 prototype, plus finite binding, hazard, upload/resize-lifetime, and indirect stress cases.
- Classify Metal 4/D3D12 mappings and shader constraints as native, emulated, unavailable, or unknown; Vulkan remains unscored comparative evidence rather than a backend commitment.
- Compare correctness, failure behavior, capture quality, API surface, CPU encoding, binding traffic, pipelines/cache, barriers, and allocation against the maintained RHI.

**Exit gate:** checkpoint A stays green; the prototype reproduces the output, failure behavior, and three-frame lifetime rules it claims to cover; all emulation, fallback, shader constraints, gaps, and risks are recorded; and evidence is judged against the frozen thresholds. An ADR selects one production direction and disposes of the experiment. M5.1 does not migrate production: the adopted change is implemented by M5.2 before M6, with no parallel API left.

**Defer:** a production D3D12 backend, Vulkan/Linux support, multi-queue optimization, ray tracing, production interface migration, and later rendering features.

## M5.2 — RHI frame-data path and public surface

**Outcome:** the production RHI absorbs M5.1's proven per-frame data-delivery win through one typed, GPU-address-visible path, while its retained object model becomes easier to navigate.

**Deliver:** replace `setUniforms` with typed `bindFrameData`, which allocates, copies, binds, and returns one frame-owned GPU address; use retirement-safe growable pages per frame slot while preserving `bindBuffer` for static reuse; split the public header into self-contained concepts behind an `RHI.h` umbrella; migrate with no compatibility alias; and verify correctness, allocation, binding, and paired CPU performance without modifying the frozen experiment.

**Exit gate:** checkpoint A passes unchanged; every public header compiles alone and leaks no native types; all production callers use the new path; growth creates no backing allocations after the three slots reach high water; static bindings write no frame-data bytes; overflow workloads materially improve while fitting and static workloads regress by no more than 5%; captures identify uploaded ranges and three-frame reuse is validation-clean.

**Defer:** bindless-resource migration, shader-pointer/root-signature or retained-model redesign, general buffer mapping, per-mip raster attachments, generation handles, resident-size queries, a production D3D12 backend, and later rendering features.

## M5.3 — Editor workspace and selection

**Outcome:** the editor becomes a repeatable selection-driven workspace that separates scene navigation, property editing, performance observation, and exact compiled-frame debugging without changing renderer execution.

**Deliver:**

- Add a main menu for quit, panel visibility, default-layout reset, and next-frame GPU capture; use a versioned workspace so clean, legacy M5.2, restored M5.3, and reset layouts have deterministic behavior.
- Replace the overloaded right column with a searchable left Scene panel, central Viewport, right context Inspector, and dockable Performance panel; keep Render Graph independently dockable and closable.
- Model single selection as editor-local active-scene identity over camera, rendering settings, directional lights, and objects; resolve indices safely without introducing persistent or GPU-scene IDs.
- Give the Viewport compact rendering controls and move full properties into the selected Inspector; preserve rolling Performance and exact newest-retired-frame Render Graph as distinct time domains.

**Exit gate:** clean, legacy, restored, and reset layouts keep a usable Viewport; every panel and menu action works; selection, filtering, scene changes, resize, and hidden-Viewport input are safe; Performance pause is coherent; Render Graph remains frame-correct; fixed-camera renderer output is unchanged.

**Defer:** viewport picking, outlines, gizmos, multi-selection, hierarchy/ECS work, persistent identity, rename/serialization, undo/redo, asset browsing, graph visualization or mutation, multiple platform windows, new profiling instrumentation, and graph, renderer, scheduling, or RHI changes.

## M5.4 — Render graph node visualization

**Outcome:** M5's retained compiled frame gains a stable read-only node view without changing graph execution or replacing its exact list and text representations.

**Deliver:**

- Add a node canvas over `CompiledFrameRecord`: passes are nodes, version dependencies are edges, sinks are endpoints, and culled passes remain separate from the scheduled DAG. Selection exposes subresources, barriers, timing, lifetimes, and reuse; alias links differ from execution edges.
- Keep automatic layout deterministic and stable for an unchanged graph; drags are session state. The panel is canvas-first: the M5.3 list becomes a selected-node details pane; the dump remains.

**Exit gate:** unchanged frames produce stable positions; dependencies agree with resource versions and schedule; culled passes and aliases cannot resemble scheduled edges; details agree with the selected pass, version, and transient assignment; node view and dump identify the same frame.

**Defer:** graph mutation, user passes, capture-file browsing, manual scheduling, and changes to Render Graph execution or RHI semantics.

## M5.5 — Render graph legibility and detached window

**Outcome:** the node view becomes readable for real frames: it opens in its own native OS window, and its cards, links, and layout read the way a Falcor-style graph editor does, without changing what M5.4 draws from.

**Deliver:**

- Enable Dear ImGui platform viewports. The Render Graph window always owns its own OS window, with native title bar and close button, through a dedicated window class and never docks; the other panels keep the main window. Vendored-backend defects this exposes (event pacing, missing autorelease pools) are fixed in the maintained patch, and RHI wrappers release Metal objects inside their own pools.
- Add a pure layout step over `GraphNodeModel`: stage groups by dotted label prefix, collapsed by default and expandable in place; layers and ranks left to right, with optional row wrapping. The panel places cards from measured sizes: title-bar cards, pins on the card edges, pin-to-pin curved links coloured per resource, short pin labels with full labels on hover and selection.

**Exit gate:** the detached window is validation-clean across open, resize, move, close, and GPU capture, and its footprint does not grow with frames; a collapsed group carries exactly the edges that cross its boundary and its members' summed timing; layers, ranks, rows, and columns are deterministic for a given record, expansion set, and column count, and cards never overlap; the M5.4 exit gate still holds; fixed-camera renderer output is unchanged.

**Defer:** other panels as OS windows as a supported workflow, persisted node positions, manual grouping, an orientation toggle, graph mutation, and changes to Render Graph execution or RHI semantics.

## M5.6 — GPU work-submission experiment

**Outcome:** closed as reliability failure / **DEFER**, with no accepted performance conclusion.
Production retains M5.5 rendering behavior; no GPU-submission implementation is adopted.

**State:** the user approved terminal closure on 2026-09-06 ([ADR 0012](decisions/0012-gpu-submission-defer.md), [milestone](milestones/m5.6.md)). Repeated no-validation timeouts/GPU resets remain unresolved. The prescribed 1,920-pair matrix was not completed; partial results are not accepted evidence. Source and historical plan/spec are frozen at `m5.6-gpu-submission-evidence`, not promoted to production.

**Retained evidence:** four native modes, seeded workload/CPU oracle and separate public-RHI
reference; pre-collection correctness tests; incomplete paired corpus and all failures; bounded
diagnostics with no scored timings; explicit unavailable ICB, GPU-span/stage and capture gates.
The [custody record](research/2026-09-06-gpu-submission-closure.md) names local bundles and checksums.

**Accepted closure exception:** stop rather than complete the prescribed measurement matrix;
preserve failures, accept DEFER without wins/break-even claims, freeze source and remove the active
executor from the production baseline. This exception closes the investigation, not its failed
reliability/measurement gates. Only conclusions return to main; no experiment code/build include.
Root-cause diagnosis is separate future work, not a condition keeping M5.6 active or blocking M6.

**Future evidence gate:** any adoption still requires a demonstrated correction, exact-artifact
validation, new freeze and full paired collection, honest capture/capability coverage and original
uncertainty/regression guards. A validation-on or isolated retirement pass cannot substitute.

**Defer:** production GPU-scene ownership or public RHI redesign before interface gate B, general
bindless materials, HZB/temporal occlusion, mesh/task shaders, DGC, Work Graphs, multi-API parity,
and a universal GPU score. Experiment-local instance tables do not define persistent scene IDs.

## M6 — Temporal and display foundation

**Outcome:** a stable moving PBR/HDR image with engine-owned temporal inputs, native reconstruction and an explicit display/UI boundary. Completion is the five slices below, not a general animation or post-processing system.

**Deliver:** motion/history state, native TAA and exposure stability, TAAU/dynamic resolution, a MetalFX adapter, and display/EDR evaluation through M6.1–M6.5.

**Shared rules:** previous means the previous valid rendered frame, not the last occupant of a frame slot. Shared events carry reset reasons; each history chooses clear, resample or exposure correction. Temporal inputs name color, depth and resolution domains. Camera/rigid motion is required; unsupported skinning, morphs or deformation are explicitly invalid, not silently zero-motion. Add formats, attachment support and depth access only as actual passes need them. Alpha/reactive tests grow with supported materials; full transparent composition arrives in M8.

**Interface gate B:** M6.1 → M6.2 → M6.3 → M6.4; M6.5's bounded evaluation completes the display decision. Before M7, verify ADR 0010 conformance and approve the temporal, root-data, binding, synchronization, capability and minimal scene-identity semantics its first consumers need. Do not freeze all future GPU-scene layouts or implement them here. A new production submission capability still needs its own evidence. D3D12, when scheduled, must pass checkpoint A and reproduce the PBR/HDR/TAA frame without redefining shared semantics; another backend or an EDR adoption is not a gate.

**Exit gate:** M6.1–M6.5 pass their individual gates, the shared temporal rules hold, and interface gate B approves entry to M7. EDR may close with an evidenced defer decision.

**Defer:** GPU visibility/submission, clustered lighting, LOD, full animation, transparent materials, scalable shadows, atmosphere, opaque-path experiments and frame generation.

## M6.1 — Temporal state and motion

**Outcome:** The frame exposes trustworthy camera and rigid-object motion with explicit history ownership.

**Deliver:** Current/previous camera and rigid-object transforms, jittered/unjittered matrices, declared motion units/direction, render/output extents, history ownership/reset reasons, and a minimal TemporalLab.

**Exit gate:** Camera and rigid motion reproject correctly; camera cuts, scene/projection changes and resize invalidate predictably; persistent history imports and three-frame retirement are validation-clean.

**Defer:** Temporal accumulation, upscaling and display evaluation to M6.2–M6.5; unsupported deformation remains explicitly invalid.

## M6.2 — Native TAA and exposure stability

**Outcome:** Native-resolution reconstruction stabilizes motion and exposure while preserving an inspectable raw reference.

**Deliver:** Native-resolution TAA, disocclusion/rejection and neighborhood clipping, baseline reactive weighting, exposure adaptation and history exposure correction; inspect raw, reprojected and accumulated signals.

**Exit gate:** Scripted static detail, thin geometry, motion and emissive/exposure changes meet frozen stability/ghosting tolerances; resets and declared warmup are reproducible; raw bypass remains a reference.

**Defer:** Temporal upscaling and dynamic resolution to M6.3; vendor reconstruction to M6.4; full transparency to M8.

## M6.3 — TAAU and dynamic resolution

**Outcome:** The renderer reconstructs a stable output image while internal resolution follows a bounded GPU budget.

**Deliver:** Fixed-scale native TAAU first, then a GPU-time controller with hysteresis and bounded steps; separate active extents from allocation capacity.

**Exit gate:** Spatial/raw and temporal outputs compare at the same output extent; gradual scale changes reuse/resample or reset history explicitly; scale oscillation does not cause repeated allocation or sustained ghosts; UI stays sharp.

**Defer:** MetalFX integration to M6.4, EDR evaluation to M6.5, and frame generation to independent research.

## M6.4 — MetalFX temporal adapter

**Outcome:** MetalFX is a replaceable reconstruction implementation over the same engine-owned temporal contract.

**Deliver:** A capability-selected adapter consuming the same engine-owned color, depth, motion, jitter, exposure, extent and reset semantics; vendor packing remains inside the adapter.

**Exit gate:** Native and MetalFX replay matching input sequences with separate valid histories; switching is correct; native fallback and labeled resources are validation-clean; captures identify algorithms and frame inputs.

**Defer:** Display/EDR evaluation to M6.5; other vendor adapters and frame generation to independent research.

## M6.5 — Display boundary and EDR evaluation

**Outcome:** SDR, UI and capture domains are explicit, with a bounded decision on macOS EDR/HDR.

**Deliver:** Defined SDR transform, UI composition domain, capture encoding/metadata and a bounded macOS EDR/HDR evaluation with reference-white/headroom policy.

**Exit gate:** Fixed SDR gradients and reference images pass; UI has the intended brightness and resolution; record EDR adopt/defer with evidence and limits, without changing scene/exposure/temporal semantics or requiring another display.

**Defer:** An adopted EDR path when evidence is insufficient; platform output never changes upstream scene or temporal semantics.

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
