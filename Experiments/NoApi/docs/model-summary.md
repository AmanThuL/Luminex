# "No Graphics API" — model summary for the M5.1 prototype

**Status**: Frozen — non-normative
**Date**: 2026-08-12

What the comparison model prescribes, and what it deliberately leaves out, for each of the six areas
the M5.1 spec names. This is the contract `Experiments/NoApi/Include/NoApi/` implements; where the
prototype departs from it, the departure is recorded in
`docs/research/2026-08-12-execution-model-evidence.md`, never resolved silently here.

Sources, cited inline as `[A §Section]`, `[S n]`, `[T mm:ss]`:

- **[A]** Sebastian Aaltonen, *No Graphics API*, blog post, 2025-12-16.
- **[S]** *Reducing graphics API complexity*, SIGGRAPH 2026 Moving Mobile Graphics slides, 34 pages;
  `n` is the PDF page number.
- **[T]** Extended presentation transcript, 2026-08-08, 01:43:38; timestamps as displayed.

An omission is listed only where the model had the opportunity to specify and chose not to. Topics
the model explicitly defers to a follow-up — the 16-entry-point shader framework, ray tracing,
shader execution reordering, wave spawning — are out of scope for all six areas
[A §Graphics shaders] [S 34] [T 1:37:52].

## 1. Memory, addresses, and root data

**Prescribes.** Allocation is `gpuMalloc(size, alignment, memoryKind)` returning a pointer, freed by
`gpuFree`; there is no buffer object and no resource-specific alignment query for buffers
[A §Modern GPU memory management] [A §Prototype API]. Three memory kinds only: CPU-mapped GPU
memory as the default, GPU-private for textures and large GPU-only data, and CPU-cached for
readback [A §Modern GPU memory management]. Every allocation carries two addresses — a mapped host
address for CPU writes and a GPU address the GPU dereferences — translated once per allocation and
cached in a userland struct rather than per use [A §Modern GPU memory management] [S 13]
[T 41:04–41:54]. Only GPU addresses may be stored in GPU-visible data. Pointer arithmetic on GPU
addresses replaces the buffer-plus-offset pair [A §Appendix]. Default alignment is 16 bytes, stated
explicitly by the caller rather than queried, so the shader compiler can emit wide aligned loads
[A §Modern data].

Root data is exactly one 64-bit GPU address per shader stage, cast by the entry point to a
caller-defined `struct` shared verbatim between CPU and GPU [A §Root arguments] [S 14] [S 15]
[T 43:29–45:23]. The struct is `const` and no-alias, which preserves uniform-register preloading and
scalarization [A §Root arguments] [S 16]. It has no size limit, and callers order hot fields first
because preload capacity varies [A §Root arguments]. Nested data — vertex arrays, instance arrays,
material tables, index data, indirect arguments — is reached by embedding further GPU addresses in
the same struct [A §Graphics shader bindings]. Root blocks are bump-allocated per use from a linear
allocator over CPU-mapped GPU memory; the presentation ties that allocator to the command buffer
itself, comparing it to `vkCmdPushDataEXT` and Metal `setBytes` [A §Appendix] [S 13] [S 16]
[T 39:59–40:55]. Specialization constants are a second, pipeline-time struct that may itself contain
GPU addresses, letting a pipeline bake a runtime memory location [A §Static constants] [S 17].

**Omits.** No heap-type enumeration or memory-budget query. No alignment query for buffer-shaped
data. No statement of the linear allocator's lifetime rule: the reference implementation wraps
silently on overflow and is never coupled to a frame fence [A §Appendix]. No thread-safety
statement for allocation or for the allocator. No defragmentation, suballocation policy, or
allocation-count accounting. No rule for what happens to outstanding GPU addresses when their
allocation is freed, beyond the observation that the result is a page fault [A §Tooling].

## 2. Handles and bindings

