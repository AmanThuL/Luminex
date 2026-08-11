# M5.1 execution-model evidence

**Status**: Proposed
**Research date**: 2026-08-12

Evidence document for the M5.1 experiment defined in
`docs/specs/2026-08-12-m5.1-rhi-execution-model-design.md`. It holds the mapping classification, the
recorded environment, all scored measurements with their uncertainty, unscored exploratory results
kept separate, rubric scoring, and gate results. It becomes `Frozen — non-normative` at milestone
close; decisions derived from it are restated in the closing ADR, never here.

At this stage only the mapping classification (section 4) and the shader constraints (section 5)
carry findings. Sections 1–3 are the frozen structure the measurement stages fill in; no
measurement has been taken, and the spec's measurement freeze gate has not yet started.

## 1. Environment

Recorded per the spec's measurement protocol for **every** scored run. One block per collection
round; a round is invalidated in full if any field changes mid-round.

| Field | Value |
|-------|-------|
| Hardware model | *(pending)* |
| Chip / GPU family | *(pending)* |
| macOS build | *(pending)* |
| Xcode version | *(pending)* |
| Metal toolchain version | *(pending)* |
| Slang version | `v2026.14.1` (pinned in `xmake.lua`) |
| Build configuration | *(pending — release for performance runs)* |
| Metal validation | *(pending — off for performance runs, `MTL_DEBUG_LAYER=1` for correctness)* |
| Power source | *(pending — AC required)* |
| Thermal state at run start | *(pending)* |
| Repository commit | *(pending)* |
| Collection round | *(pending)* |

## 2. Scored evidence

Everything in this section is produced under the spec's frozen protocol: paired AB/BA repetitions,
12 pairs per time metric, 16 warm-up plus 256 measured frames, bootstrap confidence intervals for
time metrics, exact counts for count metrics. Nothing may be added here that the spec did not
freeze.

### 2.1 Correctness

| Case | Frames / assertions | Incumbent | Prototype | Result |
|------|--------------------|-----------|-----------|--------|
| Representative graph P01–P13 | 32 frames, byte-identical readback | *(pending)* | *(pending)* | *(pending)* |
| Hazard matrix H01–H24 | expected values | *(pending)* | *(pending)* | *(pending)* |
| S-BIND (1,024 / 4,096 draws) | readback | *(pending)* | *(pending)* | *(pending)* |
| S-LIFE 12-frame schedule | retirement + allocation counters | *(pending)* | *(pending)* | *(pending)* |
| I1–I4 indirect | readback | *(pending)* | *(pending)* | *(pending)* |

### 2.2 Failure and validation behavior

| Case | Contract | Incumbent assert category / message | Prototype assert category / message |
|------|----------|-------------------------------------|-------------------------------------|
| M1 | dispatch outside a compute pass | *(pending)* | *(pending)* |
| M2 | storage write to a texture without storage usage | *(pending)* | *(pending)* |
| M3 | view mip range exceeds the texture | *(pending)* | *(pending)* |
| M4 | binding offset violates the alignment contract | *(pending)* | *(pending)* |
| M5 | use of a destroyed or never-created handle | *(pending)* | *(pending)* |
| M6 | reuse of a frame slot not proven retired | *(pending)* | *(pending)* |

### 2.3 Rubric dimensions

| Dimension | Incumbent | Prototype | Delta | Material? |
|-----------|-----------|-----------|-------|-----------|
| CPU encoding (timed region) | *(pending)* | *(pending)* | *(pending)* | *(pending)* |
| Binding traffic — calls / frame | *(pending)* | *(pending)* | *(pending)* | *(pending)* |
| Binding traffic — bytes / frame | *(pending)* | *(pending)* | *(pending)* | *(pending)* |
| API surface — concepts | *(pending)* | *(pending)* | *(pending)* | *(pending)* |
| API surface — operations | *(pending)* | *(pending)* | *(pending)* | *(pending)* |
| API surface — caller ceremony | *(pending)* | *(pending)* | *(pending)* | *(pending)* |
| Barriers — count and kind | *(pending)* | *(pending)* | *(pending)* | *(pending)* |
| Allocation — calls / resident bytes | *(pending)* | *(pending)* | *(pending)* | *(pending)* |
| Pipeline and cache behavior | *(pending — descriptive only)* | *(pending)* | — | never material |
| Capture quality checklist | *(pending)* | *(pending)* | — | gate |

### 2.4 Gate results

| Gate | Result | Evidence |
|------|--------|----------|
| 1 — correctness parity incl. three-frame lifetime | *(pending)* | *(pending)* |
| 2 — deterministic diagnosable failure on M1–M6 | *(pending)* | *(pending)* |
| 3 — capture-quality checklist | *(pending)* | *(pending)* |
| 4 — honest mappings, no load-bearing unknown | *(pending)* | section 4 |
| 5 — no permanent parallel API, bounded productionization | *(pending)* | *(pending)* |

## 3. Unscored evidence

Exploratory results added during execution live here and **cannot influence the ADR**. Every entry
states why it is unscored. The Vulkan column of section 4 is unscored by construction and carries no
backend commitment.

