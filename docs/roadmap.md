# Rendering Roadmap

**Status**: Accepted

This entry and its four parts jointly own current milestone identifiers, boundaries, dependencies,
outcomes, gates and deferrals. Each boundary is defined in one part; frozen research preserves
its supporting evidence. Starting a milestone requires an `In progress` plan under `docs/plans/`
that decomposes the accepted boundary without expanding it.

## Four parts

| Part | Scope | Purpose |
|---|---|---|
| [Rendering Foundations](roadmap/rendering-foundations.md) | M4–M6.5 and interface gate B | Establish image formation, execution, inspection, temporal and display contracts; retain acceptance limits |
| [GPU-Driven Hybrid Rendering](roadmap/gpu-driven-hybrid-rendering.md) | M7–M11 and independent research | Scale scene data, visibility and lighting, then evaluate geometry, transport, GI and residency |
| [Codebase Refactoring](roadmap/codebase-refactoring.md) | R1 between M6.5 and gate B; later R milestones | Restructure modules and large units between rendering milestones without changing output |
| [Editor Experience](roadmap/editor-experience.md) | UX1 after gate B and before M7.1 | Make scene inspection, controls and diagnostic data understandable and reliable for a human operator |

The dividing point is the change from a trustworthy moving image and execution substrate to
shared GPU scene data and its consumers. Foundation completion supplies those contracts;
[interface gate B](roadmap/rendering-foundations.md#m6--temporal-and-display-foundation)
approved entry to M7.1 in its [separate review](milestones/interface-gate-b.md). The second part's
[dependency map](roadmap/gpu-driven-hybrid-rendering.md#dependency-map) explains independent entry.
[UX1](milestones/ux1.md) is implemented and owner-accepted for integration after manual review.
M7.1 is next and remains inactive pending its own plan; gate B's technical approval is preserved.

## Current baseline

The shipped baseline is [M6.5](milestones/m6.5.md): explicit SDR/UI/capture domains, tagged PNG,
manifest v2 with v1 comparison compatibility, and EDR DEFER (ADR 0019). The owner accepted closure
on 2026-09-13 with a narrow historical screenshot-drift exception; original hashes and failed
runs remain intact, and the drift is not fixed. UX1 adds the accepted editor experience; no executor plan is active.

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
is implemented and its executor plan is closed. M7.1 is eligible for its own implementation plan;
no GPU-scene or GPU-visibility path is adopted yet.

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
also opens bounded neural-shader research without requiring completion of M8–M11.

Each milestone has one recognizable completion outcome. Use a few independently accepted slices;
implementation steps belong in a just-in-time plan or PR, not an expanding series of milestone IDs.
M6's five slices and M7's four are fixed in Parts I and II; M8–M11 retain bounded work areas
until planned. R milestones in Part III restructure code between rendering milestones and add no
rendering scope. Part IV owns editor experience and its completion criteria independently of the
rendering and structural milestones. Stated prerequisites and accepted delivery order, rather than
numerical order, determine entry. Only one
implementation plan is active at a time; independent entry does not start another plan.

Every rendering slice includes a diagnostic fixture, relevant intermediate views, deterministic
seeds/camera tracks and declared temporal warmup, plus raw/reference and final captures. Retain
Sponza and Helmet integration checks. Freeze a device, resolution, content and CPU/GPU/memory
budget before measuring; 60 real frames/s is a planning target, not an unmeasured performance claim.
Report full update/render/reconstruction/composite cost, timing variation and unavailable counters.
Track pipeline variants, cache misses and compilation stalls when a slice introduces them. Grow
depth, normal, roughness, motion and identity outputs only for real consumers with shared meaning;
add no speculative G-buffer. Each feature states its fallback, overflow and reset behavior.
