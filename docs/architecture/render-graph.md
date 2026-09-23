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
slot. [ADR 0008](../decisions/0008-transient-graph-resources.md) owns the transient-ownership
and aliasing rules; ADR 0005's import-only model still governs resources a caller brings in.

## Compiled frame record and the dump

Compiling a frame produces a `CompiledFrameRecord`: the schedule, the derived barriers, each
transient's lifetime and placement, and the frame's transient memory totals. `CompiledFrameRecord.h`
owns this value-only observer contract independently of the builder that produced it, so an
observer can read or compare a record without the graph that produced it. `GraphDump.h` renders a
record as deterministic text, the form the golden-file tests compare, the `LMX_GRAPH_DUMP`
environment variable writes for the first executed frame, and the Render Graph window's Dump frame
action exports; procedures for reading a graph dump and the detached Render Graph window live in
[GPU debugging](../guides/gpu-debugging.md).

## Implementation units

Declaration and execution, schedule compilation and transient-lifetime assignment, transition
derivation, and validation are separate units (`RenderGraph.h`/`.cpp`, `RenderGraphCompile.cpp`,
`RenderGraphTransitions.cpp`, `RenderGraphValidation.cpp`), sharing private range and declaration
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
  placement and aliasing, external passes, attachment rules and the deterministic dump, across
  `GpuAppFrameDeclaration`, `GpuRenderGraph`, `GpuTransient`, `GraphDump`, `RenderGraph`,
  `RenderGraphAttachment`, `RenderGraphExecution`, `RenderGraphExternal`, `RenderGraphRecord`,
  `RenderGraphTransient`, `RenderGraphTransition` and `RenderTransientPool`.
- `Tests/App/Model/Graph/` covers the editor's graph models built on this contract (frame-record
  retention, node model and layout, snapshots and the graph inspector), described in
  [App](app.md).
