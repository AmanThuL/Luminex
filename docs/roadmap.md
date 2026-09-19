# Rendering Roadmap

**Status**: Accepted

This entry and its five parts jointly own current milestone identifiers, boundaries, dependencies,
outcomes, gates and deferrals. Each boundary is defined in one part; frozen research preserves
its supporting evidence. Starting a milestone requires an `In progress` plan under `docs/plans/`
that decomposes the accepted boundary without expanding it.

## Five parts

| Part | Scope | Purpose |
|---|---|---|
| [Rendering Foundations](roadmap/rendering-foundations.md) | M4–M6.5 and interface gate B | Establish image formation, execution, inspection, temporal and display contracts; retain acceptance limits |
| [GPU-Driven Hybrid Rendering](roadmap/gpu-driven-hybrid-rendering.md) | M7–M11 and independent research | Scale scene data, visibility and lighting, then evaluate geometry, transport, GI and residency |
| [Codebase Refactoring](roadmap/codebase-module-boundaries.md) | R1 between M6.5 and gate B; [R2–R4](roadmap/codebase-restructuring.md) between M7 and UX2 | Restructure modules and large units between rendering milestones without changing output; move the RHI to its own repository |
| [Editor Experience](roadmap/editor-experience.md) | UX1 after gate B and before M7.1; UX2 after R4 and before N1 | Make scene inspection, controls and diagnostic data understandable and reliable for a human operator; make scenes saved documents |
| [Neural and Learned Rendering](roadmap/neural-rendering.md) | N1–N4 interleaved after M7 | Evaluate learned techniques as bounded experimental features with oracles, fallbacks and hardware gates |

