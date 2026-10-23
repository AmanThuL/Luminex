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
`BeginRenderPassOp`, `RunOp` and `EndRenderPassOp`, a compute or copy pass's `BeginScopeOp`, `RunOp`
and `EndScopeOp` under its own label, or an external pass's `RunOp` alone. Attachment load and store
actions live in `AttachmentOps.h`. `execute()` lowers, places the frame's transients, fills their
objects in, dumps the record and hands the frame to `emit()` (`FrameEmitter.h`), the one piece of
graph code that records commands. The v1 emitter records each barrier in order, builds each render
pass from its op and asserts the command list's attachment limits there, opens compute and copy
passes around their bodies from the `RunOp`'s kind and label, and records nothing for scopes, since
that command list times passes itself. Raster passes carry no scope of their own; a backend that
times passes times a raster pass from its `BeginRenderPassOp`/`EndRenderPassOp` pair.

## Persistent texture state

A fresh graph cannot see the passes of an earlier frame, so a texture that outlives a frame carries
the use it was left in through a `TextureStateTable` (`TextureStateTable.h`): one whole-resource
`TextureUse` per texture, keyed by the texture's address, with an absent entry meaning no earlier
access. The table holds no GPU object and is not thread-safe. `Renderer` owns it and binds it to
each frame's graph with `bindTextureStates`; binding the same table again is allowed, a different
one asserts, and stages reach it through `textureStates()`.

`importPersistentTexture` imports a texture with the table's entry as its earlier-frame access, so
the first conflicting access of the frame is barriered like any other; with no entry it is exactly a
plain `importTexture`, which stays the import for a texture with no history, such as the drawable.
Lowering fills `LoweredFrame::finalStates` for the table imports a scheduled pass names or a
`setFinalUse` call states, and `execute()` writes them back to the table after `emit()`. The use
written back is the one the last scheduled pass naming the texture left it in, a write winning over
a read within that pass, mapped by the same role and kind table the transitions use. A texture no
scheduled pass names, its passes culled, keeps its entry, because the frame did nothing to it on the
GPU.

`setFinalUse(handle, use)` states the use when something outside the graph accesses the texture
afterwards (a caller or the UI sampling it), and overrides the derived use; the last call wins. It
adds no barrier and no record entry, so dumps are unchanged: the access it states is the caller's
to order. An owner calls `set` directly only to seed a texture at creation or recreation, which
also keeps a destroyed texture's reused address from handing its use to the new one, or to state
a texture the current graph does not import; `forget` drops an entry. Every texture imported from
the table is seeded (`set`) or forgotten before its first import, and owners call `forget` when
they destroy a texture.

Every persistent texture in production is imported this way; only the drawable stays a plain
import. `Renderer` seeds its targets in `create()` and `resize()` (a replaced target takes its
predecessor's entry, the temporal slots restart as `CopyDestination` colour and `ShaderRead`
depth), the HZB stage forgets the pyramids a resize replaces and an unbuilt pyramid's entry before
importing it, and the selection outline forgets the target a resize replaces and seeds the new one
at its next import. The deliberate `setFinalUse` states are the
display target, the frame's depth slot outside vendor frames and the outline target (all
`ShaderRead`, sampled after the graph), a vendor frame's colour slot (`ExternalWrite`), every HZB
pyramid (`ShaderRead`), and the scene colour on a Raw temporal frame at the output extent
(`CopySource`). That last one is the only override that states no access after the graph: it keeps
the earlier hand-tracked `CopySource` state solely so the gpu golden dump stays unchanged. The state
is likely under-synchronised, because the derived last use is `ShaderRead` (the commit copy is
scheduled before the bloom and display reads), so the next frame's first barrier does not wait on
those reads. Correcting it changes that golden, so it stays a deliberate, open decision.

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
  `TextureStateTable`. Graph cases run on the transient backend fake and assert ordering on the
  lowered frame; only the emitter cases record on a command list, and imports borrow
  `FakeTexture` and `FakeBuffer`.
- `Tests/App/Model/Graph/` covers the editor's graph models built on this contract (frame-record
  retention, node model and layout, snapshots and the graph inspector), described in
  [App](app.md).
