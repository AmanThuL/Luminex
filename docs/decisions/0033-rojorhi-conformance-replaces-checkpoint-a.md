# ADR 0033: RojoRHI's conformance suite replaces portability checkpoint A

**Status**: Proposed · **Roadmap**: [R5](../roadmap/codebase-rojorhi-port.md#r5--luminex-runs-on-the-rojorhi-v2-interface) ·
**Record**: [RojoRHI v2 port](../milestones/r/r5.md)

## Context

[ADR 0009](0009-checkpoint-a-conformance.md) froze eight semantic areas behind the
`[checkpoint-a]` tag: upload and layout, resource views, sRGB, reversed-Z, storage hazards,
load/store behavior, indirect arguments and frame-slot retirement. A future backend is accepted only
when it passes every GPU case of that set unchanged.
[R2](../roadmap/codebase-restructuring.md#r2--rhi-becomes-rojorhi) split the set under
[ADR 0024](0024-rhi-relocation-to-rojorhi.md): 16 RHI cases now live in RojoRHI's frozen v1 tests,
3 transient and graph cases stay in Luminex, and `Tools/check_checkpoint_a.py` checks that the union
of both filters equals `Tests/checkpoint-a.inventory` by case name.

The 16 RHI cases exercise RojoRHI's v1 interface, which RojoRHI deletes once R5 moves Luminex to
v2; they cannot outlive it. RojoRHI's own
[ADR 0009](https://github.com/AmanThuL/rojo-rhi/blob/v0.1.0/docs/decisions/0009-conformance-and-valid-usage.md)
defines tier T1 conformance, the `[conformance]` cases, as the acceptance gate for any RojoRHI
backend on hardware: a backend is accepted when it passes every T1 case unchanged under the platform
validation layer. It replaced RojoRHI's copy of checkpoint A and did not carry the v1 case names
forward. Metal 4 passes T1 on Apple Silicon.

## Decision

At R5's integration Luminex retires `Tools/check_checkpoint_a.py`, `Tests/checkpoint-a.inventory`,
the `[checkpoint-a]` tag and its CI step. The role of the 16 RHI cases passes to RojoRHI's T1 suite.
The 3 Luminex cases ("a transient cannot read what the transient it replaced left behind", "a
transient loaded as an attachment fails to compile" and "resizing and toggling transients leaks no
heap generation") stay as ordinary GPU and graph cases under their names.

Each area maps to the RojoRHI T1 files that cover it at `v0.1.0`:

| Area | RojoRHI `[conformance]` coverage | Gap |
|---|---|---|
| Upload and layout | [Copy](https://github.com/AmanThuL/rojo-rhi/blob/v0.1.0/Tests/Conformance/CopyConformanceTests.cpp) (buffer-to-texture regions with row pitch and offset, layers, cube faces, slices, BC1 blocks); [InitialData](https://github.com/AmanThuL/rojo-rhi/blob/v0.1.0/Tests/Conformance/InitialDataConformanceTests.cpp) (every mip and layer, cube faces); [AbiCompute](https://github.com/AmanThuL/rojo-rhi/blob/v0.1.0/Tests/Conformance/AbiComputeConformanceTests.cpp) (BC1 sampling, the cube face a direction selects) | none |
| Resource views | [AbiCompute](https://github.com/AmanThuL/rojo-rhi/blob/v0.1.0/Tests/Conformance/AbiComputeConformanceTests.cpp) (layer views of 1D, 2D and cube arrays, format and depth-aspect views); [WriteOnly](https://github.com/AmanThuL/rojo-rhi/blob/v0.1.0/Tests/Conformance/WriteOnlyConformanceTests.cpp) (storage views); [RenderPass](https://github.com/AmanThuL/rojo-rhi/blob/v0.1.0/Tests/Conformance/RenderPassConformanceTests.cpp) (an attachment's mip and layer, a cube face as a layer) | no case samples or stores through a view of one mip of a multi-mip texture |
| sRGB | [AbiCompute](https://github.com/AmanThuL/rojo-rhi/blob/v0.1.0/Tests/Conformance/AbiComputeConformanceTests.cpp) (linear and BC1 textures through sRGB views and the reverse); [Copy](https://github.com/AmanThuL/rojo-rhi/blob/v0.1.0/Tests/Conformance/CopyConformanceTests.cpp) (sRGB to linear partner bit for bit) | none |
| Reversed-Z | [Draw](https://github.com/AmanThuL/rojo-rhi/blob/v0.1.0/Tests/Conformance/DrawConformanceTests.cpp) and [Format](https://github.com/AmanThuL/rojo-rhi/blob/v0.1.0/Tests/Conformance/FormatConformanceTests.cpp) write and keep D32Float depth | no case runs a Greater depth test that keeps the nearer fragment |
| Storage hazards | [GraphicsBarrier](https://github.com/AmanThuL/rojo-rhi/blob/v0.1.0/Tests/Conformance/GraphicsBarrierConformanceTests.cpp) (compute buffer and record writes read by draws, attachments read by later passes and dispatches); [Barrier](https://github.com/AmanThuL/rojo-rhi/blob/v0.1.0/Tests/Conformance/BarrierConformanceTests.cpp) and [Fill](https://github.com/AmanThuL/rojo-rhi/blob/v0.1.0/Tests/Conformance/FillConformanceTests.cpp) (copies and kernel fills ordered by barriers) | no case orders a storage texture written by one dispatch before a later dispatch or draw, or reads a GPU-written mip chain |
| Load/store behavior | [RenderPass](https://github.com/AmanThuL/rojo-rhi/blob/v0.1.0/Tests/Conformance/RenderPassConformanceTests.cpp) (Clear, Load and Store per attachment); [Draw](https://github.com/AmanThuL/rojo-rhi/blob/v0.1.0/Tests/Conformance/DrawConformanceTests.cpp) (a DontCare load); [GraphicsBarrier](https://github.com/AmanThuL/rojo-rhi/blob/v0.1.0/Tests/Conformance/GraphicsBarrierConformanceTests.cpp) (a depth attachment sampled after its pass) | none; the transient cases stay in Luminex |
| Indirect arguments | [Indirect](https://github.com/AmanThuL/rojo-rhi/blob/v0.1.0/Tests/Conformance/IndirectConformanceTests.cpp) (direct, indexed and GPU-written counted draws); [Dispatch](https://github.com/AmanThuL/rojo-rhi/blob/v0.1.0/Tests/Conformance/DispatchConformanceTests.cpp) (`dispatchIndirect` from a GPU-written record) | none; v2's vertex index excludes the base vertex, which Luminex never sets |
| Frame-slot retirement | [Frame](https://github.com/AmanThuL/rojo-rhi/blob/v0.1.0/Tests/Conformance/FrameConformanceTests.cpp) (pacing at two and three frames in flight, deferred destruction); [Handoff](https://github.com/AmanThuL/rojo-rhi/blob/v0.1.0/Tests/Conformance/HandoffConformanceTests.cpp) (other queues' work joined before a frame retires) | no T1 case keeps per-frame uploads intact across many overlapping frames; RojoRHI's Null contract tier covers uploads across slot reuse |

The gaps are RojoRHI's to close, tracked in
[rojo-rhi issue 133](https://github.com/AmanThuL/rojo-rhi/issues/133); they do not block this
decision, because T1 is RojoRHI's gate and Luminex keeps its own GPU suite over the same behavior.

Once accepted, at R5's integration, this ADR supersedes ADR 0009, and ADR 0009's status line then
reads "Superseded by" a link to this ADR, changed in that integration. ADR 0009 is unchanged until
then.

## Consequences

Luminex stops promising itself a backend acceptance gate: it no longer freezes RHI case names, and
no Luminex check proves a backend portable. That guarantee lives wholly in RojoRHI, where a backend
is accepted only by passing T1 unchanged on hardware. ADR 0009's caveat, that the storage-hazard
cases prove barrier sufficiency and not necessity, becomes RojoRHI's concern: its validation tracker
reports a missing or insufficient barrier for every access it observes, identically on every
backend, and R5 adds a Debug frame smoke that runs Luminex's renderer under that tracker. Luminex keeps its
own image parity gates and GPU suites; a RojoRHI pin bump still meets them.