### 3.1 GPU addresses in a capture (unscored observation)

Unscored: it is a single-machine observation of driver behaviour, taken outside the spec's paired
protocol, and it measures nothing the rubric scores. It is recorded because section 4.6's
`capturePersistsAddresses` row is gate-relevant and this is the evidence behind it.

Procedure, reproducible from the prototype's own test binary:

```
xmake build NoApiTests
cd build/macosx/arm64/<mode>
MTL_CAPTURE_ENABLED=1 LMX_NOAPI_CAPTURE_PATH=/tmp/lmx-noapi-smoke.gputrace ./NoApiTests "[capture]"
```

The case drives `MTLCaptureManager` around one prototype frame (root data, a bindless table
publish, a debug group, a render pass, a texture readback) and reports the addresses that frame
bound. Two findings:

- **The trace records the addresses verbatim.** Searching the written bundle for the reported
  64-bit values finds the vertex and pixel root addresses inside the recorded command stream
  (`capture`, `unsorted-capture`) and the bindless-table and readback allocation bases in the
  device-resource table (`device-resources-0x…`). An address-first frame is therefore *recorded*
  losslessly; nothing about the binding model is dropped at capture time.
- **The addresses are reproducible.** Three separate processes running the same case produced
  byte-identical addresses for all four allocations (vertex root `0x100000a8000`, pixel root
  `0x100000a8020`, table `0x10000080000`, readback `0x100000d8000`), so Metal's virtual-address
  assignment is deterministic for an identical allocation sequence on this device and OS build.

What is still **unknown**: whether Xcode's replayer re-creates resources at those addresses when a
trace is replayed. No public API in the vendored headers exposes or requests that, and the replay
path cannot be driven from a test. The unknown is not load-bearing for the gates in the way the
draft assumed — the two facts above show the information is present in the trace and that a
replayer performing the same allocation sequence would land on the same addresses — but it is not
resolved, and `Capabilities::capturePersistsAddresses` reports `false` rather than claiming it.

### 3.2 Prototype smoke coverage (unscored)

Unscored: these are the Stage 2 correctness cases that prove the prototype does what section 4
claims, not workloads the rubric scores. Thirteen Catch2 cases in
`Experiments/NoApi/Tests/NoApiTests.cpp`, twelve tagged `[smoke]` plus the `[capture]` probe of
§3.1, each an offscreen render or dispatch with a readback oracle: reported capabilities;
allocation address pairs, alignment, and suballocation arithmetic; root data reaching both shader
stages; a bindless slot sampled with a sampler slot from the same table; a dispatch writing through
a storage address; a copy round-trip through private memory plus a fill; indexed, indirect, and
indexed-indirect draws over addresses; an indirect dispatch; a barrier ordering a compute image
write before a raster sample; a split-barrier signal and wait publishing counters; three-slot frame
ring recycling over six frames; and semaphore progress. All thirteen pass with
`MTL_DEBUG_LAYER=1`, and also with `MTL_DEBUG_LAYER_ERROR_MODE=assert` and
`MTL_DEBUG_LAYER_WARNING_MODE=assert`, so the run is clean of validation warnings as well as
errors.

Two prototype behaviours exist only because that stricter run flagged them, and both are recorded
in section 4: no default viewport is set at `beginRenderPass`, and compute pipeline state is not
re-set when it already matches (which only ever happens because the prototype's own signal kernel
displaced it).

## 4. Mapping classification

Every concept in the prototype interface (`Experiments/NoApi/Include/NoApi/`) classified for Metal 4
and D3D12 as **native**, **emulated**, **unavailable**, or **unknown**, per the spec's section 11.

- **Metal 4** claims marked *prototype-grounded* are what the built prototype under
  `Experiments/NoApi/Source/` does, verified by its smoke coverage; the rest are read from the vendored
  `ThirdParty/metal-cpp` headers named in the row, or marked *unverified* where the header does not
  settle the question.
- **D3D12** is a documented paper mapping only. No D3D12 code exists or is planned in M5.1.
- **Vulkan** is an unscored comparative column.
- **Shader constraint** records the Slang → MSL limit that bears on the row; section 5 holds the
  evidence for each verified claim.

Model concepts deliberately outside the prototype's scope, and therefore absent from these tables:
multi-draw indirect with a per-draw root stride, mesh and amplification shaders, framebuffer fetch
and programmable blending, texel buffers, and the dynamic (non-embedded) blend state object. Each is
described in `Experiments/NoApi/docs/model-summary.md`; none is required by the spec's frozen
workloads.

### 4.1 Memory, addresses, and root data

