# ADR 0023: Share local-light evaluation across direct and clustered paths

**Status**: Accepted (2026-09-19)

## Context

The renderer already evaluates three directional lights, image-based lighting and emissive in
scene-linear space before one pre-exposure multiply. Point and spot lights need a common physical
interpretation and a bounded selection path without changing those existing terms. The
[M7.5 specification](../milestones/m7/m7.5.md) owns the frozen constants and verification families.
[ADR 0021](0021-gpu-scene-handoff-contract.md) continues to own scene identity and paced updates.

## Decision

### Light model and table

Point intensity uses the existing directional-light scale: at one metre and normal incidence,
its inverse-square factor is one before the finite-range window. Strength is linear RGB colour
multiplied by intensity. This follows the candela/lux relation without claiming absolute
photometric calibration. Attenuation is
`saturate(1 - (distance / range)^4)^2 / max(distance^2, 0.01^2)`; the finite range is mandatory.
A spot multiplies by `saturate(cosTheta * spotScale + spotOffset)^2`, with CPU-derived scale and
offset from its inner and outer cone half-angles. Points encode scale zero and offset one.
Distance at or beyond range, zero cone response and nonpositive N.L return exact zero, in that
order. Fast math remains enabled for scene shaders; the exact mathematical cone edge may retain
a tiny numerical response.

`ComputePunctualLight` and its CPU mirror share the directional BRDF core. The local sum follows
the directional sum and precedes ambient/emissive and the existing pre-exposure multiplication.
Local lights affect opaque and masked surfaces and cast no shadows.

`LightRow` is a 64-byte shared ABI: position/range, strength/spotScale, direction/spotOffset and
boundCentre/boundRadius occupy four 16-byte groups. Range zero denotes a free or disabled stable slot (2026-09-19 amendment below).
Point bounds use the range sphere; spots with outer angle at most 45 degrees use their tighter
cone sphere. Radius inflation is fixed at `1 + 2^-10`. `LightId` is generational and scene-owned;
light updates use three paced tables, dirty-row writes, doubling growth and retirement after
lastFrame + 3. The identity cap is 4096, including disabled lights. Light edits do not invalidate occlusion coverage.
All edits precede `prepareFrame`; the borrowed table and row span stay consistent through frame
declaration. `lightRowCount` is the addressable high-water mark, distinct from `liveLightCount`.

### One shading loop and explicit bindings

`LocalLightMode { Off, Direct, Clustered }` selects a runtime loop; no pipeline permutation is
added and `PassUniforms` remains 400 bytes. Direct visits addressable row indices in ascending
order, including free rows that contribute zero. Clustered visits an ascending subset. Both
selection paths call the same function at the same accumulation site, so dropping exact-zero
terms preserves the order of all nonzero terms. Off visits no rows.

The scene pass binds light rows at b8, cluster grid at b9, flat indices at b10 and the 136-byte
scalar-packed `LocalLightParams` at b11. Its fields are mode, rowCount, grid dimensions, active
origin/extent and 25 reversed-Z slice depths. Persistent resources use `bindBuffer`; the copied
per-frame parameter block uses `bindFrameData` under ADR 0010. Every declared slot is bound,
including unused slots, using immutable stage-owned minimal fallback buffers outside the graph.
Only enabled lights justify importing `lmx.scene.lights`; zero-light frames retain their graph
resources, passes and golden dumps. A Clustered request with live lights requires the grid and
index list produced for that same frame; missing outputs are a contract violation.

### Cluster selection contract

The cluster consumer uses a 16 by 9 by 24 grid over the active render rectangle.
Integer pixel lookup matches pixel-aligned tile edges `ceil(tile * extent / tileCount)`.
The CPU-uploaded depth table covers near to 0.3 m, 22 exponential intervals to 100 m and an open
last slice. Empty pixel tiles and collapsed slices list nothing. Froxel bounds use the jittered
projection which rasterizes the frame; the open slice extends only to each light's far reach.

Count, scan and fill share the CPU mirror's ordered fp32 comparisons under safe math. Count
records raw intersections. Scan caps each froxel at 128 lights and grants ranges in froxel
order within the global 65,536-index capacity. Fill keeps lowest row indices in ascending order.
Record count bit 31 marks truncation; readers mask it off. Assigned and dropped counters must
reconcile exactly with candidates. Overflow deliberately omits contributions, remains bounded,
and must be visible through diagnostics. No performance result can excuse a correctness failure.

### Diagnostics and frame ownership

When checking is enabled, the CPU mirror consumes the declaration's borrowed rows before an update
can change them. Retired GPU grids, defined index prefixes and counters join the same frame ID;
mode changes cannot relabel older in-flight results. A final idle drain resolves remaining frames.
The check requires exact grid, index and counter equality; disabled checks retain no list capture.
`LightingStatus` publishes requested/effective mode, frame and scene identity, live count, counters
and list allocation/use. Requested Clustered with zero live lights is effective Off, with no new
light import, list pass or debug pass; the high-water row count does not determine participation.

The post-display debug stage loads the actual depth pixel, maps it to the active render froxel and
reads that frame's rows/lists. Count has a fixed palette; Overflow marks truncation; Missed checks
every row's range/cone reach independently of N.L and reports absent reaching lights as red, or
yellow only in truncated froxels. It reconstructs position from depth and is separate from the
shading-equality gate. Display first writes an SDR source transient; the diagnostic writes the public display target,
never scene colour or temporal history. Temporal/HZB debug views are mutually exclusive with a non-off light view.

## 2026-09-19 follow-up amendment

`LocalLight::enabled` preserves authored values, IDs and orbit bindings while uploading an inert
row. `localLights()` includes disabled identities; `enabledLightCount()` supplies rendering
`liveLightCount`. Sponza authors 16 lights before finalize; CLI rig off disables them without
removal/allocation rollback. Hierarchy owns individual enable checkboxes; Rendering has no rig toggle.

The shared punctual accumulator now applies normal-footprint NDF filtering before divergent
list traversal, using Tokuyoshi/Kaplanyan 2021 Eq.13. It changes only punctual specular alpha;
authored roughness, directional/IBL terms, attenuation and the 64-byte ABI remain unchanged.
The [follow-up](../milestones/m7/m7.5-followup.md) records filtering/content diagnostics, a passing
exact zero-enabled causal gate and current-camera 15/15 replay. Historical-camera repeatability
remains failed/unresolved; original F5 failures and frozen costs are not relabelled.
Mode-only editor changes at zero enabled lights preserve temporal continuity; content/live-mode
changes retain their resets. Exact causal validation requires equal inputs and incoming history,
exposure and reset state, with a nonempty qualifying set and no added pixel tolerance.

## Consequences

The owner accepted this implemented contract on 2026-09-19 after visual review and authorized
integration. [Acceptance and limits](../milestones/m7/m7.5-validation.md#owner-acceptance-and-integration)
retain original and historical-camera failures; acceptance does not convert them to passing gates.
Direct was the provisional default through the frozen candidate collection. Family 2 lossless lists and scoped
family 3 exact images then passed, satisfying the fixed rule for Clustered default adoption;
Direct remains the reference. The [default decision](../milestones/m7/m7.5-validation.md#default-decision)
records that scope. Zero-light parent comparisons, temporal difference reports, overflow checks,
cost and GPU capture remain separate conclusions in the validation record; default adoption does not waive a failed gate.

These units, identities and row semantics constrain future shadow/indirect consumers without
adding glTF light import, area lights, transparency or local-light shadows. The grid and fixed
capacities deliberately precede performance evidence; refinements require a separate decision.
