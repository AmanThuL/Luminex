# ADR 0009: Portability checkpoint A freezes eight semantic areas behind one test tag

**Status**: Accepted (2026-08-11) · **Roadmap**: ../roadmap.md (M5)

## Context

M5 gave the RHI and render graph a compute/copy/barrier execution substrate: storage buffers and
textures, subresource views, general copies, indirect arguments, transient ownership and pooling,
and a deterministic frame record. The roadmap names portability checkpoint A as the point where a
fixed set of semantic areas gets frozen for any future backend to reproduce, ahead of the M5.1
interface experiment and the eventual D3D12 backend (ADR 0007). Freezing needs a mechanism a CI
job or a reviewer can run as one command, not a prose promise that source can drift away from.

Conformance-style tests for these areas already exist from shipping the substrate. The freeze does
not need new tests; it needs to name the tests that already prove each area and make it possible to
run exactly that set, unchanged, against a second backend later.

## Decision

Portability checkpoint A covers eight semantic areas: upload and layout, resource views, sRGB,
reversed-Z, storage hazards, load/store behavior, indirect arguments, and frame-slot retirement.
Each area's strongest existing Catch2 case (or cases) carries the tag `[checkpoint-a]` alongside
its existing tags. The frozen suite is exactly `[checkpoint-a]`, runnable as one filter:

```bash
xmake build Tests
cd build/macosx/arm64/release/test
MTL_DEBUG_LAYER=1 ./Tests "[checkpoint-a]"
```

The Tests target's `set_default(false)` means `xmake build Tests` is required first — a plain
`xmake` does not relink it. The binary must run with its build directory as the working directory,
because the Metal 4 backend resolves shader paths relative to the current directory and the
compiled shaders exist only in that tree.

Most listed cases run against a real GPU device (tag `[gpu]`) and a second backend must pass them
on its own hardware. One is a CPU-only graph-contract case (tag `[render][graph]`, no GPU device)
that exercises `RenderGraph` compilation against a mock device; it stays in the frozen set because
its assertion is part of what checkpoint A promises, but a backend implementation has nothing
backend-specific to prove against it — the graph layer above any backend already covers it
identically.

| Area | Level | Case | File |
|---|---|---|---|
| Upload and layout | GPU | "a copy writes buffer bytes into a texture sub-rectangle" | `Tests/GpuCopyTests.cpp` |
| Upload and layout | GPU | "a BC1 block decodes to its endpoint colour when sampled" (compressed-block layout; CPU-initial-data row layout via `TextureMip::bytesPerRow`) | `Tests/GpuRhiTests.cpp` |
| Upload and layout | GPU | "a cubemap samples the face its direction points at" (face-major cubemap upload, six distinct `TextureMip` entries) | `Tests/GpuRhiTests.cpp` |
| Resource views | GPU | "a storage texture view addresses a single mip level" | `Tests/GpuComputeTests.cpp` |
| Resource views | GPU | "a copy captures one array layer and mip of a cubemap" (layer/face addressing) | `Tests/GpuCopyTests.cpp` |
| sRGB | GPU | "an sRGB texture is linearised by the sampler, a linear one is not" | `Tests/GpuRhiTests.cpp` |
| Reversed-Z | GPU | "a Greater depth test keeps the nearer fragment in reversed-Z" | `Tests/GpuRhiTests.cpp` |
| Storage hazards | GPU | "a storage texture written by one dispatch is read by the next" | `Tests/GpuComputeTests.cpp` |
| Storage hazards | GPU | "a storage texture written by a dispatch is sampled by a later draw" (the exit gate's compute-to-sample case) | `Tests/GpuComputeTests.cpp` |
| Storage hazards | GPU | "a copy captures an intermediate mip level of a GPU-written chain" (the exit gate's per-mip case) | `Tests/GpuCopyTests.cpp` |
| Load/store behavior | CPU (graph contract) | "a transient loaded as an attachment fails to compile" | `Tests/RenderGraphTests.cpp` |
| Load/store behavior | GPU | "a depth-only pass stores depth a later pass can sample" | `Tests/GpuRhiTests.cpp` |
| Load/store behavior | GPU | "a transient cannot read what the transient it replaced left behind" | `Tests/GpuTransientTests.cpp` |
| Indirect arguments | GPU | "an indirect dispatch reads its threadgroup counts from a buffer" | `Tests/GpuIndirectTests.cpp` |
| Indirect arguments | GPU | "an indirect draw reads its vertex range from a buffer" | `Tests/GpuIndirectTests.cpp` |
| Indirect arguments | GPU | "an indirect indexed draw reads its index range and base vertex from a buffer" | `Tests/GpuIndirectTests.cpp` |
| Frame-slot retirement | GPU | "uniform ring survives twelve frames overlapping in flight" | `Tests/GpuFrameLifetimeTests.cpp` |
| Frame-slot retirement | GPU | "resizing and toggling transients leaks no heap generation" | `Tests/GpuTransientTests.cpp` |

Adding `[checkpoint-a]` to a case not listed above needs no ADR: it only adds evidence for an
area already frozen and changes nothing the table above asserts. Removing a listed case's tag, or
retagging it to a different case — even one judged to prove the same area better — requires a
superseding ADR, because either edits the case table above, and this table is not correctable once
this ADR is Accepted except by supersession. A future backend implementation is not accepted until
it passes every GPU-level case in `[checkpoint-a]` unchanged — no edited assertion, no relaxed
tolerance — because relaxing a frozen case to fit a backend defeats the reason it was frozen.

**Known caveat: barrier sufficiency, not necessity.** The storage-hazard cases verify *correct
results with the declared barrier present*; they do not prove the barrier is necessary. Mutating
the compute and copy hazard tests' barrier recording out of the implementation left the
compute-hazard cases passing anyway, because this hardware happened to order the accesses without
it, while pre-existing render-target-to-sampled cases failed under the same mutation. Checkpoint A
therefore freezes sufficiency and cannot, without a fuzzing harness or a backend that reorders more
aggressively than Metal 4, prove necessity. A future backend more tolerant of missing barriers
could pass this suite while still shipping a real hazard; that risk is accepted rather than solved
here.

## Consequences

A future backend's acceptance gate is concrete: build against the same RHI headers, wire up the
new backend, run `Tests "[checkpoint-a]"` from its build directory, and every GPU-level case must
pass with its assertions unchanged. Reviewers and CI can check the freeze mechanically instead of
re-deriving which tests matter from the spec each time. The eight areas do not cover everything M5
shipped — bloom, auto-exposure, the inspector panel, and the render graph's own culling/pooling
semantics are validated elsewhere and are not part of the portability boundary this ADR freezes —
because checkpoint A exists to gate a second backend's execution substrate, not to freeze every M5
behavior.

The barrier-sufficiency caveat above means a green `[checkpoint-a]` run is evidence of correct
results under the barriers as declared, not proof that a new backend's synchronization is complete;
it remains a documented gap for whoever designs the M5.1 workload matrix or a future D3D12
backend's validation plan to weigh alongside Metal's own validation layer results.
