# Rendering Roadmap

**Status**: Accepted

This roadmap is the sole owner of current milestone identifiers, boundaries, dependency order,
outcomes, exit gates, and explicit deferrals. Frozen research preserves the evidence and proposals
that informed these decisions; if its milestone wording differs, this roadmap wins. Starting a
milestone requires a separate `In progress` plan under `docs/plans/` that decomposes the boundary
without expanding it.

## Current baseline

M5 preserves M4.1's graph-declared, scene-linear HDR renderer and adds a compute/copy/barrier
execution substrate — compute pipelines, storage resources, subresource views, general copies,
indirect execution — beneath a culled, conservatively pooled validating render graph with a
deterministic dump, a read-only editor inspector, and a pausable rolling timing summary;
histogram exposure and bloom exercise it while preserving deterministic manual exposure
(`docs/milestones/m5.md`). M5.1's measured experiment (ADR 0010, `docs/milestones/m5.1.md`)
retained the object-shaped RHI and selected an address-first per-frame data path as a bounded
reshape for a future, separately accepted M5.x migration before M6; production has not migrated yet
(prototype: `Experiments/NoApi/`).

## M4 — Correct image formation

**Outcome:** the current frame runs through a small validating render graph and produces a scene-linear
HDR image with physically based glTF materials.

**Deliver:**

- Introduce logical texture and buffer handles, imported and exported resources, declared pass uses,
  read-before-write validation, a serial topological schedule, capture-readable labels, and GPU
  timestamps. Migrate the shadow, scene, UI, and new output passes without changing ownership beyond
  what the frame needs.
- Add deterministic filtered mip and IBL assets, inverse-transpose normal and tangent-frame handling,
  full glTF metallic-roughness inputs, GGX direct lighting, diffuse and specular IBL, and reversed-Z
  depth.
- Render into FP16 scene color with pre-exposure, manual exposure, a neutral tone map, and a
  replaceable SDR display transform. Add `MaterialLab` for deterministic material, light, mip, depth,
  and color checks.

**Exit gate:** Damaged Helmet and Sponza use their authored material inputs; mip, furnace,
dielectric/conductor, depth-reconstruction, gradient, and known-color tests pass; scene shaders do
not manually encode sRGB; undeclared graph use fails validation; unchanged passes match the M3
reference; every pass reports a visible GPU timestamp.

**Defer:** general compute and storage execution, transient pooling, graph optimization, automatic
exposure, bloom, temporal reconstruction, local-light scaling, advanced material lobes, and ray
tracing.

## M4.1 — RHI foundation and reference lookdev

**Outcome:** the shipped M4 renderer gains a clearer RHI component boundary and a readable,
license-clean material reference scene without changing its rendering model.

**Deliver:**

- Move the RHI to the repository-root `RHI/` component, with public headers under
  `RHI/Include/RHI/`, implementation in `RHI/Source/`, and the Metal 4 backend in
  `RHI/Backends/Metal4/Source/`. Keep Metal types out of public headers and preserve the existing
  caller-facing contracts.
- Give the component its own build description, keep backend implementation dependencies private,
  and split the Dear ImGui adapter into the optional `RHIMetal4ImGui` target.
- Keep `std::expected` as the common mechanism while each domain owns its error vocabulary; expose
  the existing RHI `Error` and `Result<T>` through a focused, self-contained public header.
- Make project headers self-contained, remove unnecessary or accidental transitive includes, and
  document every project C++ file and public API under the checked comment convention.
- Replace MaterialLab's flat blue environment with a pinned CC0 neutral studio HDRI that drives the
  visible sky and IBL together, while retaining an explicit deterministic asset-free fallback.
- Reframe MaterialLab's complete roughness/metallic grid as the default view and arrange the
  remaining diagnostics in horizontal lanes reachable without rotating the camera.

**Exit gate:** M4 unit, GPU, and scene-smoke coverage remains green; core public RHI headers compile
in isolation and expose no Metal or ImGui dependency; the optional adapter owns its ImGui-dependent
include path; the core RHI builds without that adapter; MaterialLab opens with every material sphere
prominent under a neutral studio environment and reaches its other visible diagnostics through
horizontal translation; formatting, policy, include, and documentation checks pass.