**Prescribes.** One flat, user-owned descriptor heap holding homogeneous 256-bit texture
descriptors, allocated as ordinary GPU memory and written directly by the CPU or by a compute shader
through a thin descriptor-creation helper [A §Texture bindings] [S 18] [S 19] [T 56:11–1:01:43].
Shaders reference a texture by a 32-bit index into that heap; a single index also names the start of
a contiguous range, which is how a material's texture set is addressed with four bytes
[A §Texture bindings] [S 22]. The active heap is set once per command buffer
(`gpuSetActiveTextureHeapPtr`) [A §Prototype API]. Non-uniform indices are marked with
`NonUniformResourceIndex`; buffer addresses never need it, because a 64-bit pointer is passed per
lane [A §Texture bindings]. Samplers are declared inline in shader code, Metal-style, and are not
CPU-side bound objects [A §Texture bindings]. Texture upload remains a copy command into private
memory, because swizzle order and delta color compression are hardware-specific
[A §Modern GPU memory management] [S 20] [T 1:02:20–1:02:43]. Writing the heap from the GPU requires
a barrier carrying the descriptor hazard flag [A §Texture bindings]. There are no descriptor sets,
descriptor tables, root signatures, bind groups, or per-draw binding calls of any kind
[A §Shader pipelines] [S 14].

**Omits.** No slot allocation or lifetime policy — a freelist appears only as an implementation note
for translating Metal's driver-managed heap [A §Translation layers]. No handle validity, generation,
or bounds checking; reading a stale slot is undefined. No CPU-side sampler object, and therefore no
account of targets whose sampler state must exist as an object. No statement of how many heaps may
be live or how a heap is resized. No capability query for descriptor size or heap capacity: 256 bits
is asserted as the de facto width, with Apple noted as 192 + 32 [A §Texture bindings].

## 3. Pipelines and commands

**Prescribes.** Pipeline creation takes shader intermediate code plus, for graphics, a small raster
descriptor — and nothing else: no binding layout, no vertex layout, no root signature
[A §Shader pipelines] [S 21]. The baked raster state is only what changes generated microcode:
topology, sample count, alpha-to-coverage, dual-source-blend support, attachment formats, per-target
write mask, and optionally an embedded blend equation [A §Rasterizer state]. Depth-stencil state is
a separate object with its own set command [A §Rasterizer state] [S 28] [T 1:22:46]. Viewport,
scissor, cull mode, and winding are always dynamic and can never be baked [S 30]. Blend state may be
separate where the hardware has a blender, embedded otherwise, with mobile framebuffer fetch as the
third option — the model refuses to abstract that difference away [A §Rasterizer state] [S 29].
Vertex buffers do not exist; vertices are struct arrays read through a root pointer, so vertex
layout never causes a pipeline permutation [A §Graphics shader bindings] [S 21]. The index buffer
survives as one more GPU address on the draw call, justified by index-deduplication hardware
[A §Graphics shader bindings]. Command buffers are transient: recorded, submitted, and gone; there
are no persistent or reusable command buffers [A §Command buffers]. Draws take two root addresses,
one per shader stage, so sharing and separating stage data both cost the same
[A §Rasterizer state]. Indirect draws and dispatches take a GPU address for arguments, and — unlike
every shipping API — also allow the root data itself to be GPU-generated, including multi-draw with
a per-draw root stride [A §Indirect drawing] [S 23].

**Omits.** No shader intermediate format is defined, and no compilation-failure reporting mechanism
is described. No pipeline cache, serialization, or archive API. No multi-threaded or deferred
recording, no secondary command buffers, no bundles. No swapchain, surface, or present operation
anywhere in the prototype API [A §Prototype API]. Indirect shader selection is named as a real
limitation and left unsolved [A §Indirect drawing].

## 4. Attachments and synchronization

**Prescribes.** A render pass names its attachments, load actions, store actions, and clear values
at its boundary; the pass object is transient, matching dynamic rendering [A §Render passes].
Attachment binding needs a CPU-side texture object, because no current rasterizer is bindless
[A §Texture bindings] [A §Render passes]. Pass begin and end emit **no** barriers, so disjoint
passes may overlap and a depth prepass pays for no flush it does not need [A §Render passes].