| Concept | Metal 4 | D3D12 | Shader constraint | Vulkan (unscored) |
|---------|---------|-------|-------------------|-------------------|
| `allocate` returning a `{cpu, gpu}` address pair | **Emulated (thin)** — *prototype-grounded*: one `MTL::Buffer` per allocation supplies both (`MTLBuffer::contents`, `MTLBuffer::gpuAddress`). Metal picks the address, so the caller's alignment is met by reserving `size + alignment - 1` and aligning inside the reservation (`Source/Memory.cpp`) | **Native** — `Map` plus `GetGPUVirtualAddress` on a committed resource | none | **Native** — mapped memory plus `VK_KHR_buffer_device_address` |
| `MemoryKind::Shared` (CPU-mapped, GPU-readable) | **Native** — *prototype-grounded*: `MTLResourceStorageModeShared` plus `CPUCacheModeWriteCombined`, the model's streaming mapping | **Native** — `D3D12_HEAP_TYPE_GPU_UPLOAD`; *unknown* on adapters without ReBAR | none | **Native** — `HOST_VISIBLE \| DEVICE_LOCAL` |
| `MemoryKind::Private` | **Emulated (heap + cover buffer)** — *prototype-grounded*: a private allocation is an `MTLHeap(HeapTypePlacement, StorageModePrivate, HazardTrackingModeUntracked)` plus one cover `MTL::Buffer` created at heap offset 0 spanning it. The heap is what textures are placed into; the cover buffer is what supplies the allocation's base GPU address and what copy commands resolve to. A bare private `MTLBuffer` cannot hold a placed texture | **Native** — `D3D12_HEAP_TYPE_DEFAULT` | none | **Native** |
| `MemoryKind::Readback` | **Native** — *prototype-grounded*: shared storage with the default (cached) CPU cache mode; readback is correct after a semaphore wait | **Native** — `D3D12_HEAP_TYPE_READBACK` | none | **Native** — `HOST_CACHED` |
| `GpuAddress` as the only buffer reference | **Native at consumption** — `MTL4::ArgumentTable::setAddress`, `drawIndexedPrimitives(... GPUAddress indexBuffer ...)`, `dispatchThreadgroups(GPUAddress)` (`MTL4ArgumentTable.hpp`, `MTL4RenderCommandEncoder.hpp`, `MTL4ComputeCommandEncoder.hpp`); the address must belong to a live, resident `MTLBuffer`. **Emulated at the copy side**: the prototype keeps a base-sorted registry of live allocations and resolves an address to `(MTL::Buffer*, offset)` in `O(log n)` for every copy, fill, and index or indirect range check (`Source/Metal4Internal.cpp`) | **Emulated** — root CBV/SRV/UAV and `IASetIndexBuffer` take a virtual address, but copies and `ExecuteIndirect` take `ID3D12Resource*` plus offset | HLSL has no pointer type; a D3D12 frontend must use `ByteAddressBuffer`. Slang → MSL emits real `device T*` (§5) | **Native** with BDA; same copy-side gap as D3D12 |
| Texture placed at an explicit address (`textureSizeAlign` + `createTexture(desc, placement)`) | **Emulated** — *prototype-grounded*: `MTLDevice::heapTextureSizeAndAlign` plus `MTLHeap::newTexture(desc, offset)`, with address → (heap, offset) resolved against the owning allocation. Verified by placing every smoke-test texture inside one 8 MiB private allocation | **Emulated** — `GetResourceAllocationInfo` plus `CreatePlacedResource(heap, offset)`; identical shape | none | **Emulated** — `vkGetImageMemoryRequirements` plus `vkBindImageMemory(offset)`; the model names Vulkan's create-then-query order as a design flaw |
| `LinearAllocator` / `Suballocation` | **Native** — pure userland pointer arithmetic | **Native** | none | **Native** |
| `pushRoot` (block by value → address) | **Native** — a write into mapped memory plus an address | **Native** | none | **Native**; `vkCmdPushDataEXT` is the API-side equivalent |
| Root data delivered to the shader as a pointer | **Emulated (thin, load-bearing)** — *prototype-grounded*: the address is argument-table state, not a call parameter. Exact measured cost: **one `MTL4::ArgumentTable::setAddress` per non-null root address per draw or dispatch** — up to 2 per draw, 1 per dispatch, plus 1 per `setBindlessTable`. Every one of them passes through the single `bindAddress` choke point in `Source/CommandBuffer.cpp`. See §6.1 | **Emulated** — `SetGraphicsRootConstantBufferView` per draw, plus a mandatory root signature | Slang `ConstantBuffer<T>` lowers to `T constant* [[buffer(n)]]` (§5.1) | **Emulated** — same shape via BDA in a push constant |
| Separate vertex and pixel root addresses | **Emulated** — *prototype-grounded*: one argument table with pinned bind indices — vertex and compute root at 0, bindless table at 1, pixel root at 2, the prototype's own signal kernel at 3. `setArgumentTable` is issued once per encoder with `RenderStageVertex \| RenderStageFragment` | **Native** — root parameter visibility masks | none | **Native** — push-constant stage ranges |
| Specialization constant block containing addresses | **Unavailable** — *prototype-grounded*: `MTL::FunctionConstantValues` / `MTL4::SpecializedFunctionDescriptor` take indexed scalars, and there is no way to feed them the model's opaque struct. The prototype refuses a non-empty `specConstants` with an assert naming the pipeline rather than silently dropping it. Not load-bearing: no frozen workload uses specialization | **Unavailable** — D3D12 has no specialization constants; permutations are compiled offline | Slang's `[SpecializationConstant]` → MSL function constants is **unverified** | **Emulated** — `VkSpecializationInfo`, and the model records that it cannot change layouts |

