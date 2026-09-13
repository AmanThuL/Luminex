# Rendering Foundations

**Status**: Accepted

Part I of the [rendering roadmap](../roadmap.md): M4–M6.5 establish correct image formation,
inspectable execution, temporal reconstruction and the display boundary. These contracts support
[GPU-Driven Hybrid Rendering](gpu-driven-hybrid-rendering.md), which owns M7 onward and independent
research. Shared delivery rules and the current baseline remain in the roadmap entry.

## Foundation and handoff

| Foundation | What later rendering consumes | Boundary |
|---|---|---|
| Image formation | Linear materials, pre-exposed HDR, filtered assets and a single display transform | M4–M4.1 |
| Execution and inspection | Declared graph hazards/lifetimes, frame-data delivery, timing and a frame-correct editor | M5–M5.5 |
| Motion and reconstruction | Previous rendered-frame state, reset/exposure rules, active/output extents and capture domains | M6.1–M6.5 |

The retained RHI uses `bindFrameData` over growable frame-slot pages and `bindBuffer` for static
reuse; `setUniforms` is removed. The NoApi evidence remains at immutable tag
`m5.1-noapi-evidence`, with no experimental source on `main`. Temporal targets allocate at output
extent; active render-scale changes reuse that capacity. At scale 1.0 the native declaration and
kernel retain the M6.2 path, and disabling temporal inputs retains the M5.5 frame declaration.
Historical screenshot parity claims must be read with the M6.4–M6.5 evidence limits below.

These sections preserve accepted delivery boundaries; their milestone records own detailed
shipped evidence. In particular, M5.6 closed with no adopted submission path or performance
conclusion. M6.4 retains manual/capture follow-ups, and M6.5 closed with EDR deferred and a narrow
historical-hash exception. Their closure does not record a pass for every original gate.
[Interface gate B](#m6--temporal-and-display-foundation) still requires explicit approval before M7;
the [R1](codebase-refactoring.md) structural milestone runs before that review.

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

**State:** the user approved terminal closure on 2026-09-06 ([ADR 0012](../decisions/0012-gpu-submission-defer.md), [milestone](../milestones/m5.6.md)). Repeated no-validation timeouts/GPU resets remain unresolved. The prescribed 1,920-pair matrix was not completed; partial results are not accepted evidence. Source and historical plan/spec are frozen at `m5.6-gpu-submission-evidence`, not promoted to production.

**Retained evidence:** four native modes, seeded workload/CPU oracle and separate public-RHI
reference; pre-collection correctness tests; incomplete paired corpus and all failures; bounded
diagnostics with no scored timings; explicit unavailable ICB, GPU-span/stage and capture gates.
The [custody record](../research/2026-09-06-gpu-submission-closure.md) names local bundles and checksums.

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

The owner-approved visual extension adds optional San Miguel, binary masked foliage and a fixed
camera rail, plus synchronized captures and optional offline FLIP differences against Native TAA.
This completes the deferred real-scene comparison; blended transparency remains in M8.

**Exit gate:** Native and MetalFX replay matching input sequences with separate valid histories; switching is correct; native fallback and labeled resources are validation-clean; captures identify algorithms and frame inputs.

**Acceptance:** Integrated by owner authorization on 2026-09-12 with automated and real-scene
comparison evidence. Manual Sponza switching and Xcode inspection of opaque work remain follow-ups;
the [milestone record](../milestones/m6.4.md) preserves the unproven screenshot drift and capture limits.

**Defer:** Display/EDR evaluation to M6.5; other vendor adapters and frame generation to independent research.

## M6.5 — Display boundary and EDR evaluation

**Outcome:** SDR, UI and capture domains are explicit, with a bounded decision on macOS EDR/HDR.

**Deliver:** Defined SDR transform, UI composition domain, capture encoding/metadata and a bounded macOS EDR/HDR evaluation with reference-white/headroom policy.

**Exit gate:** Fixed SDR gradients and reference images pass; UI has the intended brightness and resolution; record EDR adopt/defer with evidence and limits, without changing scene/exposure/temporal semantics or requiring another display.

**Acceptance:** Closed by owner authorization on 2026-09-13 with SDR goldens, capture metadata,
UI evidence and EDR DEFER (ADR 0019). The strict fifteen historical screenshot hashes did not all
reproduce. The accepted exception covers demonstrated pre-existing screenshot nondeterminism;
reference hashes, tolerances, strict checker behavior and failed evidence remain unchanged.
This exception closes M6.5 without claiming a hash-gate pass or a drift repair.

**Follow-up:** isolate and correct the scene diffuse/UV-derivative drift; independently resolve
shader-validation bloom resource/view usage reports. Preserve the
[milestone evidence](../milestones/m6.5.md#open-parity-investigation), establish a reproduction and
validate any correction before making performance or repeatability claims. Neither a shared root
cause nor an authorized reference-image change is implied. Interface gate B remains required.

**Defer:** An adopted EDR path when evidence is insufficient; platform output never changes upstream scene or temporal semantics.

