# ADR 0026: Core's charter and the placement of domain-free code

**Status**: Proposed (2026-09-21) · **Roadmap**: ../roadmap/codebase-restructuring.md

## Context

[ADR 0020](0020-module-layering-and-units.md) made `core` the unit every other unit may depend on
and gave it one admission rule, which the [module convention](../conventions/modules.md) states: a
helper with no domain meaning and consumers in two or more units goes to `core`; one consumer
keeps it local. [ADR 0025](0025-engine-subsystem-and-render-on-engine.md) applied it once, moving
`Bounds.h` down because three units used it.

The rule kept Core small, and it also decided by accident where the project's math lives. At R3.3
Core is 506 lines: logging, assertions, alignment, whole-file reading, JSON escaping, numeric
parsing, colour transfer, one AABB header and one division helper. Frustum plane extraction and
the AABB test are in a Render visibility unit. The reversed-infinite-Z projection is in Engine's
camera, the sphere-to-orthographic fit in a shadow stage, TRS decomposition in the glTF loader's
folder, SHA-256 in the texture baker, cube-face solid angles in the IBL generator. The radical
inverse is implemented twice, in Render for jitter and in Asset for Hammersley. A bounding sphere
is derived from an AABB in four places and an AABB accumulated from points in four more. Five
generational identifiers are five copies of one struct, their slot allocator is in an anonymous
namespace, and three bounded histories in App are each written by hand. None of this code names a
scene, a pass or a panel. Each piece stayed where it was first needed because it had one consumer
on the day it was written.

NVIDIA's Donut, the structure [R3](../milestones/r/r3.md) checks itself against, gives `core` the
math (vector, matrix, affine, quaternion, box, frustum, sphere, colour) and the generic containers,
and its engine and render layers name only scene and pass concepts.

## Decision

Core owns the math and the data structures that carry no domain meaning, and the general services
it already has. It is organised as `Math/`, `Containers/`, `Util/`, `IO/` and `Diagnostics/`.

The placement rule gains a first clause. Math and data structures with no domain meaning go to
`core` whatever their consumer count. A domain meaning keeps code in its owning unit: a function
that takes or returns a scene, asset, render or app type, or that fixes a constant belonging to
one (the 16×9×24 froxel grid, the light-row layout), is not generic, and the generic part is cut
out from under it rather than the whole moved. Every other helper keeps ADR 0020's two-consumer
rule. Extraction still preserves each caller's contract, and two implementations are unified only
when pinned output proves their contracts identical.

glm is the one vector, matrix and quaternion vocabulary, in Core and above it. Core's math is the
geometry and the algorithms over glm types: boxes, spheres, planes and frusta with their tests,
TRS transforms, projection and view builders, low-discrepancy sequences, sampling measures, colour
transfer and scalar helpers. Core does not define vector types or aliases of its own.

Core's containers are those the code has needed: a ring buffer, a tagged generational handle with
its slot allocator, a dirty set, an interval. Core does not wrap standard containers.

Core's dependencies do not change: spdlog and glm, no project unit, and no RojoRHI header. A type
that must name an RHI type stays above Core. The contract adds a link check that the `Core`
archive has no undefined reference into `lmx::engine`, `lmx::asset`, `lmx::render`, `lmx::app` or
`rojoRHI`.

Code that enters Core arrives with unit tests of its own, before any call site uses it, and keeps
the numerical contract it had: a function compiled with `-ffp-contract=off` is compiled with it in
Core.

Authored content leaves Engine. The catalog scenes and labs become the unit `scenes`
(`Source/Scenes`, target `Scenes`, namespace `lmx::scenes`), which may depend on `core`, `asset`
and `engine`. `engine` may not depend on it. App, AppModel and Tests, the catalog's consumers
today, depend on `scenes`.

## Consequences

Core grows from about five hundred lines to a few thousand and becomes the first place to look for
geometry, and its tests become a suite rather than three files. A reader of Render or Engine sees
scene and pass logic without the arithmetic under it.

The single-consumer clause invites speculative additions, which the two-consumer rule prevented.
Three limits replace it: the code must already exist in the repository or be needed by the change
that adds it; Core takes no vector types, container wrappers, virtual file system, thread pool,
platform or profiling layer until a consumer exists and a decision records it; and a domain
meaning is judged by signature and constants, which review can check.

Moving a function between translation units can change inlining and therefore floating-point
results. The comparison protocol, the IBL bake hashes and the image hashes are checked at every
extraction commit, and an extraction that cannot hold them is left in place with the reason
recorded.

Identifier types become aliases of one template. Their size, layout, stale and foreign rejection
and the contract of [ADR 0021](0021-gpu-scene-handoff-contract.md) do not change.

This decision changes no rendered output, shader, uniform layout, pass label, schema, CLI option
or existing test case name. It narrows ADR 0020 in its placement rule and Core's ownership row,
and ADR 0025 in where the catalog lives. The unit model, the reach semantics, the allowlist
policy, Asset's independence and the Render-on-Engine edge stand.

## Alternatives considered

**Aliases over glm (`lmx::float3`), as Donut and other engines spell them.** Rejected: it hides
glm behind one header at the cost of a repository-wide rename through every frozen-layout struct,
and nothing plans to replace glm.

**A math library of Core's own.** Rejected: thousands of lines of numerics with no rendering
benefit and a direct risk to every exact-image gate.

**A fuller foundation layer: container wrappers, VFS, thread pool, platform, profiling.** Rejected
for now: none has a consumer, and the engineering convention rules out abstraction ahead of need.
Each returns as its own decision when one does.

**Keep the two-consumer rule and extract as consumers appear.** Rejected: it is the rule that
produced the present distribution, and it makes Core's content a function of history rather than
of responsibility.

**Leave the catalog in Engine.** Rejected: two thousand lines of authored scenes in the scene
machinery's library make Engine's archive depend on what the project happens to demonstrate, and
UX2's scene documents need the two apart.

## Cross-references

- [Module contract](../conventions/modules.md) — the enforceable form of this decision.
- [R3 record](../milestones/r/r3.md#r34--core) — sources, cuts and order of extraction.
- [Part III](../roadmap/codebase-restructuring.md#r34--core) — outcome and exit gate.