### 4.2 Handles and bindings

| Concept | Metal 4 | D3D12 | Shader constraint | Vulkan (unscored) |
|---------|---------|-------|-------------------|-------------------|
| One bindless table at a plain `GpuAddress` | **Native** — *prototype-grounded*: a shared `MTL::Buffer` of `MTL::ResourceID`, written by ordinary CPU stores and published to shaders by **one** `setAddress` at bind index 1. No descriptor-heap API is involved | **Emulated** — `ResourceDescriptorHeap` (SM 6.6) is a driver-owned heap, not addressable memory | Slang lowers `ConstBufferPointer<DescriptorHandle<Texture2D>>` to `device texture2d<...>*` with runtime indexing (§5.2) | **Native** — `VK_EXT_descriptor_buffer` / `VK_EXT_descriptor_heap` |
| 32-bit slot index shader-side | **Emulated (bounded)** — *prototype-grounded*: `MTL::ResourceID` is 64-bit, so `Capabilities::bindlessSlotStride` reports 8, doubling table bytes against the model's 4. The interface keeps the 32-bit slot index; only the stride differs | **Native** — SM 6.6 heap index is 32-bit | none | **Native** |
| Contiguous slot ranges (base + offset) | **Native** — a caller-owned `ResourceID` array indexes contiguously; `MTLResourceViewPool::baseResourceID` plus `copyResourceViewsFromPool` gives the same over a pool (`MTLResourceViewPool.hpp`). The article's claim that Metal cannot express contiguous ranges predates `MTLTextureViewPool` | **Native** — heap slots are contiguous by construction | none | **Native** |
| CPU write of a texture view into a slot | **Native** — *prototype-grounded*: the prototype stores `gpuResourceID` directly (whole-texture slots) or creates an `MTLTexture` view with `newTextureView` and stores its ID (subrange or reinterpreted slots), keeping the view object alive in the slot's CPU-side state. `MTLResourceViewPool` is deliberately not used: a pool is another driver object, and a plain store is the model's shape | **Emulated** — `CreateShaderResourceView` into a staging heap plus `CopyDescriptorsSimple`; no direct write | none | **Native** — `vkGetDescriptorEXT` writes into caller memory |
| Sampler in the **same** table as textures | **Native** — *prototype-grounded*: samplers created with `setSupportArgumentBuffers(true)` expose `gpuResourceID`, and a smoke case samples a texture slot with a sampler slot read from the same table address | **Unavailable as one table** — samplers require a separate sampler heap with its own index space; a D3D12 frontend needs two tables | none | **Native** — one descriptor buffer may hold both |
| GPU (compute) writes into the table | **Unknown** — storing a `ResourceID` from a shader is a plain 64-bit store, but no header settles whether Metal honors GPU-authored IDs. **Not load-bearing**: no frozen workload writes descriptors from the GPU | **Unavailable** — the descriptor heap is not GPU-writable | none | **Native** with descriptor buffer |
| `TextureViewDesc` (subresource range, format reinterpretation) | **Native** — *prototype-grounded*: `newTextureView`, which requires `MTLTextureUsagePixelFormatView` on the parent; the prototype adds it to every sampled or storage texture | **Native** — SRV/UAV descriptors carry the range | none | **Native** — `VkImageView` |

### 4.3 Pipelines and commands