Barriers name a producer stage mask and a consumer stage mask and no resources at all
[A §Barriers and fences] [S 24] [S 25] [T 1:08:20–1:16:07]. Texture layouts do not exist. A small
hazard bitfield covers the caches hardware does not flush automatically: descriptor caches, indirect
argument prefetch, and depth caches [A §Barriers and fences] [S 25]. Split barriers are a
signal-after/wait-before pair operating on a plain GPU memory counter with atomic-set, atomic-max,
or atomic-or semantics and an optional wait mask — no synchronization object exists
[A §Barriers and fences] [S 26] [T 1:16:15–1:18:09]. GPU-to-CPU pacing uses one timeline semaphore
with a monotonically increasing counter, waited on to keep N frames in flight
[A §Barriers and fences].

**Omits.** No multi-queue or cross-queue synchronization, and no queue-family concept. No MSAA
resolve store action. No presentation synchronization of any kind. No validation rule for stage
masks, and no statement of what an insufficient barrier produces beyond a wrong result. The
frames-in-flight example never connects the semaphore to reuse of the bump allocator's storage, so
the model states the pacing primitive without stating the lifetime rule it exists to enforce
[A §Barriers and fences] [A §Appendix].

## 5. Residency and capabilities

**Prescribes.** Nothing for residency. Memory is allocated, mapped, and addressable; nothing must be
made resident, and no working set is declared. Metal 4's residency set is mentioned once, approvingly
and only as evidence that Apple's hardware clears the model's bar [A §Min spec hardware].

For capabilities, the model substitutes a minimum-specification list — Turing, RDNA 2, Xe1,
Apple M1/A14, Mali-G710, Adreno 650, PowerVR DXT — and asserts that everything the API needs is
present on all of them [A §Min spec hardware] [S 31] [T 1:37:06]. Only two runtime capability
notions appear, both as bare `needs feature flag` comments on the separate-blend-state path
[A §Rasterizer state].

**Omits.** Any residency, working-set, or eviction concept. Any capability or limit query API:
descriptor width, heap capacity, root preload size, alignment minima, and dynamic-state support are
all asserted as universal rather than queried. Any notion of a target that meets some requirements
and not others — the model's answer to a target below min spec is that it is out of scope.

## 6. Debugging and failure behavior

**Prescribes.** Debuggability comes from the shading language, not from the API: with C/C++ pointer
semantics and debug symbols, a debugger follows pointer chains and shows struct layouts exactly as
on the CPU, and the descriptor heap is indexable memory whose entries a debugger can resolve to a
visualized texture — the model cites Xcode's Metal debugger as already doing this
[A §Tooling] [T 1:32:46]. Capture and replay require the replayer to reproduce identical GPU virtual
addresses, and the model asks for that to be a public API rather than a vendor-internal one so that
open-source tools can implement it [A §Tooling]. Out-of-bounds and stale-pointer access are ordinary
page faults, unchanged in kind from what today's storage buffers already permit; robustness, where
wanted, is a caller-side pointer-plus-size discipline and a compiler-inserted clamp, as WebGPU does
[A §Tooling].

**Omits.** Everything the M5.1 gates require of failure behavior. There are no debug labels on any
object in the prototype API. There is no error return, result type, or status code on any entry
point — creation returns handles unconditionally, including pipeline creation, whose failure the
model discusses at length elsewhere. There is no validation layer design, no misuse contract, and no
assertion behavior. The one validation duty mentioned is a hypothetical validation layer complaining
about a missing dual-source output [A §Rasterizer state]. Consequently, the prototype's labels,
`LMX_ASSERT` misuse contract, and `Result`-based creation reporting are **additions** to the model,
not implementations of it; the M5.1 spec requires them of both sides, so the API-surface comparison
is only fair if the incumbent is counted under the same rules.
