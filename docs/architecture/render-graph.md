# Render Graph

**Status**: Implemented

`Source/Render/Graph` declares and validates one frame's GPU work before any of it reaches the
device, owns the resources it creates for that frame alone, and hands back a deterministic record
of what it decided. [Render passes](render-passes.md) describes `Renderer`, `SceneView` and the
pass families that append their own passes and sinks to the graph this page describes.

## Declaration

A `RenderGraph` is built fresh every frame and owns no GPU state between frames. Callers add
raster, compute, copy and external passes (`addPass`, `addComputePass`, `addCopyPass`,
`addExternalPass`), each declaring the resource versions it reads and writes at per-subresource
granularity, including extra colour attachments. The graph either imports a resource the caller
already owns (`importTexture`, `importBuffer`) or creates a transient that lives for exactly one
frame (`createTexture`, `createBuffer`). Callers root results with sinks (`exportTexture`,
`exportBuffer`, `presentTexture`, `readbackTexture`, `readbackBuffer`), each marking a version as
leaving the frame by export, presentation or readback; sinks are the only roots dead-pass culling
starts from. [ADR 0005](../decisions/0005-render-graph.md) holds the original validating, serial,
declaration-over-versions model this builds on.

## Validation and culling

Before any pass reaches the GPU, the graph proves its declarations form a valid schedule: a read
must have a producing write, a version may not be written twice, and attachment and subresource
rules must hold. It also rejects cycles, and answers one serial schedule in which declaration order
breaks ties between independent passes. A pass that produces nothing, or whose output no sink
reaches directly or through the passes that consume it, is culled from the schedule; a graph dump
names which passes were culled and why, which is the first thing to check when a pass appears to do
nothing (see [GPU debugging](../guides/gpu-debugging.md)).

## Transients and the TransientPool

A transient holds nothing until a pass writes it, and no sink may name one directly. The graph
places lifetime-disjoint transients in the shared bytes of a `TransientPool` placement heap:
aliasing is conservative, so two transients share memory only when their descriptors agree on every
axis that decides layout, and only bytes whose previous occupant's lifetime has already ended are
reused. Offsets are assigned first-fit in lifetime order, tie-broken by declaration order, so the
layout depends only on the declarations. The pool holds one placement heap per frame-in-flight
slot.

The pool retains three paced slots, heap generations and one reservation per frame, reaching the
device through `TransientBackend` (`TransientBackend.h`). `RhiTransientBackend` borrows
`GpuContext` for its real frame counter; tests can supply the device-free fake backend.
Footprints are queried and snapshotted during declaration, including both effective indirect-buffer
variants. Compile and lower use those snapshots without querying a device.

Each reservation carries a canonical `TransientLayout`: heap bytes and every used resource's
identity, offset, effective descriptor, footprint and label in deterministic placement order. A
cache hit requires complete structural equality and a previously completed placement, not matching
bytes or a hash alone. The same slot/layout reuses actual placed textures, buffers and their views.
Changed layouts replace the set; outgoing generations retire on paced boundaries. Sampled and
per-mip storage views and buffer addresses are retained alongside placements. Every transient
still takes first-use discard/dependency handling each frame, including cached alias reuse.
[ADR 0008](../decisions/0008-transient-graph-resources.md) owns the transient-ownership
and aliasing rules; ADR 0005's import-only model still governs resources a caller brings in.

## Compiled frame record and the dump

Compiling a frame produces a `CompiledFrameRecord`: the schedule, the derived barriers, each
transient's lifetime and placement, and the frame's transient memory totals. `CompiledFrameRecord.h`
owns this value-only observer contract independently of the builder that produced it, so an
observer can read or compare a record without the graph that produced it. Records and dumps name
the graph's own use vocabulary (`ResourceUse.h`), converted to the RHI's only where commands are
recorded. `GraphDump.h` renders a record as deterministic text, the form the golden-file tests
compare, the `LMX_GRAPH_DUMP` environment variable writes for the first executed frame, and the
Render Graph window's Dump frame action exports; procedures for reading a graph dump and the
detached Render Graph window live in [GPU debugging](../guides/gpu-debugging.md).

## Lowering and emission

`RenderGraph::lower` produces a `LoweredFrame` (`LoweredFrame.h`) without a device: the logical
record, ordered ops, resource/view/address snapshots and proposed state writebacks. Imported
resources are value refs; transients remain unplaced until execution. `passResources(frame, pass)`
is a pure resolver over that frame's snapshot and the pass's declared handles. `execute()` lowers,
reserves/places transients, fills their cached refs, emits commands, then publishes both state
channels. No table writeback precedes emission.

`RenderGraphLowering.cpp` maps uses to physical endpoints independently of logical transition rows:

| Use | Stage | Access | Texture layout |
| --- | --- | --- | --- |
| Color attachment | ColorAttachment | ColorAttachmentRead \| ColorAttachmentWrite | ColorAttachment |
| Depth attachment | DepthStencil | DepthStencilRead \| DepthStencilWrite | DepthStencilWrite |
| ShaderRead | VertexShader \| FragmentShader for raster; ComputeShader otherwise | ShaderRead | ShaderRead; DepthStencilRead for depth |
| StorageRead / StorageWrite | Same shader stages | ShaderRead / ShaderWrite | ShaderWrite |
| CopySource / CopyDestination | Copy | CopyRead / CopyWrite | CopySrc / CopyDst |
| ExternalRead / ExternalWrite | ComputeShader | ShaderRead / ShaderWrite | ShaderRead / ShaderWrite |
| Buffer IndirectArgument | IndirectArgs | IndirectArgs | — |