| Concept | Metal 4 | D3D12 | Shader constraint | Vulkan (unscored) |
|---------|---------|-------|-------------------|-------------------|
| Pipeline with no binding layout | **Emulated (thin)** — *prototype-grounded*: no root signature exists, but `MTL4::ArgumentTableDescriptor` still declares bind counts. The prototype declares 4 buffer binds and **zero** texture and sampler binds, because nothing shader-visible is bound as a texture or sampler | **Emulated** — a root signature object is mandatory; the minimum is two root descriptors | none | **Emulated** — `VkPipelineLayout` is mandatory |
| `RasterDesc` baking only microcode-affecting state | **Native**, with one gap — *prototype-grounded*: `MTL4::RenderPipelineDescriptor` takes color formats, write masks, sample count, topology class, alpha-to-coverage, and embedded blend, but carries **no depth attachment format**; the render pass supplies it. `RasterDesc::depthFormat` therefore only feeds the prototype's own pipeline-versus-pass check | **Native** for the same fields | none | **Native** |
| Separate `DepthStencilState` object | **Native** — *prototype-grounded*: `MTLDepthStencilState` plus `setDepthStencilState`; a disabled depth test is expressed as compare-always | **Unavailable** — depth-stencil state is baked into the PSO; only stencil reference and depth bounds are dynamic | none | **Native** — VK 1.3 extended dynamic state |
| Dynamic viewport, scissor, cull, winding, depth bias, stencil reference | **Native** — *prototype-grounded*. One detail: the prototype sets **no** default viewport at `beginRenderPass`. Metal derives one from the pass's render-target size, and setting it again makes every caller-supplied viewport a redundant state change that the validation layer reports | **Partly unavailable** — viewport and scissor are dynamic; cull mode, winding, and depth bias are PSO state | none | **Native** — VK 1.3 |
| Embedded blend only; `separateBlendState == false` | **Native as embedded** — Metal bakes blend into the render pipeline, so the model's dynamic blend object is **unavailable**. The interface exposes only the embedded form, so nothing is load-bearing | **Native as embedded**; dynamic blend likewise **unavailable** | none | **Native as embedded**; dynamic blend available in VK 1.3 |
| Transient command buffers | **Native** — *prototype-grounded*: `beginCommands` takes a pooled context (`MTL4::CommandAllocator` + `MTL4::CommandBuffer` + `MTL4::ArgumentTable`) whose allocator may only be reset once the submission that used it retired. One `MTLSharedEvent` submission timeline proves that for every context, so there is still no per-submission fence object | **Native** — a command allocator reset per frame slot | none | **Native** — one-shot buffers from a per-frame pool |
| Index data supplied by address | **Native** — *prototype-grounded*: `drawIndexedPrimitives(..., GPUAddress indexBuffer, indexBufferLength, ...)`; Metal wants the bytes remaining at the address, which the allocation registry supplies | **Native** — `D3D12_INDEX_BUFFER_VIEW::BufferLocation` | none | **Native** — `vkCmdBindIndexBuffer2` still takes a buffer handle; BDA does not cover indices |
| Indirect draw / dispatch arguments by address | **Native** — *prototype-grounded*: `drawPrimitives(type, GPUAddress)`, `drawIndexedPrimitives(..., GPUAddress indirectBuffer)`, and `dispatchThreadgroups(GPUAddress, ...)`; all three verified against CPU-written arguments | **Emulated** — `ExecuteIndirect` takes `ID3D12Resource*` plus offset and a command signature | none | **Emulated** — `vkCmdDrawIndirect` takes a buffer handle |
| Threadgroup shape of a dispatch | **Emulated (pinned)** — *prototype-grounded*: the model's `dispatch` carries only threadgroup counts and its pipeline descriptor carries no shape, while `dispatchThreadgroups` requires one. The prototype pins **8x8x1** for every compute pipeline, checks it against `maxTotalThreadsPerThreadgroup` at creation, and shaders are written against it | **Native** — HLSL's `[numthreads]` bakes the shape into the shader, so `Dispatch` needs only counts | HLSL `[numthreads(x,y,z)]`; MSL has no shape attribute | **Native** — SPIR-V `LocalSize` |
| Copies and fills over addresses | **Emulated** — *prototype-grounded*: `MTL4::ComputeCommandEncoder::copyFromBuffer` / `fillBuffer` / `copyFromTexture` take `MTL::Buffer*` plus offset, so every address is resolved through the allocation registry. Metal 4 has no blit encoder, so copies share the compute encoder and the prototype opens one lazily outside render passes | **Emulated** — `CopyBufferRegion` takes resources plus offsets | none | **Emulated** — same shape |

### 4.4 Attachments and synchronization