**Defer:** new compute, storage, copy, barrier, view, graph, or rendering capabilities; a production
second backend; publishing RHI as a standalone repository; a shared cross-domain error type; and
the M5.1 API-model experiment.

## M5 — Execution substrate and observability

**Outcome:** the RHI and render graph can express, validate, inspect, and safely reuse the compute and resource workloads required by later temporal and GPU-driven features.

**Deliver:**

- Add compute pipelines and dispatch, storage buffers and textures, general copies and barriers,
  subresource uses, and the required view and synchronization contracts.
- Add dead-pass culling and conservative transient pooling, with deterministic graph dumps and a
  read-only editor Render Graph inspector for pass/resource uses, schedule and culling, lifetimes,
  transitions, logical-to-physical reuse, exact per-frame timing, and transient memory; add a
  stable rolling timing summary for ongoing performance observation.
- Exercise the substrate with histogram exposure and a bloom chain while preserving a deterministic
  manual-exposure path.

**Exit gate:** compute-to-sample and per-mip hazards pass conformance tests; resize and feature
toggles neither leak nor reuse live resources; pooling on and off produces the same output; arbitrary
intermediate mips and layers can be captured; the same compiled frame is inspectable through a
deterministic dump and the editor; pass time, transient high-water marks, and alias savings are
visible.

**Portability checkpoint A:** freeze semantic tests for upload and layout, resource views, sRGB,
reversed-Z, storage hazards, load/store behavior, indirect arguments, and frame-slot retirement.

**Defer:** motion/history semantics, dynamic resolution, GPU scene ownership, bindless materials,
indirect visibility, and alternative opaque surface paths.

## M5.1 — RHI execution-model decision

**Outcome:** a measured prototype decides whether to retain, partially reshape, or replace the object-shaped RHI with a GPU-address-first interface honest to Metal 4 and D3D12.

**Deliver:**

- Before measuring results, freeze the baseline, hypotheses, representative graph, bounded workloads,
  rubric, and adoption thresholds; the design spec owns concrete scales and prototype architecture.
