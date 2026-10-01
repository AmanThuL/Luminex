# Cluster Geometry

**Status**: Accepted

Part VI of the [rendering roadmap](../roadmap.md) owns cluster geometry: a crack-free cluster
hierarchy built offline, selected, culled and drawn on the GPU, and the visibility and surface work
that moves it toward pixel-scale triangles. It replaces M9, "Geometry LOD and surface-path
experiments", which the owner retired on 2026-10-01 after the
[geometry direction review](../research/2026-10-01-geometry-direction-review.md) compared it with
Nanite in current Unreal Engine source. Documents dated earlier use M9 for the pre-split milestone.
These are accepted future boundaries, not shipped capabilities; no cluster path exists.

## Placement and ownership

The roadmap entry's [execution sequence](../roadmap.md#execution-sequence) owns the cross-part
order: N1 → G1 → G2 → M8 → G3 → M10 → M11. Each shadow view multiplies the cluster candidates a
frame must test, so G2 precedes M8's cascades and atlas; G3 follows M8 and uses its forward shading
and guides as the comparison target. Only one implementation plan is active at a time.

[GPU-Driven Hybrid Rendering](gpu-driven-hybrid-rendering.md) owns the scene, visibility and
lighting contracts these slices consume (M7), the shadow views that draw cluster geometry (M8.1,
M8.2), the ray-query proxies (M10) and residency, including geometry pages (M11). This part owns
the hierarchy, its bake and oracle, cluster culling and raster, same-frame occlusion, hierarchy
traversal and the surface-path decision. Part II states the hardware floor; G1 and G2 run on every
Metal 4 device, and G3.3 needs Apple M2 or later.

## Shared rules

- Ordinary raster and Forward+ stay the reference and the default. A cluster path is off by
  default until a gate adopts it, and existing scenes render unchanged with it off. Whether its
  entries sit beside the existing scene shaders or in a pass family of their own is the G1 plan's
  decision under [ADR 0027](../decisions/0027-scene-pass-deduplication-defer.md)'s twin rule.
  Sponza and MaterialLab integration checks run with the path off.
- Correctness gates are set equality against a CPU mirror, counter reconciliation, an independent
  ID render and exact images with temporal reconstruction off. LOD output cannot equal the
  ordinary path, so it is gated on geometric deviation derived from its own selection threshold:
  stored error bounds measured deviation at bake time, the GPU cut equals the CPU mirror at every
  threshold, and the rendered cut stays within a declared multiple of it. Every factor is declared and frozen
  before the scored measurement. Color difference and flicker are reported, never gated.
- Metal has no multi-draw with a GPU-read count. Visible clusters are drawn with one draw per
  pipeline state; per-cluster indirect command buffers are not used.
- Every buffer the GPU fills has an explicit capacity, counted overflow and a visible diagnostic;
  forced overflow is safe. Counters are written by the shaders and read back at retirement.
- Content is fetched at setup from pinned sources with license and provenance. Alpha-masked and
  aggregate geometry stays outside cluster LOD and draws through the ordinary path; San Miguel is
  the recorded hard case.
- An RHI addition is a `rojo-rhi` change with a named consumer slice and a conformance case, and it
  keeps checkpoint A. Experiment source follows the `exp/<topic>` rule; conclusions and adopted
  code return to `main`.
- Measurements freeze the device, content, resolution and method, and are published with them.

## Dependency map

| Area | Required foundation | Independent ordering |
|---|---|---|
| G1 cluster geometry | M7.1–M7.5 and M6 temporal contracts | G1.1 → G1.2 → G1.3 → G1.4; planned without an RHI change |
| G2 scalable visibility | G1.3; the M7.4 pyramid and oracle | G2.1 and G2.2 in either order; before M8 in the accepted order |
| G3 surface path | G1 and the G1.1 probes; `rojo-rhi` additions | G3.1 → G3.2 → G3.3; M10 builds G3.1's module if it starts first |
| M8.1, M8.2 shadow views | G1.3 view records | Owned by Part II |
| M10 proxies | G1.2 bake | Owned by Part II |
| M11 geometry pages | G1.2 addressing and residency table | Owned by Part II, after texture mips |

## G1 — Cluster geometry

**Outcome:** dense scenes render through a crack-free cluster hierarchy that is selected on the GPU
and drawn by the existing forward path, with a published native-Metal measurement table, while
ordinary raster remains the reference. G1 finishes when the four slices below pass.

**Deliver:** through G1.1–G1.4: dense licensed fixtures and a GeometryLab with forward baselines;
an offline bake with a CPU cut oracle; cluster tables, a flat GPU cut and a single-draw raster back
end; a tuned error metric and the measurement table.

**Sequence:** G1.1 → G1.2 → G1.3 → G1.4.