Raster shader stages remain conservative. Each physical barrier batch becomes one `barrier` call
with global, buffer and texture arrays. Buffer alias dependencies use global barriers; aliased
textures discard from Undefined with the prior occupant's frontier stages (All fallback) and None
source access. The prior buffer-write dependency remains effective.

A `lmx.frame` scope encloses every frame, including an empty schedule. Raster ops open a labeled
render pass, set viewport/scissor from the render area, invoke the body and end the pass; that
pass owns its automatic timing scope. Compute/copy ops wrap the body in explicit labeled scopes.
External bodies open no graph scope and use their operation's timing. `FrameEmitter.cpp` only
translates ops and invokes the supplied pass runner. Attachment load/store actions remain per
attachment. The existing no-attachment raster lower assertion is a bounded limitation; malformed
graphs are not universally reported as `GraphError`.

Graph declarations do not themselves register shader accesses with the RHI tracker. Kept shader
bodies declare real resources immediately before actual draws/dispatches. Nested frame-arena data
without public handles and opaque adapter texture-ID reads remain outside full declared coverage.

## Persistent texture state

`Renderer` owns a recording-thread-only `TextureStateTable`, keyed by texture handle identity,
and binds it to each graph. It owns no GPU objects and has two independent channels:

- Whole-resource `logicalUse/setLogicalUse` retains the graph's diagnostic vocabulary and unchanged
  `CompiledFrameRecord`/dump hazards.
- Physical mip/layer cells retain exact `{stage, access, layout}` frontiers. `physicalRanges`
  partitions disjoint known cells; `find` succeeds only for complete coverage in one layout,
  combining stage/access masks. A mixed or missing range returns nullopt, never Undefined.

Persistent owner/ref imports snapshot known cells. Missing cells seed from immutable creation
facts, even when a logical-only entry already exists: initial data means ShaderRead, otherwise
Undefined. Existing cells retain their current state. Lowering walks every scheduled access,
including those without a logical transition row, tracks outstanding readers and orders later
writes/layout changes from the complete frontier. A fully culled import without an explicit final
use preserves both channels because no GPU access occurred.

`setFinalUse` keeps its logical override and emits real terminal transitions from exact current
cells, including outstanding readers. This also applies to an otherwise unused import. A Present
sink transitions the acquired drawable from its actual graph frontier to Present; combining
presentation with `setFinalUse` is rejected before emission. These physical ops add no logical dump
rows. The retained Raw temporal `sceneColor → CopySource` override therefore remains in logical
records without discarding the display/bloom reader dependencies.

On successful target replacement, only the deliberate logical use carries forward; the new
identity's physical cells come from its own creation fact. Equal-identity resize retains its full
entry, and failed resize preserves the old targets and all states. Owners forget released
identities. Pending stages retain their conceptual final-use policy until their ports resume.
Readback consumes and restores exact current physical ranges rather than treating creation layout
or a nominal logical use as the current state.

## Implementation units

Declaration, lowering and execution, schedule compilation and transient-lifetime assignment,
transition derivation, validation and emission are separate units (`RenderGraph.h`/`.cpp`,
`RenderGraphCompile.cpp`, `RenderGraphTransitions.cpp`, `RenderGraphValidation.cpp`,
`FrameEmitter.h`/`.cpp`, `TextureStateTable.h`/`.cpp`), over the value types of
`LoweredFrame.h`, `AttachmentOps.h` and `ResourceUse.h`, sharing private range and declaration
helpers through `RenderGraphInternal.h` and `RenderGraphRanges.cpp`. The private transition steps
share one `graph_detail::TransitionState`, which holds each resource's last write and pending
reads, and they retain alias handoffs, read-after-write, write-after-write and write-after-read
hazards, and the order each access was recorded in. Range overlap for subresource tracking uses
[Core](core.md)'s interval container.

## FrameDeclaration

`FrameDeclaration` shares graph construction and execution across the editor and headless
application loops: constructing one rotates the pool's retired transient slot, applies the pooling
policy, and declares the renderer's passes; `execute()` runs the graph exactly once and returns the
accepted `CompiledFrameRecord`, which the caller retains for its own frame-record history.

## Tests

- `Tests/Render/Graph/` covers declaration, validation, scheduling, transitions, transient
  placement and aliasing, external passes, attachment rules, lowering and the deterministic dump,
  across `GpuAppFrameDeclaration`, `GpuRenderGraph`, `GpuTransient`, `GraphDump`, `RenderGraph`,
  `RenderGraphAttachment`, `RenderGraphExecution`, `RenderGraphExternal`, `RenderGraphLowering`,
  `RenderGraphRecord`, `RenderGraphTransient`, `RenderGraphTransition`, `RenderTransientPool` and
  `TextureStateTable`. Pure graph cases use synthetic value imports and the transient backend fake, asserting logical
  records separately from physical masks. Device cases use genuine owners and command lists;
  unchanged dump goldens cover the observer contract.
- `Tests/App/Model/Graph/` covers the editor's graph models built on this contract (frame-record
  retention, node model and layout, snapshots and the graph inspector), described in
  [App](app.md).
