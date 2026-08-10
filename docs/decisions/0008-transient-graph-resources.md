# ADR 0008: The render graph owns transient resources

**Status**: Accepted (2026-08-11) · **Roadmap**: ../roadmap.md (M5)

## Context
ADR 0005 made the render graph import-only: every resource belonged to a caller, and the graph
created, pooled, and aliased nothing. That was right while a frame was four raster passes over
targets the renderer already held for their own reasons. It stops being right as soon as a feature
needs scratch memory that exists for part of one frame and for nothing else — a bloom chain, a
histogram's bins — because the only ways to express that under an import-only rule are to make the
caller hold a permanent allocation for a temporary result, or to hide the allocation behind the
graph without the graph knowing its lifetime.

The graph already knows every lifetime exactly: a resource's live range is the span of the schedule
between the first and last surviving pass that names it, and dead-pass culling has already decided
which passes those are.

## Decision
`lmx::render::RenderGraph` gains `createTexture`/`createBuffer`, declaring a resource the graph owns
for exactly one frame. A frame's resources are therefore of two kinds, and the difference is
ownership rather than convenience:

- **Imported** resources belong to the caller. They persist between frames, they carry contents at
  version 0, they can be exported, presented, or read back, and they are never pooled or aliased.
- **Transient** resources belong to the graph. They live one frame, they hold nothing until a pass
  writes them — consuming version 0 is a compilation failure — and no sink may name one. They are
  placed in a `TransientPool`'s placement heap, and two whose lifetimes do not overlap may share
  bytes.

Aliasing is conservative. Two transients share memory only when their descriptors agree on every
axis that decides a layout: resource kind, format, extent, mip count, usage, and the size and
alignment the RHI reports for them. Storage mode is not compared because it is not a variable — a
transient can be neither uploaded to nor read back, so it is always device-private. Offsets are
assigned first-fit in lifetime order, tie-broken by declaration order, which makes a frame's whole
layout a function of its declarations. Where one transient takes bytes another held, the graph emits
a whole-resource barrier before the new one's first pass, from the previous occupant's last use to
this one's first: the two sides are different logical resources, so the version chain cannot order
them and the heap tracks no hazards of its own.

Physical memory is one placement heap per frame-in-flight slot, reused only after the RHI's pacing
has proved that slot's previous frame retired. A frame whose transient footprint differs from what
its slot holds gets a new heap generation; the outgoing one is released only once the last frame
that could still be reading it has retired.

Pooling has a runtime toggle. Off gives every transient its own bytes and costs the frame its alias
savings; it cannot change the picture, because nothing can observe a transient before its first
write. Compilation records every lifetime, every assignment, the heap high-water mark, and the alias
savings in `CompiledFrameDebug`, so the plan is visible in the deterministic dump and to the editor.

This supersedes only ADR 0005's import-only rule. Its validating, serial, declaration-over-versions
model — read-before-write, double writes, cycles, attachment rules, one schedule — stands unchanged,
and import remains the only way to bring in a resource somebody else owns.

## Consequences
A feature needing scratch memory declares it and stops thinking about it: the frame pays for the
high-water mark of its live set rather than for the sum of its declarations, and a toggled-off
feature's memory costs nothing because its passes were culled. The cost is that the graph now owns
GPU memory and needs a pool to place it in, so a caller that declares transients supplies one and
keeps it alive across frames.

Conservative compatibility means the first aliasing consumers see reuse only between identically
shaped resources — exactly the ping-pong and chain-scratch cases — and nothing else. Relaxing it to
place differing layouts in shared bytes is a later decision that needs its own evidence about what a
heap actually promises; the plan, the barriers, and the accounting are already shaped for it.
