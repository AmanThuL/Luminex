# ADR 0021: Preserve frame contracts while introducing scene identities

**Status**: Accepted (2026-09-13)

## Context

[Interface gate B](../roadmap/rendering-foundations.md#m6--temporal-and-display-foundation)
reviews the restructured M6.5/R1 baseline before the first shared GPU-scene consumer.
[M7.1](../roadmap/gpu-driven-hybrid-rendering.md#m71--gpu-scene-foundation) introduces stable
instance/mesh/material/texture identities and GPU tables through the retained CPU draw path.
Today, `Scene::view` builds a borrowed, frame-local `DrawItem` span containing resource pointers,
copied materials and current/previous transforms. That span is not a persistent scene database.

The review finds the existing frame-data, temporal, synchronization and capability contracts
suitable for that migration. What needs an explicit decision is the meaning and lifetime of
identities and table updates, without prematurely choosing a universal scene layout or RHI.
The [gate record](../milestones/interface-gate-b.md) owns the evidence and admission result.

## Decision

### Keep the execution and reconstruction contracts

- [ADR 0010](0010-execution-model-partial-reshape.md) remains binding. `bindFrameData` combines
  allocation, copying and address binding; its returned `GpuAddress` is usable only within the
  producing frame. Persistent data uses owned `Buffer` objects and `bindBuffer`. GPU-scene work
  must not retain frame-data addresses, restore `setUniforms`, promote NoApi, or replace the
  resource/pass/pipeline/residency/barrier model.
- A frame's allocator, argument table and frame-data pages recycle only after its GPU retirement,
  with three frames in flight. Temporal history's two ping-pong slots describe successive rendered
  frames; their count and chronology do not become the frame allocator's three-slot cycle.
- Preserve [ADR 0013](0013-temporal-motion-and-history-contract.md) and its refinements in
  [ADR 0014](0014-temporal-reconstruction-and-exposure-correction.md),
  [ADR 0015](0015-temporal-slot-terminal-access.md),
  [ADR 0016](0016-active-render-extent-and-resolution-control.md) and
  [ADR 0017](0017-vendor-reconstruction-capability.md). Previous transforms belong to the same
  object in the previous accepted rendered frame. Skipped frames do not commit previous state.
  Motion is unjittered `uvCurrent - uvPrevious`; unsupported motion remains explicitly invalid.
  Output extent, active render extent, jitter and exposure keep their existing domains and reset
  rules. Native TAA stays the default/reference; vendor history keeps its separate reset/lifetime.
- Preserve scene-linear material and lighting values, pre-exposure, masked coverage and the
  display/UI/capture boundary in [ADR 0018](0018-masked-material-coverage.md) and
  [ADR 0019](0019-display-domains-and-edr.md). Table indirection changes where inputs come from,
  not their meaning or the supported material set.

### Minimal identity semantics for the first consumer

1. Instance, mesh, material and texture identities are distinct, scene-owned logical namespaces.
   They are not CPU pointers, GPU addresses, argument-table slots, draw-list positions or compacted
   table row numbers. The existing catalog `SceneId` and temporal `sceneGeneration` have different
   jobs and are not substitutes for object identity.
2. An identity continues to name the same live entity through ordinary updates and storage/draw
   reordering. Sharing one mesh or material does not merge instance identities or previous poses.
   An explicit mapping may translate stable identities into the table rows of a particular frame.
3. Removal or scene replacement invalidates old references. Reusing storage must not let a stale
   identity resolve to a different entity; the implementation must prevent reuse or distinguish
   incarnations. Invalid references must be detected before an out-of-bounds GPU access. A null
   optional texture resolves to the existing semantic fallback, never an unbound descriptor.
4. Current and previous transforms follow instance identity through remapping. A new or replaced
   instance must not inherit a removed entity's previous transform. M7.1 must explicitly derive
   motion invalidation or the existing scene-history reset when continuity cannot be established;
   table relocation alone does not mean the scene changed.

These are semantic requirements for M7.1, not implemented IDs. Bit widths, generation encoding,
allocation strategy, table packing and capacity are chosen with that consumer and its ABI tests.
There is no requirement for cross-scene persistence, serialization IDs, an ECS, streaming or a
general residency system.

### Frame-consistent updates, bindings and ownership

- Each submitted frame must see a mutually consistent set of instance mappings, mesh/material/
  texture references and current/previous poses. Updating CPU scene state for a later frame must
  not rewrite storage still read by an earlier frame. Keep referenced resources and mappings alive
  until every submitted consumer retires, including when a table grows or an entity is removed.
- M7.1 must choose a concrete safe update/retirement mechanism: for example, immutable replacement
  resources retained until completion, paced frame-slot copies, or explicitly ordered GPU copies.
  A modulo slot number alone is not proof of retirement. `Device::waitIdle` is an available coarse
  fallback; a new public retirement query or upload API requires a real consumer and justification.
- Mutable GPU tables and upload/copy operations participate in the render graph's declared buffer
  dependencies and resource lifetimes. Import persistent resources with their recorded terminal
  uses. Residency does not order accesses, and binding an address does not declare a graph edge.
  Copies, storage writes, shader reads and indirect argument reads retain their distinct uses.
- Persistent tables can use the existing buffer binding model. Texture identities need not become
  native descriptor indices: the retained CPU path can resolve an ID and bind the corresponding
  texture per draw. Respect the existing shared buffer-slot namespace and separate texture/sampler
  namespaces; do not assume unbounded descriptor indexing or freeze a new bindless API at this gate.
- Scene owns scene resource/identity lifetime; Render consumes the shared lower-level contract.
  Concrete shared types follow [ADR 0020](0020-module-layering-and-units.md) and the module rules.
  Public RHI interfaces remain free of scene types and Metal/ImGui dependencies.

### Capabilities and evidence boundaries

Retain neutral capability discovery, explicit creation errors and observable tested fallback.
`DeviceCapabilities::temporalScaler` selects reconstruction only; it says nothing about GPU
submission. Do not infer ICB, descriptor indexing or GPU-driven drawing support from MetalFX or
from the backend name. Add any new capability with its first real consumer and semantic tests.

M7.1's CPU drawing path is the first table consumer and reference. M7.2 owns visibility,
submission and production measurements; ICB remains optional. M5.6's reliability failure/DEFER
provides no accepted performance result. A future D3D12 implementation must satisfy checkpoint A
and the shared frame semantics; it is not required for this gate. EDR adoption is not required.

## Consequences

Gate B can admit M7.1 without a speculative RHI redesign or GPU-scene implementation. Its plan
must turn the requirements above into bounded fixtures for identity sharing, update/reorder,
remove/reuse, scene replacement, texture fallback, capacity growth and three-frame overlap,
including temporal continuity. Preserve existing scene/material/temporal output under the
roadmap's validation rules. These fixtures are M7.1 acceptance work, not checks passed here.

This decision adds the handoff contract and supersedes no existing ADR. The gate carries forward
all recorded image-parity exceptions and manual/capture follow-ups without broadening or closing
them. It neither starts M7.1 nor waives any of its implementation gates.
