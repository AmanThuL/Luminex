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
MTL_DEBUG_LAYER=1 ./build/macosx/arm64/release/test/Tests "[checkpoint-a]"
```

(The Tests target's `set_default(false)` means `xmake build Tests` is required first — a plain
`xmake` does not relink it. Run from the build directory so shader paths resolve.)

| Area | Case(s) | File |
|---|---|---|
| Upload and layout | "a copy writes buffer bytes into a texture sub-rectangle" | `Tests/GpuCopyTests.cpp` |
| Resource views | "a storage texture view addresses a single mip level" | `Tests/GpuComputeTests.cpp` |
| sRGB | "an sRGB texture is linearised by the sampler, a linear one is not" | `Tests/GpuRhiTests.cpp` |
| Reversed-Z | "a Greater depth test keeps the nearer fragment in reversed-Z" | `Tests/GpuRhiTests.cpp` |
| Storage hazards | "a storage texture written by one dispatch is read by the next"; "a storage texture written by a dispatch is sampled by a later draw" (the exit gate's compute-to-sample case); "a copy captures an intermediate mip level of a GPU-written chain" (the exit gate's per-mip case) | `Tests/GpuComputeTests.cpp`, `Tests/GpuCopyTests.cpp` |
| Load/store behavior | "a depth-only pass stores depth a later pass can sample"; "a transient loaded as an attachment fails to compile" | `Tests/GpuRhiTests.cpp`, `Tests/RenderGraphTests.cpp` |
| Indirect arguments | "an indirect dispatch reads its threadgroup counts from a buffer"; "an indirect draw reads its vertex range from a buffer"; "an indirect indexed draw reads its index range and base vertex from a buffer" | `Tests/GpuIndirectTests.cpp` |
| Frame-slot retirement | "uniform ring survives twelve frames overlapping in flight"; "resizing and toggling transients leaks no heap generation" | `Tests/GpuFrameLifetimeTests.cpp`, `Tests/GpuTransientTests.cpp` |

These tests change only by a superseding ADR. A future backend implementation is not accepted
until it passes `[checkpoint-a]` unchanged — no edited assertion, no relaxed tolerance, no removed
case — because relaxing a frozen case to fit a backend defeats the reason it was frozen. Extending
the tag to a case not listed above, or retagging a listed case to a different one that better
proves the same area, is an implementation detail of test maintenance and does not need a new ADR;
replacing or weakening what a listed case asserts does.

**Known caveat.** The storage-hazard cases verify *correct results with the declared barrier
present*, not that the barrier is necessary: mutating the compute and copy hazard tests' barrier
recording out of the implementation left the compute-hazard cases passing anyway, because this
hardware happened to order the accesses without it, while pre-existing render-target-to-sampled
cases failed under the same mutation. Checkpoint A therefore freezes sufficiency (declaring the
barrier produces the correct result) and does not, and cannot without a fuzzing harness or a
backend that reorders more aggressively, prove necessity. A future backend that is more tolerant of
missing barriers than Metal 4 could pass this suite while still shipping a real hazard; that risk
is accepted rather than solved here.

## Consequences

A future backend's acceptance gate is concrete: build against the same RHI headers, wire up the
new backend, run `Tests "[checkpoint-a]"`, and every case must pass with its assertions unchanged.
Reviewers and CI can check the freeze mechanically instead of re-deriving which tests matter from
the spec each time. The eight areas do not cover everything M5 shipped — bloom, auto-exposure, the
inspector panel, and the render graph's own culling/pooling semantics are validated elsewhere and
are not part of the portability boundary this ADR freezes — because checkpoint A exists to gate a
second backend's execution substrate, not to freeze every M5 behavior.

The sufficiency-not-necessity caveat means passing `[checkpoint-a]` is necessary but not
sufficient evidence that a new backend synchronizes correctly; it remains a documented gap for
whoever designs the M5.1 workload matrix or a future D3D12 backend's validation plan to weigh
alongside Metal's own validation layer results.