**Exit gate:** all four slices pass their gates and the shared rules above.

**Defer:** per-cluster occlusion and hierarchy traversal to G2; a visibility buffer and compute
rasterization to G3; quantized payloads, pages and streaming to M11; mesh-shader execution to
independent research; alpha-masked geometry, deformation and general asset tooling.

## G1.1 — Dense content and forward baselines

**Outcome:** Dense, licensed content and a measured baseline of the ordinary path exist before any cluster code.

**Deliver:** Two CC0 photogrammetry fixtures fetched at setup with pinned hashes, license and provenance, with a many-material scene as an optional fetch; a GeometryLab with a seeded instanced field; shader-written counters and the measurement-schema extension; the M7 path's triangles, GPU pass times and memory against instance count on that content; a timing sweep of the forward pass over triangle size, comparing the full and a trivial fragment shader at fixed covered pixels; an alpha-only depth control for masked materials on San Miguel.

**Exit gate:** Fixtures reproduce by hash and carry their license and provenance; the lab is deterministic under its seed; the baseline table and both probe results are recorded with their method on the frozen device; no cluster capability is claimed.

**Defer:** Cluster data to G1.2; the surface-path decision the probes inform to G3.

## G1.2 — Cluster bake and cut oracle

**Outcome:** Each fixture has a deterministic baked cluster hierarchy whose cuts are proven crack-free on the CPU.

**Deliver:** An offline bake in the texture-bake pattern over a pinned, maintained clusterization library, with a versioned format and manifest; an error and bounds per group, a generating-group reference and a level tag per cluster; one hierarchy per glTF primitive, with positions shared between primitives locked; cluster data that references the scene's shared vertex pool; the group hierarchy for later traversal; verified complete per-level meshes and a fixed-error proxy per mesh; a CPU oracle over random views.

**Exit gate:** A rebuild is bit-identical on the frozen toolchain; every cut covers each surface exactly once with matching boundaries; error is monotonic and bounds nest; each group's stored error bounds its measured position deviation from the source surface within a factor declared before measurement; triangles in groups that never simplify stay under a declared budget; bake time and size are reported; no GPU work is claimed.

**Defer:** The GPU path to G1.3; quantized payloads and pages to M11; fields for deformation until skinning has an owner.

## G1.3 — GPU cut and single-draw raster

**Outcome:** The GPU selects and draws a crack-free cut for every view through the existing forward shading.

**Deliver:** Immutable cluster and group tables with their ABI oracle; view records carrying a view index, a per-view LOD scale and a depth-only output mode; expansion of visible instances into clusters; a flat frustum and LOD cut with a CPU mirror; visible-cluster records indexed per frame; a residency table read by selection, with a forced non-resident mode; one draw per pipeline state through fixed-topology instancing or a GPU-compacted index buffer, chosen by paired measurement with per-cluster draws measured beside them; a cluster and level debug view through an ID pass. Opaque materials only; clusters are not occlusion-culled, and instance-level M7.4 occlusion stays opt-in.

**Exit gate:** The visible-cluster set equals the CPU mirror for every view and threshold; a cut forced to zero error is byte-identical to the ordinary path with temporal reconstruction off and shows the same triangle per pixel under an independent ID render; groups forced non-resident leave no hole; forced overflow is safe and visible; existing scenes are unchanged with the path off.

**Defer:** Per-cluster occlusion to G2.1; traversal to G2.2; quality tuning and the published table to G1.4; shadow views to M8.1.

## G1.4 — LOD quality and published measurements

**Outcome:** The error metric is tuned against measured geometric deviation, and a native-Metal measurement table is published.

**Deliver:** Attribute weights and builder options chosen with evidence; screen-space geometric deviation of each cut from the full-detail reference at several thresholds with temporal reconstruction off; color-difference and flicker measures along a rail; a table of cluster tests per second, triangles per second by back end, rasterized against source triangles as instance count grows to the flat pass's measured knee, memory, bake time and bytes per triangle.

**Exit gate:** At each tested threshold the measured screen-space geometric deviation stays within the multiple of that threshold declared before measurement; rasterized triangles stay within a stated factor as instance count and source complexity grow, up to the knee; color-difference and flicker measures are reported per threshold; the table is published with device, content, resolution and method; ordinary raster stays the default unless paired evidence adopts the cluster path.

**Defer:** Instance counts beyond the knee to G2.2; foliage and other aggregate geometry.

## G2 — Scalable visibility

**Outcome:** culling cost follows visible detail beyond the flat pass's knee, and occlusion loses
nothing within a frame. G2 finishes when both slices pass.

**Deliver:** through G2.1–G2.2: same-frame occlusion recovery for instances and clusters, and
traversal of the baked group hierarchy.

**Sequence:** G2.1 and G2.2 need only G1.3 and may run in either order.