| Concept | Metal 4 | D3D12 | Shader constraint | Vulkan (unscored) |
|---------|---------|-------|-------------------|-------------------|
| Attachments declared at the pass boundary | **Native** — *prototype-grounded*: `MTL4::RenderPassDescriptor` per pass, with load and store actions, clear values, mip and slice selection, and explicit render-target extents | **Native** — `BeginRenderPass` / `OMSetRenderTargets` | none | **Native** — dynamic rendering (VK 1.3) |
| No implicit barrier at pass boundaries | **Native, verified** — *prototype-grounded*: opening or closing an encoder emits nothing, and a compute-then-sample smoke case reads stale texels unless the caller states the dependency. Metal 4 orders nothing implicitly between encoders | **Native** — enhanced barriers are fully explicit | none | **Native** |
| Stage-mask barrier with no resource list | **Native, with a scope split** — *prototype-grounded*: `barrierAfterQueueStages` orders work from **earlier encoders** only and does **not** order commands recorded earlier in the same encoder; `barrierAfterEncoderStages` does the opposite. Neither is a superset of the other (found by a copy-then-copy readback that silently returned zeros under queue scope alone). One `barrier()` therefore lowers to one primitive in the ordinary cases and two when the producer stages straddle both scopes. See §6.7 | **Emulated** — enhanced barriers offer `D3D12_BARRIER_TYPE_GLOBAL` with sync/access scopes, but texture layouts persist and per-resource barriers remain the documented path | none | **Emulated** — `VK_KHR_unified_image_layouts` removes layouts but keeps the resource list |
| `Hazard::Descriptors` | **Unknown (benign)** — *prototype-grounded*: `MTL4::VisibilityOptions` offers only `None`, `Device`, and `ResourceAlias`, so the flag rides on the device-scope visibility every barrier already requests. A copy-then-bindless-sample smoke case passes with it and with no separate descriptor maintenance available. **Not load-bearing** | **Emulated** — implied by heap-copy ordering rules | none | **Native** — descriptor-buffer invalidate flag |
| `Hazard::DrawArguments` | **Emulated** — *prototype-grounded*: the flag widens the consumer stage mask to `StageVertex \| StageDispatch`, which is where a command processor fetches arguments. The GPU-written-arguments cases (I2/I4) that would exercise it are Stage 4's; the CPU-written cases need no hazard and pass | **Native** — `D3D12_BARRIER_ACCESS_INDIRECT_ARGUMENT` | none | **Native** — `VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT` |
| `Hazard::DepthStencil` | **Emulated** — *prototype-grounded*: the flag widens the consumer mask to `StageFragment`, which is the only stage Metal exposes for depth output; there is no dedicated flag | **Native** — depth-stencil access bits | none | **Native** — depth stage/access bits |
| Split barrier signalling a **memory counter** (`signalAfter` / `waitBefore`, atomic max/or, wait mask) | **Emulated (load-bearing, bounded)** — *prototype-grounded*, three separate pieces. **Ordering** is a stage barrier, not a fence: `barrierAfterQueueStages`/`barrierAfterEncoderStages` already reach across encoders, so `MTLFence` is never needed and is not used. **Value publication** is a real GPU write: `SignalOp::Set` copies the exact 64-bit value from an eight-byte suballocation of the frame's root storage; `AtomicMax` and `AtomicOr` run a one-thread internal kernel using **32-bit** device atomics on the counter's low word, because Metal rejects `atomic_ulong` on this target (§5.8) — values above 2^32-1 assert rather than truncate. **The wait's predicate is not evaluated on the GPU at all**: Metal has no wait-on-memory-value command, so `waitBefore` emits the stage dependency and checks `value`, `op`, and `mask` at record time against the `signalAfter` that paired with it, asserting when no such signal was recorded in the same command buffer. A split barrier that waits on a value another submission will write is therefore **unavailable**. See §6.2 | **Unavailable** — split barriers exist (BEGIN/END) but there is no in-command-list wait on a memory value; `ID3D12CommandQueue::Wait` is queue-level | none | **Emulated** — `VkEvent` is an object; the model names its usability as the reason nobody uses it |
| Timeline semaphore for CPU pacing | **Native** — *prototype-grounded*: `MTLSharedEvent` plus `MTL4::CommandQueue::signalEvent` and `waitUntilSignaledValue`; the prototype signals its internal submission timeline before any caller semaphore, so a caller's wait also proves context retirement | **Native** — `ID3D12Fence` | none | **Native** — VK 1.2 timeline semaphores |
| `FrameRing` three-frames-in-flight retirement proof | **Native** — *prototype-grounded*: userland over one timeline semaphore; frames are numbered from one, frame *N* signals *N*, and `beginFrame` waits for *N-3* before resetting the slot's allocator and re-checks the value afterwards. Verified over six frames across three slots | **Native** | none | **Native** |

### 4.5 Residency and capabilities

