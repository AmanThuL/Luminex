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

The pool keeps that policy (slots, heap generations and their retirement, one reservation per
frame, heap labels) and reaches the device only through `TransientBackend`
(`TransientBackend.h`): footprints for a `TransientTextureDesc` or `TransientBufferDesc`
(`TransientResourceDesc.h`), heap creation and destruction, placement, and releasing a heap's
placements when its slot comes round. The backend owns the heaps and placed resources, named to
the pool by `TransientHeapId`. `RhiTransientBackend` is the implementation over a `rojoRHI::Device`
and the one a pool constructed from a device owns; it converts each descriptor to the RHI's with
the graph resource's name as its label and no CPU readback. Graph and pool unit tests construct
the pool over the device-free `FakeTransientBackend`
(`Tests/Support/TransientBackendTestSupport.h`).
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

`RenderGraph::lower` turns a compiled frame into a `LoweredFrame` (`LoweredFrame.h`) without a
device: the record, an ordered list of ops, and the device objects behind each resource index, with
transients left unplaced. `BeginScopeOp{"lmx.frame"}` and its `EndScopeOp` enclose every frame, an
empty schedule included. Each scheduled pass then contributes one `BarriersOp` holding its texture
and buffer transitions in record order (none when it needs none), followed by a raster pass's
`BeginRenderPassOp`, `RunOp` and `EndRenderPassOp`, a compute or copy pass's `BeginScopeOp`,
`RunOp` and `EndScopeOp` under its own label, or an external pass's `RunOp` alone. Attachment load
and store actions live in `AttachmentOps.h`. `execute()` lowers, places the frame's transients,
fills their objects in, dumps the record and hands the frame to `emit()` (`FrameEmitter.h`), the one
piece of graph code that records commands. The v1 emitter records each barrier in order, builds
each render pass from its op and asserts the command list's attachment limits there, opens compute
and copy passes around their bodies from the `RunOp`'s kind and label, and records nothing for
scopes, since that command list times passes itself. Raster passes carry no scope of their own; a backend
that times passes times a raster pass from its `BeginRenderPassOp`/`EndRenderPassOp` pair.

## Implementation units

Declaration, lowering and execution, schedule compilation and transient-lifetime assignment,
transition derivation, validation and emission are separate units (`RenderGraph.h`/`.cpp`,
`RenderGraphCompile.cpp`, `RenderGraphTransitions.cpp`, `RenderGraphValidation.cpp`,
`FrameEmitter.h`/`.cpp`), over the value types of `LoweredFrame.h`, `AttachmentOps.h` and
`ResourceUse.h`, sharing private range and declaration
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
  `RenderGraphRecord`, `RenderGraphTransient`, `RenderGraphTransition` and `RenderTransientPool`.
- `Tests/App/Model/Graph/` covers the editor's graph models built on this contract (frame-record
  retention, node model and layout, snapshots and the graph inspector), described in
  [App](app.md).