The dividing point is the change from a trustworthy moving image and execution substrate to
shared GPU scene data and its consumers. Foundation completion supplies those contracts;
[interface gate B](roadmap/rendering-foundations.md#m6--temporal-and-display-foundation)
approved entry to M7.1 in its [separate review](milestones/interface-gate-b.md). The second part's
[dependency map](roadmap/gpu-driven-hybrid-rendering.md#dependency-map) explains independent entry.

## Execution sequence

This section owns the cross-part delivery order. Each part still owns its slices' boundaries and
gates; a row here changes priority, never a gate. Starting a row requires the preceding rows to be
accepted unless its entry column names an earlier technical prerequisite, and only one
implementation plan is active at a time. Slices inside one row deliver in numerical order unless
the row says otherwise.

| Step | Slice | Part | State | Enters after |
|---|---|---|---|---|
| 1 | [M6.5](roadmap/rendering-foundations.md#m65--display-boundary-and-edr-evaluation) display boundary | I | Closed 2026-09-13 | M6.4 |
| 2 | [R1.1–R1.5](roadmap/codebase-module-boundaries.md#r1--module-boundaries-and-shared-foundations) module boundaries | III | Complete 2026-09-13 | M6.5 |
| 3 | [Interface gate B](roadmap/rendering-foundations.md#m6--temporal-and-display-foundation) | I | PASS 2026-09-13 | R1 |
| 4 | [UX1](roadmap/editor-experience.md#ux1--editor-usability-and-diagnostics) editor usability | IV | Implemented, owner-accepted | Gate B |
| 5 | [M7.1](roadmap/gpu-driven-hybrid-rendering.md#m71--gpu-scene-foundation) GPU scene foundation | II | Implemented, owner-accepted 2026-09-15 | UX1 |
| 6 | [M7.2](roadmap/gpu-driven-hybrid-rendering.md#m72--cpu-visibility-reference-and-indirect-baseline) CPU visibility reference and indirect baseline | II | Owner-accepted for integration 2026-09-15; image gates failed | M7.1 |
| 7 | [M7.3](roadmap/gpu-driven-hybrid-rendering.md#m73--gpu-visibility-and-work-generation) GPU visibility and work generation | II | Owner-accepted for integration | M7.2 |
| 8 | [M7.4](roadmap/gpu-driven-hybrid-rendering.md#m74--conservative-occlusion) conservative occlusion | II | Implemented, owner-accepted 2026-09-18; image gate failed 13/15 | M7.3 |
| 9 | [M7.5](roadmap/gpu-driven-hybrid-rendering.md#m75--clustered-local-lighting) clustered local lighting | II | Owner-accepted for integration 2026-09-19; historical image failures retained | M7.1; may run before steps 6–8 |
| 10 | [R2.1–R2.4](roadmap/codebase-restructuring.md#r2--rhi-becomes-rojorhi) RHI becomes RojoRHI | III | R2.1 accepted 2026-09-19; R2.2 next; [proposed record](milestones/r2.md) | M7 complete |
| 11 | [R3.1–R3.6](roadmap/codebase-restructuring.md#r3--subsystems-and-tree-restructure) Donut-style subsystems and tree restructure | III | Inactive; [proposed record](milestones/r3.md) | R2 |
| 12 | [R4.1–R4.2](roadmap/codebase-restructuring.md#r4--shader-source-deduplication) shader source deduplication | III | Inactive; [proposed record](milestones/r4.md) | R3 |
| 13 | [UX2.1–UX2.5](roadmap/editor-experience.md#ux2--scene-documents-and-hierarchy) scene documents and hierarchy | IV | Inactive; [proposed record](milestones/ux2.md) | R4 |
| 14 | [N1.1–N1.4](roadmap/neural-rendering.md#n1--in-shader-inference-lab) in-shader inference lab | V | Inactive | UX2; technically gate B |
| 15 | [M9](roadmap/gpu-driven-hybrid-rendering.md#m9--geometry-lod-and-surface-path-experiments) geometry LOD and surface paths | II | Inactive | M7 and N1 |
| 16 | [M8.1–M8.5](roadmap/gpu-driven-hybrid-rendering.md#m8--shadows-indirect-lighting-floor-and-environment) shadows, indirect floor and environment | II | Inactive | M9; technically M7 |
| 17 | [M10](roadmap/gpu-driven-hybrid-rendering.md#m10--hybrid-scene-query-and-reference-transport) scene query, transport and reflections | II | Inactive | M8; query/reference work needs only M7 |
| 18 | [M11](roadmap/gpu-driven-hybrid-rendering.md#m11--dynamic-gi-and-content-residency) dynamic GI and residency | II | Inactive | M10 for GI; M7 and M9 for residency |

Rows that interleave when their own prerequisites exist, without a fixed step:

- [N2](roadmap/neural-rendering.md#n2--learned-reconstruction-study) after N1; N3's reconstruction
  adapter after N1 and its denoiser after M10; N4 candidates one at a time after N1 and each
  candidate's baseline. None is a condition of any M-slice.
- [R milestones](roadmap/codebase-restructuring.md#opening-a-further-r-milestone) after R4, between
  rendering milestones, adding no rendering scope.
- Area lights and stochastic direct lighting after M7.5; other
  [independent research](roadmap/gpu-driven-hybrid-rendering.md#independent-research-and-graduation-gates)
  behind its own gates; a D3D12 backend only under [ADR 0007](decisions/0007-d3d12-backend-target.md).

The order puts visible cluster geometry and the learned-rendering entry before the shadow and
composition work while preserving every M-slice gate. On 2026-09-19 the owner placed R2, R3, R4
and UX2 between M7 and N1: the RHI moves to its own repository, the tree takes Donut's core/engine/render/app subsystems, the scene
shader variants are deduplicated if the experiment supports it, and scenes become saved documents
before new rendering work starts. Identifiers are names, not ordinals:
M8 and M9 keep theirs although M9 delivers first, because frozen research and accepted records
already use them; the Step column carries the order.
[UX1](milestones/ux1.md) is implemented and owner-accepted for integration after manual review.
M7.1 is implemented and owner-accepted after manual verification on 2026-09-15. Xcode Replay
and validation pass; the original image criterion passes 11/15 and the accepted scoped MetalFX
profile passes 15/15. [The record](milestones/m7.1.md) retains both results and limits. Its executor
plan is closed; [M7.2 acceptance](milestones/m7.2-validation.md#owner-acceptance-and-integration) authorizes integration on 2026-09-15 after manual review, retaining original/revised image-gate failures (13/15 and 9/15). Its plan is closed; no new tolerance or performance adoption follows. Gate B is preserved.

## Current baseline

The accepted rendering baseline is [M7.5](milestones/m7.5.md), owner-accepted for integration on
2026-09-19 after visual review. F2/F3 select its Clustered default; the [follow-up](milestones/m7.5-followup.md)
passes exact causal/current-camera checks while retaining historical-camera replay failures.
Its [validation record](milestones/m7.5-validation.md#owner-acceptance-and-integration) preserves
original failed image gates and cost disclosure; occlusion remains opt-in. The display foundation remains [M6.5](milestones/m6.5.md):
explicit SDR/UI/capture domains, tagged PNG,
manifest v2 with v1 comparison compatibility, and EDR DEFER (ADR 0019). The owner accepted closure
on 2026-09-13 with a narrow historical screenshot-drift exception; original hashes and failed
runs remain intact, and the drift is not fixed. UX1 adds the accepted editor experience. M7.1
adds owner-accepted GPU scene foundations with a scoped vendor comparison criterion; its plan
is closed and the owner approved main integration on 2026-09-15.

This builds on [M6.4](milestones/m6.4.md): optional MetalFX reconstruction, native fallback,
masked San Miguel and offline comparisons. Its manual Sponza switching review and Xcode opaque-
encoder inspection remain explicit follow-ups, not completed checks.

[M6.1](milestones/m6.1.md)–[M6.3](milestones/m6.3.md) provide rigid-object/camera motion, native
TAA with exposure correction, temporal upscaling and dynamic resolution. Their base is the
scene-linear PBR/HDR renderer, validating graph, transient pooling, frame-data path and selection
workspace documented in [Part I](roadmap/rendering-foundations.md). Current implementation detail
belongs to the [architecture](architecture/overview.md) and [frame walkthrough](frame-pipeline.md).
[R1.1](milestones/r1.1.md) adds the module contract, dependency checks and standalone Source header
checks, with an explicit migration allowlist.
[R1.2](milestones/r1.2.md) separates CPU Asset from GPU Scene and clears their migration
allowances. [R1.3](milestones/r1.3.md) adds AppModel and shared scene/frame preparation; the owner
accepted it into local main on 2026-09-13 with an explicit
[six-hash image parity exception](milestones/r1.3.md#unresolved-image-parity). That comparison
remains failed and unexplained; R1.3 is not an integration blocker, and the exception relaxes no
later slice's validation. [R1.4](milestones/r1.4.md) separates the shadow/scene draw stages and
compiled record; its strict parent/candidate matrix passes without exception.
[R1.5](milestones/r1.5.md) completes all five consolidation, decomposition, header-visibility and
build/shader groups. The owner accepted its
[two-hash image parity exception](milestones/r1.5.md#integration) on 2026-09-13; that comparison
remains failed and unexplained, with no outstanding integration blocker or relaxation of later
validation. R1.1–R1.4's structural prerequisite and R1.5 are complete.
[Interface gate B](milestones/interface-gate-b.md) passes on 2026-09-13, approving entry to M7.1
under [ADR 0021](decisions/0021-gpu-scene-handoff-contract.md). The
[editor audit](research/2026-09-14-editor-uiux-audit.md) motivates the intervening UX1 work.
The [UX1 milestone](milestones/ux1.md) records implemented P1–P3 behavior, the owner's acceptance
after manual review, and retained validation limits. Its [design](specs/2026-09-14-ux1-editor-experience-design.md)
is implemented and its executor plan is closed. M7.1 implements shared identities, geometry and
paced scene tables through retained CPU drawing; its [record](milestones/m7.1.md) owns evidence
and retained evidence limits. M7.2 CPU visibility and indirect submission are owner-accepted for integration on 2026-09-15 with failed image gates retained; M7.3 GPU visibility is implemented with [validation](milestones/m7.3-validation.md) and failed exact-image gates retained; owner acceptance and integration authorization were recorded on 2026-09-16.

## Project direction and delivery

Luminex is a Metal 4-first modern rendering playground and portfolio: visible image quality and
verifiable graphics engineering are both outcomes. The
[foundation goals](specs/2026-08-07-luminex-upgrade-design.md) and
[research synthesis](research/2026-08-09-rendering-pipeline-synthesis.md) motivate a graph-scheduled,
GPU-driven hybrid renderer whose raster, screen-space, ray, reconstruction and cache paths share
scene, material, light and temporal semantics. Grow the thin RHI through actual consumers.
[ADR 0007](decisions/0007-d3d12-backend-target.md) governs production backends: D3D12 second when a
validated host exists; Vulkan is research only. A benchmark adapter is not a production backend,
and additional hardware is not a renderer gate.

M5.6 closed as reliability failure / DEFER with no accepted performance conclusion
([ADR 0012](decisions/0012-gpu-submission-defer.md)); it does not block the temporal foundation or
preselect ICB. The [project-fit assessment](research/2026-09-06-graphics-paradigm-project-fit.md)
opened bounded neural-shader research without requiring completion of M8–M11; the
[2026-09-14 direction review](research/2026-09-14-rendering-direction-review.md) confirms M7–M11
against shipped 2023–2026 practice and motivates [Part V](roadmap/neural-rendering.md), the
accepted post-M7 order and the hardware floor (M3/A17 Pro for mesh shaders and ray tracing,
M5/A19 Pro for neural acceleration). CUDA is a training substrate, never a backend.

Each milestone has one recognizable completion outcome. Use a few independently accepted slices;
implementation steps belong in a just-in-time plan or PR, not an expanding series of milestone IDs.
M6's five slices, M7's five and M8's five are fixed in Parts I and II; M9–M11 retain bounded work
areas until planned. R milestones in Part III restructure code between rendering milestones and add no
rendering scope; R2 has four slices, R3 six and R4 two. Part IV owns editor experience and its completion
criteria independently of the rendering and structural milestones; UX2 has five slices. Part V owns the four learned-rendering slices, each an
independently accepted study that never becomes a correctness dependency of the shared frame; N1
is itself four slices. The [execution sequence](#execution-sequence) and stated prerequisites,
rather than numerical order, determine entry. Only one
implementation plan is active at a time; independent entry does not start another plan.

Every rendering slice includes a diagnostic fixture, relevant intermediate views, deterministic
seeds/camera tracks and declared temporal warmup, plus raw/reference and final captures. Retain
Sponza and Helmet integration checks. Freeze a device, resolution, content and CPU/GPU/memory
budget before measuring; 60 real frames/s is a planning target, not an unmeasured performance claim.
Report full update/render/reconstruction/composite cost, timing variation and unavailable counters.
Track pipeline variants, cache misses and compilation stalls when a slice introduces them. Grow
depth, normal, roughness, motion and identity outputs only for real consumers with shared meaning;
add no speculative G-buffer. Each feature states its fallback, overflow and reset behavior.