| Concept | Metal 4 | D3D12 | Shader constraint | Vulkan (unscored) |
|---------|---------|-------|-------------------|-------------------|
| Single `ResidencySet` | **Native, and required** — `MTLResidencySet` with `addAllocation` / `commit` / `requestResidency` (`MTLResidencySet.hpp`), attached via `MTL4::CommandQueue::addResidencySet`. The comparison model has no residency concept at all, so every line of this row is target-imposed. *Prototype-grounded* shape: the Metal set is created with the device and attached to the queue once; every `allocate` joins it immediately and marks it uncommitted; `createResidencySet` hands back a handle onto that same set; `submit` commits any pending change the caller did not commit, so a forgotten `commitResidency` costs a set-wide republish inside the submission rather than a GPU fault. Committing explicitly still matters, because it moves that republish out of a timed region | **Emulated** — `ID3D12Device::MakeResident` / `EnqueueMakeResident` over resource lists; no set object | none | **Native (as absence)** — Vulkan requires no residency declaration, matching the model |
| `Capabilities` query | **Emulated** — *prototype-grounded*: `MTLGPUFamilyMetal4` and tier-2 argument buffers are queried and required (creation fails deterministically otherwise); `bindlessSlotStride` = 8 (`sizeof(MTL::ResourceID)`), `maxBindlessSlots` = 500,000 (Metal's tier-2 limit), and the three alignment minima = 4 are pinned constants Metal exposes no query for. `separateBlendState` = false, `memoryBackedFences` = false, `residencyRequired` = true | **Emulated** — `CheckFeatureSupport` covers some fields; alignment minima are documented constants | none | **Emulated** — `VkPhysicalDeviceProperties` plus extension property structs |

### 4.6 Debugging and failure behavior

| Concept | Metal 4 | D3D12 | Shader constraint | Vulkan (unscored) |
|---------|---------|-------|-------------------|-------------------|
| Mandatory label on every object | **Native** — *prototype-grounded*: every device, queue, compiler, residency set, event, allocation (heap, cover buffer, buffer), texture, view, sampler, pipeline, depth state, allocator, argument table, command buffer, encoder, render pass, and debug group carries a label; empty labels are rejected by assert at creation | **Native** — `SetName` | none | **Native** — debug-utils object names |
| Debug groups grouping a pass in a capture | **Native** — *prototype-grounded*: `MTL4::CommandBuffer::pushDebugGroup` / `popDebugGroup`, balanced at `endCommands` by assert; groups straddle encoders because they live on the command buffer | **Native** — PIX events | none | **Native** — debug-utils labels |
| `LMX_ASSERT` misuse with context | **Native** — prototype-side; not an API feature on any target | **Native** | none | **Native** |
| `Result`-based creation failure | **Native** — metal-cpp creation takes `NS::Error**` | **Native** — `HRESULT` plus the info queue | none | **Native** — `VkResult` |
| Capture replays with identical GPU addresses (`capturePersistsAddresses`) | **Unknown for replay, but bounded by two verified facts** — *prototype-grounded*, see §3.1: a programmatic `MTLCaptureManager` capture of one prototype frame (a) records the exact 64-bit addresses `setAddress` bound in its command stream and each allocation's base address in its device-resource table, and (b) is reproducible — the same allocation sequence yields byte-identical GPU addresses across separate processes. What remains unverified is whether Xcode's replayer *re-creates* resources at those addresses; no public API exposes that, so `Capabilities::capturePersistsAddresses` conservatively reports `false` | **Native** — a public recreate-at-address path exists | none | **Native** — `VkMemoryOpaqueCaptureAddressAllocateInfo` |

## 5. Shader constraints (Slang → MSL)

Verified against the pinned `ThirdParty/slang` toolchain (`v2026.14.1`) by compiling probe shaders
with `slangc -target metal`. These are the constraints that decide whether the prototype's binding
frontend can wrap the production math modules or must be hand-written MSL, which the spec's
shader-control rule permits and requires to be recorded here.

**5.1 Root data as a pointer — supported.** `ConstantBuffer<Root>` lowers to
`Root constant* [[buffer(0)]]`, which is exactly what `MTL4::ArgumentTable::setAddress` binds. The
shared-header contract the model depends on therefore survives the Slang path.

**5.2 Nested GPU pointers — supported.** `ConstBufferPointer<T>` lowers to `device T*` and supports
`ptr[index]` addressing, so vertex arrays, material tables, and index data reached through a root
struct all lower to real 64-bit pointer loads rather than to buffer bindings.

**5.3 Bindless handles in a struct — supported.** `DescriptorHandle<Texture2D>` lowers to an inline
`texture2d<float, access::sample>` field inside the root struct — a `MTLResourceID` stored in GPU
memory — and `DescriptorHandle<SamplerState>` to an inline `sampler`.

**5.4 Indexable bindless table — supported.** `ConstBufferPointer<DescriptorHandle<Texture2D>>`
lowers to `device texture2d<float, access::sample>*` with runtime indexing, which is the model's
descriptor heap expressed end to end on Metal 4. This is the finding that makes the prototype's
single-table design viable without hand-written MSL.

**5.5 Production shaders cannot be reused unchanged.** `Shaders/ScenePass.slang` and its siblings
declare flat `[[vk::binding(...)]]` / `register(...)` slots that map 1:1 to `MTL4ArgumentTable`
indices (`docs/conventions/shader-style.md`). The prototype therefore needs its own binding
frontend around the shared `Lighting`, `Encode`, and `Shadow` modules; the spec's section 2 permits
exactly this, and the byte-identical parity oracle bounds the resulting math divergence at zero.

**5.6 Open shader questions.**

- Whether Slang emits a non-uniform-index-safe sample instruction for MSL when the table index
  varies per lane. The frozen workloads index materials per draw (uniform within a draw), so this is
  **not load-bearing**, but it is recorded because the model's central claim rests on it.
- Whether Slang's specialization constants reach MSL function constants is now moot for the
  prototype: the Metal side cannot accept the model's specialization *struct* at all (§4.1, §6.9),
  so no Slang path was pursued.
- Whether the two-step Slang → readable MSL → runtime compile path of ADR 0003 changes any of
  §5.1–§5.4 under the offline `metallib` path; the ADR records open Slang → metallib bugs, so the
  prototype follows the same readable-MSL route the production build uses.

**5.7 A bindless slot must be a struct member, not a bare pointer target.** Verified against Apple's
runtime Metal compiler (Xcode 26.6 / macOS 26.5): a top-level buffer argument of pointer-to-texture
type is **rejected** — `device texture2d<float>* t [[buffer(1)]]` fails with *"type
'texture2d<float>' cannot be used in buffer pointee type"*, and so does the exact form §5.4 records
Slang as emitting, `device texture2d<float, access::sample>*`. The same pointer compiles once the
texture is a struct member — `struct Slot { texture2d<float> tex; }; device const Slot* table
[[buffer(1)]]`, indexed at runtime — and `sizeof(Slot)` is 8, matching `MTL::ResourceID`. The
prototype's shaders therefore view the one flat table through one-member wrapper structs, one per
shader-side type (`texture2d<float>`, `texture2d<float, access::write>`, `depth2d<float>`,
`sampler`), all reading the same table address. This qualifies §5.4 rather than contradicting it:
the indexable-table design holds, but a Slang frontend must place the handle inside a struct, and
whether Slang's emitted MSL already does so has to be re-checked before the bench relies on it.

**5.8 No 64-bit device atomics.** `atomic_ulong` is rejected by the Metal compiler on this target:
`atomic_store_explicit`, `atomic_fetch_max_explicit`, and `atomic_fetch_or_explicit` have no
`device ulong` overload. A plain (non-atomic) 64-bit `device ulong` store compiles. This is what
bounds the prototype's `SignalOp::AtomicMax` / `AtomicOr` emulation to 32 bits (§4.4).

## 6. Recorded tensions

Places where the model and the target visibly disagree, recorded rather than resolved silently in
the interface. Each is re-checked against the built prototype in Stage 2.

**6.1 Root data is state on Metal 4, not a call parameter.** The model's whole per-draw binding
claim is that a draw *carries* its root address. Metal 4 requires the address to be argument-table
state set before the draw, so the prototype's `draw(rootVertex, rootPixel, …)` lowers to one or two
`setAddress` calls plus the draw. The binding-traffic dimension must therefore count those calls on
the prototype side; a measurement that omitted them would flatter the model. **Built cost, exactly**:
one `setAddress` per non-null root address — 2 per `draw`/`drawIndexed`/`drawIndirect`/
`drawIndexedIndirect` when both stages take root data, 1 per `dispatch`/`dispatchIndirect`, 1 per
`setBindlessTable`, and 1 more per `signalAfter` whose op is atomic (the internal kernel's root).
Nothing is deduplicated against previously bound addresses, so the count is exactly what the caller
asked for; every one of these calls goes through the `bindAddress` helper in
`Source/CommandBuffer.cpp`, which is the single place Stage 4's instrumentation has to touch.

**6.2 Split barriers order stages; the counter is only published, never waited on.** Building it
settled the draft's guess. `MTLFence` turned out to be unnecessary — `barrierAfterQueueStages` and
`barrierAfterEncoderStages` already order across and within encoders — so the prototype uses no
fence object at all. What Metal genuinely lacks is the *wait*: no command waits on a value in
memory. The prototype therefore publishes the counter with a real GPU write (an eight-byte copy for
`Set`; a one-thread 32-bit atomic kernel for `AtomicMax`/`AtomicOr`, since 64-bit device atomics do
not exist here) and evaluates the wait's comparison and mask **on the CPU at record time** against
the paired signal, asserting when the pairing is absent. Ordering is real; the predicate is not
hardware-enforced; and a split barrier across submissions is unavailable. Any scored case that needs
a GPU-evaluated comparison must be recorded as a gap rather than claimed.

**6.3 Bindless handles cost 8 bytes, not 4.** `MTL::ResourceID` is 64-bit, so a 5-texture material
set costs 40 bytes of table space against the model's 20 — the article's own complaint, still true.
The prototype keeps the 32-bit slot index in its interface and reports the real stride through
`Capabilities::bindlessSlotStride`, so the binding-bytes measurement stays honest.

**6.4 Residency exists only because the target requires it.** The model allocates and addresses
memory with no residency step. Metal 4 requires a committed residency set. The prototype keeps
exactly one set and counts its cost against the prototype, not against the incumbent.

**6.5 Texture placement needs a heap, not an address.** `createTexture(desc, placement)` reads as
address-first, but Metal 4 and D3D12 both place textures at an offset *within a heap object*. The
prototype resolves address → (heap, offset) internally. This is bounded, symmetric across both
scored targets, and the reason `Allocation` — not a bare pointer — is the unit the interface hands
out.

**6.6 The model specifies no failure behavior at all.** Labels, misuse assertions, and
`Result`-based creation reporting are additions the M5.1 spec requires of both sides, not features
of the model. The API-surface dimension must count them under the same rules on both sides, or the
comparison rewards the prototype for an omission the spec forbids it to keep.

**6.7 Metal 4's two barrier scopes do not nest.** `barrierAfterQueueStages` orders work from earlier
encoders and *not* commands recorded earlier in the same encoder; `barrierAfterEncoderStages` does
the reverse. This is not documented as a disjointness and was found the expensive way: a
copy-then-copy chain inside one compute encoder, ordered with queue scope alone, silently read
zeros. The prototype now picks the scope from where the producer stages can have run and emits both
primitives when they straddle — so one `barrier()` call is not always one Metal barrier, and the
barrier-count dimension must count emitted primitives, not interface calls.

**6.8 A dispatch on Metal 4 needs a threadgroup shape the model never carries.** `dispatch` names
threadgroup counts only, and `ComputePipelineDesc` has no shape field, but `dispatchThreadgroups`
requires one and MSL has no `[numthreads]` equivalent to read it from. The prototype pins 8x8x1 for
every kernel. HLSL bakes the shape into the shader, so this gap is Metal-specific; a production
interface would have to carry the shape or the pipeline would have to report it.

**6.9 Specialization constants are unavailable, not merely awkward.** Metal's function constants are
indexed scalars; there is no way to hand them the model's specialization *struct*, let alone one
containing addresses. The prototype refuses a non-empty specialization block with an assert instead
of pretending. No frozen workload needs it, so this is recorded rather than load-bearing.