- Compare the maintained RHI with the data-oriented "No Graphics API" model described by Sebastian
  Aaltonen's [article](https://www.sebastianaaltonen.com/blog/no-graphics-api),
  [extended presentation](https://www.youtube.com/watch?v=aQv9pUl9PBM), and
  [SIGGRAPH course slides](https://community.arm.com/cfs-file/__key/communityserver-blogs-components-weblogfiles/00-00-00-20-66/6763.2026_2D00_mmg_2D00_seb_2D00_gfx_2D00_api.pdf).
  Cover memory/address and root data, handles and bindings, pipelines and commands, attachments and
  synchronization, residency and capabilities, debugging and failure behavior; retain the render
  graph's logical ownership.
- Translate one representative raster/compute/copy graph through an isolated non-default Metal 4
  prototype, plus finite binding, hazard, upload/resize-lifetime, and indirect stress cases.
- Classify its Metal 4/D3D12 mappings as native, emulated, unavailable, or unknown, including shader
  constraints. Vulkan is unscored comparative evidence, not a backend commitment.
- Compare correctness, failure and validation behavior, capture quality, API surface, CPU encoding,
  binding traffic, pipeline/cache behavior, barriers, and allocation against the maintained RHI.

**Exit gate:** checkpoint A stays green; the prototype reproduces the output, failure behavior, and
three-frame lifetime rules it claims to cover; all emulation, fallback, shader constraints, gaps,
and risks are recorded; and evidence is judged against the frozen thresholds. An ADR selects one
production direction and disposes of the experiment. M5.1 does not migrate production: any adopted
change requires a separately accepted M5.x migration completed before M6, with no parallel API left.

**Defer:** a production D3D12 backend, Vulkan/Linux support, multi-queue optimization, ray tracing,
production interface migration, and later rendering features.

## M5.2 — Editor workspace and selection

**Outcome:** the editor separates scene authoring, property editing, performance, and compiled-frame debugging, with selection determining the properties the Inspector edits.

**Deliver:**

- Add a main menu bar for program-level actions, panel visibility, and default-layout reset. The
  Render Graph window can be opened and closed from the menu while remaining dockable or floating.
- Replace the overloaded right column with a left Scene/outliner sidebar, the central Viewport, a
  right context Inspector, and a dockable Performance panel; left-side object, light, or camera
  selection determines the properties edited on the right.
- Keep selection identity local to the editor and the active scene. This milestone does not pull
  M7's stable GPU-scene instance or resource identities into the current renderer.

**Exit gate:** default, restored, and reset layouts keep the Viewport usable; every panel toggle
works; selection edits the intended scene state and resets safely across scene changes; existing
Render Graph list and timing views remain dockable, frame-correct, and behaviorally unchanged.

**Defer:** graph visualization or mutation, persistent scene identity/serialization, undo/redo, an
entity-component system, multiple platform windows, and graph execution, scheduling, or RHI changes.

## M5.3 — Render graph node visualization

**Outcome:** M5's retained compiled frame gains a stable read-only node view without changing graph execution or replacing its exact list and text representations.

**Deliver:**

- Add a node canvas over `CompiledFrameRecord`: passes are nodes, version dependencies are edges,
  sinks are endpoints, and culled passes remain separate from the scheduled DAG. Selection exposes
  subresources, barriers, timing, lifetimes, and reuse; alias links differ from execution edges.
- Keep automatic layout deterministic and stable for an unchanged graph. Preserve the existing
  detailed list and deterministic text dump as alternate views of the same record.

**Exit gate:** unchanged frames produce stable positions; dependencies agree with resource versions
and schedule; culled passes and aliases cannot resemble scheduled edges; details agree with the
selected pass, version, and transient assignment; list, node, and dump identify the same frame.

**Defer:** graph mutation, user passes, capture-file browsing, manual scheduling, and changes to
Render Graph execution or RHI semantics.

## M6 — Temporal and display foundation

**Outcome:** every frame owns explicit current and previous state, a portable reconstruction path,
and a defined display boundary.

**Deliver:**

- Add previous camera and object transforms, jittered and unjittered matrices, motion vectors,
  history resources and reset reasons, disocclusion and reactive masks, and render/output resolution
  separation.
- Add native TAA and TAAU, a dynamic-resolution controller, exposure adaptation, and a MetalFX
  temporal adapter that consumes the same engine-owned inputs and reset policy.
- Evaluate macOS EDR/HDR presentation without allowing the platform path to redefine scene, exposure,
  temporal, or UI composition semantics.

**Exit gate:** scripted camera cuts, resize, scene and render-scale changes, animation, and algorithm
switching invalidate history correctly; rigid and camera motion reproject correctly; raw, native TAA,
and MetalFX outputs can be compared from one capture; exposure changes do not pulse histories; UI is
sharp and composed in its intended domain.

**Interface gate B:** before M7, verify production conforms to M5.1's ADR and any pre-M6 migration is complete, then freeze the GPU-scene, root-data, binding, synchronization, and capability semantics
M7 consumes. A second backend is not required; when scheduled, D3D12 must pass checkpoint A and render the M6 PBR/HDR/TAA frame without redefining shared scene, graph, or temporal semantics.

**Defer:** GPU-driven submission, clustered local lighting, scalable shadow systems, atmosphere, and
opaque-path experiments.

## M7 — Scalable scene and direct lighting

**Outcome:** stable GPU scene data drives measured visibility, indirect submission, and bounded local
lighting while retaining CPU and Forward+ reference paths.

**Deliver:** stable instance, material, mesh, and texture identities; GPU scene tables; bindless
materials; point, spot, and area-light records; clustered light lists with an overflow policy;
Forward+ opaque PBR and basic sorted premultiplied transparency; current and previous HZB;
frustum/LOD/occlusion culling; indirect work generation; and a CPU visibility oracle.

**Exit gate:** CPU render submission grows mainly with passes and bins rather than object count; GPU
visibility matches the CPU oracle or reports every difference; new and moving objects do not remain
incorrectly occluded; synthetic local-light stress remains bounded with visible overflow; stable IDs
survive remapping and residency fallback across three frames in flight; every implemented backend
passes the same semantic tests or uses a documented capability fallback.

**Defer:** cascaded and cached shadows, volumetrics, meshlets, alternative opaque surface paths, ray
queries, and dynamic GI.

## M8 — Scalable shadows, atmosphere, and transparency

**Outcome:** dependable shadow, atmosphere, fog, and transparent-surface systems share the M7 scene,
lighting, visibility, and temporal contracts.

**Deliver:** stable cascaded sun shadows; per-cascade culling; a local-light shadow atlas and cache
with update budgets; a corrected PCSS option; environment and LUT atmosphere; analytic and froxel
fog; transparent fog integration; refraction and reactive masks; and baseline particles on the
Forward+ transparent path.

**Exit gate:** cascades remain stable during camera motion and expose their bias components; atlas
allocation, eviction, and invalidation are deterministic; opaque and transparent atmosphere agrees;
froxel quality can scale without catastrophic trails; alpha-test rules match depth, shadow, color,
and motion passes.

**Defer:** virtual shadow maps, mesh-shader dependence, opaque-path replacement, hardware ray
queries, and stochastic many-light sampling.

## M9 — Modern geometry and surface experiments

**Outcome:** representative measurements, rather than architectural preference, select the default
opaque geometry and surface path per platform while ordinary indirect raster remains correct.

**Deliver:** offline LOD and meshlet data; cluster bounds and cones; instance-to-meshlet culling;
optional mesh-shader execution; compact or tile-local deferred and visibility-buffer prototypes;
derivative reconstruction and material classification; GTAO; and an SSR/probe reflection hierarchy.

**Exit gate:** Forward+, compact deferred or tile-local, and visibility-buffer paths reproduce the
material reference within stated tolerances; captures report bandwidth, tile spills, overdraw,
occupancy, transient memory, and time on representative target hardware; once D3D12 exists, the same
suite also runs on representative Windows hardware. Alpha-tested and derivative stress scenes define
limitations; a recorded decision selects the per-platform default without deleting the Forward+
oracle.

**Defer:** acceleration structures, path tracing, real-time ray-traced signals, dynamic GI caches,
and virtualized geometry streaming.

## M10 — Hybrid scene query and reference transport

**Outcome:** hardware ray queries are an optional scene service with visible fallbacks, and a
deterministic reference transport path validates shared material and lighting semantics.

**Deliver:** first add acceleration-structure resources and scheduling, inline ray queries, proxy
classifications and mismatch views, canonical light and BSDF sampling, and a progressive path-trace
oracle. After those costs and semantics are bounded, add ray-traced reflections behind the SSR/probe
fallback hierarchy plus native and vendor-adapter denoising boundaries.

**Exit gate:** acceleration-structure build, refit, compaction, and memory costs are bounded before
the real-time signal begins; unsupported geometry has a visible fallback; raster and path-traced
controlled scenes agree; reflection source and confidence are inspectable; invalid ray history does
not survive motion or disocclusion; disabling ray tracing does not change material or light semantics.

**Defer:** a ray-only default renderer, stochastic direct-light replacement, production dynamic GI,
neural rendering, and mandatory sparse residency.

## M11 — Dynamic GI and advanced residency

**Outcome:** one measured dynamic indirect-lighting cache and bounded content streaming extend the
shared scene-query and temporal architecture without making high-end capabilities mandatory.

**Deliver:** a portable probe-volume floor; one measured cascaded-probe, surfel, or sparse-world
radiance prototype using screen traces and optional scene queries; background I/O; mip and geometry
streaming; and explicit residency budgets. Sparse pages require evidence that ordinary streaming is
insufficient.

**Exit gate:** GI reports source coverage, age, updates, leaks, rays, memory, and full composite cost;
transparent and froxel lighting has a defined fallback; the prototype materially beats the portable
floor on dynamic-light and occluder tests; forced residency budgets degrade to coarse resident data
without holes or use-after-free.

## Independent research after M11

A Vulkan backend, virtual shadow maps, virtualized geometry streaming, stochastic direct lighting,
ReSTIR, frame generation, and neural methods remain independent research programs. None becomes a
baseline dependency until a representative target, scene, fallback, performance and memory budget,
validation oracle, and debugging surface justify graduation into a future roadmap revision.
