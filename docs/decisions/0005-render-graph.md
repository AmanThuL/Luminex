# ADR 0005: Render graph — validating, serial, import-only

**Status**: Accepted (2026-08-10) · **Roadmap**: ../roadmap.md (M4)

## Context
M3's frame was hand-sequenced: shadow, scene+sky, and a manual barrier, correct only by inspection.
Adding a display-transform pass and letting the App join its own UI pass is exactly where an
undeclared cross-pass dependency turns into a silent GPU hazard instead of a caught one.

## Decision
`lmx::render::RenderGraph` models one frame's passes as declarations over versioned logical
handles (`GraphTexture`/`GraphBuffer`) instead of encoded commands. Resources are imported only —
a caller brings in a texture or buffer it already owns; the graph creates, pools, and aliases
nothing. Each pass declares the versions it reads, at most one color and one depth attachment, and
any other writes. `compile()` proves the declarations form a DAG (read-before-write, double
writes, cycles, and attachment mismatches all hard-fail) and answers one serial schedule.
`execute()` re-validates, runs it, and derives exactly the render-target-to-sampled barriers the
declared reads justify; a pass resolving an undeclared handle is refused, not handed one. A graph
is declared fresh every frame and owns no GPU state between frames.

## Consequences
Transient pooling, dead-pass culling, and schedule optimization are deliberately absent until M5's
execution substrate gives the graph something to optimize; it is a validation and ordering layer
over caller-managed resources, not an allocator. The serial schedule is the right tradeoff at four
passes a frame. A pass must declare an attachment — compute dispatch, and graphs with none, arrive
with M5.