**Exit gate:** both slices pass; the flat pass and single-phase occlusion remain as oracle and
fallback.

**Defer:** light-space occlusion for shadow views to M8; an instance hierarchy until instance
counts require one; persistent-thread traversal, which depends on scheduler behavior no API
defines.

## G2.1 — Two-phase occlusion

**Outcome:** Occlusion culling of instances and clusters loses no visible geometry within a frame.

**Deliver:** Reject lists written by the first phase for instances and clusters that pass frustum and LOD and fail only previous-frame occlusion; a pyramid built from first-phase depth; a second classification with current transforms and a second draw on the forward path; the M7.4 ID oracle extended to clusters.

**Exit gate:** On the M7.4 motion rails and the GeometryLab rails no reference-visible identity is missing in any frame; the second phase's cost and the render-pass-split cost are reported separately; occlusion stays opt-in unless paired production evidence adopts a default.

**Defer:** Moving the first phase onto a visibility target to G3.2's decision.

## G2.2 — Hierarchical traversal

**Outcome:** Cluster culling cost stops growing with total cluster count.

**Deliver:** The baked group hierarchy traversed with one indirect dispatch per level; node and cluster candidate queues with explicit budgets and overflow reporting; the measured flat-pass knee as the workload boundary.

**Exit gate:** The visible set is identical to the flat pass on every fixture; beyond the knee, eight times the instances costs under 1.5 times the culling time; forced overflow is safe and visible.

**Defer:** Sharing one traversal across many shadow views until M8 measures the need.

## G3 — Surface path for pixel-scale triangles

**Outcome:** a measured decision on shading and rasterizing pixel-scale triangles on Apple GPUs,
recorded as adopt, retain or defer per workload, with Forward+ as the reference. G3 finishes when
the three slices below close with that record.

**Deliver:** through G3.1–G3.3: indexed material textures and a reconstruction module; a
visibility target and resolve compared with forward shading; a compute rasterizer study.

**Sequence:** G3.1 → G3.2 → G3.3. G3.2 proceeds only if the G1.1 probes show a small-triangle or
masked cost a resolve could remove; otherwise G3.2 and G3.3 close as defer with that evidence.

**Exit gate:** each slice meets its gate or records its negative result; Forward+ and ordinary
raster remain the oracle and the transparency path.

**Defer:** a visibility buffer for all opaque geometry and a compact G-buffer, which are reached
only through a measured result; material classification, which one material model does not need.

## G3.1 — Indexed material textures and reconstruction

**Outcome:** One shader invocation can reach any material's textures and reconstruct a surface from an identity.

**Deliver:** An unsigned-integer render-target format and material texture access beyond per-draw slots in `rojo-rhi`; a shared module that reconstructs barycentrics, attributes and texture gradients from an instance, a triangle and a pixel position, with oracles against hardware barycentrics and derivatives. M10's hit shading consumes the same module.

**Exit gate:** The module agrees with hardware within a declared numeric bound on the material and mip fixtures; the RHI additions land with their conformance cases; no surface path changes.

**Defer:** The resolve to G3.2.

## G3.2 — Visibility target and resolve

**Outcome:** A visibility resolve for cluster geometry is measured against forward shading and adopted, retained or deferred.

**Deliver:** As an experiment: hardware-rasterized cluster identity in an ordinary render target, and a resolve that shades with the clustered lights in a tile-memory form and a compute form, on the dense fixtures.

**Exit gate:** Identity is exact against an independent ID render; reconstruction stays within G3.1's bound; color difference against forward is reported; paired whole-frame cost against forward is recorded with the decision.

**Defer:** Compute rasterization to G3.3; any change of default without paired evidence.

## G3.3 — Compute rasterizer study

**Outcome:** A triangle-parallel compute rasterizer is measured against hardware raster on small clusters.

**Deliver:** A rasterizer that writes depth and identity with one 64-bit atomic maximum, on M2 and later, selected per cluster by projected edge length and merged with the hardware target. Color views need G3.2 adopted; a depth-only form for shadow views does not.

**Exit gate:** The driver defect in which fragment-stage texture atomics are dropped is probed first; coverage matches hardware on a fill-rule set; no crack appears where the two rasterizers meet; a speedup on sub-threshold clusters is stated, or the negative result is recorded.

**Defer:** Tessellation, voxel and curve primitives.

## Boundaries and deferrals

Cluster acceleration structures do not exist in Metal, so ray queries trace the G1.2 proxies.
Foliage, deforming geometry, tessellation and mesh-shader execution are placed in Part II's
[independent research](gpu-driven-hybrid-rendering.md#independent-research-and-graduation-gates).
Translucent materials on baked meshes follow M8.4. No slice here makes a capability above the
Metal 4 baseline mandatory.
