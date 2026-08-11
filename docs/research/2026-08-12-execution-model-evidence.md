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

*(none recorded)*

## 4. Mapping classification

Every concept in the prototype interface (`Experiments/NoApi/Include/NoApi/`) classified for Metal 4
and D3D12 as **native**, **emulated**, **unavailable**, or **unknown**, per the spec's section 11.

- **Metal 4** claims are grounded in the vendored `ThirdParty/metal-cpp` headers named in each row,
  or marked *unverified* where the header does not settle the question. They become prototype-grounded
  in Stage 2.
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
| `allocate` returning a `{cpu, gpu}` address pair | **Emulated (thin)** — one `MTL::Buffer` per allocation supplies both: `MTLBuffer::contents`, `MTLBuffer::gpuAddress` (`MTLBuffer.hpp`) | **Native** — `Map` plus `GetGPUVirtualAddress` on a committed resource | none | **Native** — mapped memory plus `VK_KHR_buffer_device_address` |
| `MemoryKind::Shared` (CPU-mapped, GPU-readable) | **Native** — `MTLResourceStorageModeShared` on UMA | **Native** — `D3D12_HEAP_TYPE_GPU_UPLOAD`; *unknown* on adapters without ReBAR | none | **Native** — `HOST_VISIBLE \| DEVICE_LOCAL` |
| `MemoryKind::Private` | **Native** — `MTLResourceStorageModePrivate` | **Native** — `D3D12_HEAP_TYPE_DEFAULT` | none | **Native** |
| `MemoryKind::Readback` | **Native** — shared storage with default (cached) CPU cache mode | **Native** — `D3D12_HEAP_TYPE_READBACK` | none | **Native** — `HOST_CACHED` |
| `GpuAddress` as the only buffer reference | **Native at consumption** — `MTL4::ArgumentTable::setAddress`, `drawIndexedPrimitives(... GPUAddress indexBuffer ...)`, `dispatchThreadgroups(GPUAddress)` (`MTL4ArgumentTable.hpp`, `MTL4RenderCommandEncoder.hpp`, `MTL4ComputeCommandEncoder.hpp`); the address must belong to a live, resident `MTLBuffer` | **Emulated** — root CBV/SRV/UAV and `IASetIndexBuffer` take a virtual address, but copies and `ExecuteIndirect` take `ID3D12Resource*` plus offset | HLSL has no pointer type; a D3D12 frontend must use `ByteAddressBuffer`. Slang → MSL emits real `device T*` (§5) | **Native** with BDA; same copy-side gap as D3D12 |
| Texture placed at an explicit address (`textureSizeAlign` + `createTexture(desc, placement)`) | **Emulated** — `MTLDevice::heapTextureSizeAndAlign` plus `MTLHeap(HeapTypePlacement)::newTexture(desc, offset)`; the prototype resolves address → (heap, offset) | **Emulated** — `GetResourceAllocationInfo` plus `CreatePlacedResource(heap, offset)`; identical shape | none | **Emulated** — `vkGetImageMemoryRequirements` plus `vkBindImageMemory(offset)`; the model names Vulkan's create-then-query order as a design flaw |
| `LinearAllocator` / `Suballocation` | **Native** — pure userland pointer arithmetic | **Native** | none | **Native** |
| `pushRoot` (block by value → address) | **Native** — a write into mapped memory plus an address | **Native** | none | **Native**; `vkCmdPushDataEXT` is the API-side equivalent |
| Root data delivered to the shader as a pointer | **Emulated (thin, load-bearing)** — the address is argument-table state, not a call parameter: one `ArgumentTable::setAddress` per draw per stage. See §6.1 | **Emulated** — `SetGraphicsRootConstantBufferView` per draw, plus a mandatory root signature | Slang `ConstantBuffer<T>` lowers to `T constant* [[buffer(n)]]` (§5.1) | **Emulated** — same shape via BDA in a push constant |
| Separate vertex and pixel root addresses | **Emulated** — two bind indices in one argument table, or two tables; `setArgumentTable` takes a `RenderStages` mask | **Native** — root parameter visibility masks | none | **Native** — push-constant stage ranges |
| Specialization constant block containing addresses | **Emulated** — `MTL::FunctionConstantValues` / `MTL4::SpecializedFunctionDescriptor` take indexed scalars, not a struct; whether an address-valued constant is treated as a pointer is **unknown** | **Unavailable** — D3D12 has no specialization constants; permutations are compiled offline | Slang's `[SpecializationConstant]` → MSL function constants is **unverified** | **Emulated** — `VkSpecializationInfo`, and the model records that it cannot change layouts |

### 4.2 Handles and bindings

| Concept | Metal 4 | D3D12 | Shader constraint | Vulkan (unscored) |
|---------|---------|-------|-------------------|-------------------|
| One bindless table at a plain `GpuAddress` | **Native** — a buffer of `MTL::ResourceID`; `MTLTexture::gpuResourceID`, `MTLSamplerState::gpuResourceID` (`MTLTexture.hpp`, `MTLSampler.hpp`) | **Emulated** — `ResourceDescriptorHeap` (SM 6.6) is a driver-owned heap, not addressable memory | Slang lowers `ConstBufferPointer<DescriptorHandle<Texture2D>>` to `device texture2d<...>*` with runtime indexing (§5.2) | **Native** — `VK_EXT_descriptor_buffer` / `VK_EXT_descriptor_heap` |
| 32-bit slot index shader-side | **Emulated (bounded)** — `MTL::ResourceID` is 64-bit, so `Capabilities::bindlessSlotStride` is 8, doubling table bytes against the model's 4 | **Native** — SM 6.6 heap index is 32-bit | none | **Native** |
| Contiguous slot ranges (base + offset) | **Native** — a caller-owned `ResourceID` array indexes contiguously; `MTLResourceViewPool::baseResourceID` plus `copyResourceViewsFromPool` gives the same over a pool (`MTLResourceViewPool.hpp`). The article's claim that Metal cannot express contiguous ranges predates `MTLTextureViewPool` | **Native** — heap slots are contiguous by construction | none | **Native** |
| CPU write of a texture view into a slot | **Native** — `MTLTextureViewPool::setTextureView(texture, descriptor, index)`, or store `gpuResourceID` directly | **Emulated** — `CreateShaderResourceView` into a staging heap plus `CopyDescriptorsSimple`; no direct write | none | **Native** — `vkGetDescriptorEXT` writes into caller memory |
| Sampler in the **same** table as textures | **Native** — samplers created with `MTLSamplerDescriptor::setSupportArgumentBuffers(true)` expose `gpuResourceID` (`MTLSampler.hpp`) | **Unavailable as one table** — samplers require a separate sampler heap with its own index space; a D3D12 frontend needs two tables | none | **Native** — one descriptor buffer may hold both |
| GPU (compute) writes into the table | **Unknown** — storing a `ResourceID` from a shader is a plain 64-bit store, but no header settles whether Metal honors GPU-authored IDs. **Not load-bearing**: no frozen workload writes descriptors from the GPU | **Unavailable** — the descriptor heap is not GPU-writable | none | **Native** with descriptor buffer |
| `TextureViewDesc` (subresource range, format reinterpretation) | **Native** — `MTLTextureViewDescriptor` via the view pool or `newTextureView` | **Native** — SRV/UAV descriptors carry the range | none | **Native** — `VkImageView` |

### 4.3 Pipelines and commands

| Concept | Metal 4 | D3D12 | Shader constraint | Vulkan (unscored) |
|---------|---------|-------|-------------------|-------------------|
| Pipeline with no binding layout | **Emulated (thin)** — no root signature exists, but `MTL4::ArgumentTableDescriptor` still declares max buffer/texture/sampler bind counts, per encoder rather than per pipeline | **Emulated** — a root signature object is mandatory; the minimum is two root descriptors | none | **Emulated** — `VkPipelineLayout` is mandatory |
| `RasterDesc` baking only microcode-affecting state | **Native** — `MTL4::RenderPipelineDescriptor` takes attachment formats, sample count, topology class | **Native** for the same fields | none | **Native** |
| Separate `DepthStencilState` object | **Native** — `MTLDepthStencilState` plus `setDepthStencilState` (`MTL4RenderCommandEncoder.hpp`) | **Unavailable** — depth-stencil state is baked into the PSO; only stencil reference and depth bounds are dynamic | none | **Native** — VK 1.3 extended dynamic state |
| Dynamic viewport, scissor, cull, winding, depth bias, stencil reference | **Native** — `setViewport`, `setScissorRect`, `setCullMode`, `setFrontFacingWinding`, `setDepthBias`, `setStencilReferenceValue` (`MTL4RenderCommandEncoder.hpp`) | **Partly unavailable** — viewport and scissor are dynamic; cull mode, winding, and depth bias are PSO state | none | **Native** — VK 1.3 |
| Embedded blend only; `separateBlendState == false` | **Native as embedded** — Metal bakes blend into the render pipeline, so the model's dynamic blend object is **unavailable**. The interface exposes only the embedded form, so nothing is load-bearing | **Native as embedded**; dynamic blend likewise **unavailable** | none | **Native as embedded**; dynamic blend available in VK 1.3 |
| Transient command buffers | **Native** — `MTL4::CommandBuffer` plus `MTL4::CommandAllocator`, begin/end per submission | **Native** — a command allocator reset per frame slot | none | **Native** — one-shot buffers from a per-frame pool |
| Index data supplied by address | **Native** — `drawIndexedPrimitives(..., MTL::GPUAddress indexBuffer, indexBufferLength, ...)` | **Native** — `D3D12_INDEX_BUFFER_VIEW::BufferLocation` | none | **Native** — `vkCmdBindIndexBuffer2` still takes a buffer handle; BDA does not cover indices |
| Indirect draw / dispatch arguments by address | **Native** — `drawIndexedPrimitives(..., GPUAddress indirectBuffer)`, `dispatchThreadgroups(GPUAddress, ...)` | **Emulated** — `ExecuteIndirect` takes `ID3D12Resource*` plus offset and a command signature | none | **Emulated** — `vkCmdDrawIndirect` takes a buffer handle |
| Copies and fills over addresses | **Emulated** — `MTL4::ComputeCommandEncoder::copyFromBuffer` / `fillBuffer` / `copyFromTexture` take `MTL::Buffer*` plus offset, so the prototype resolves address → (buffer, offset). Metal 4 has no separate blit encoder; copies live on the compute encoder | **Emulated** — `CopyBufferRegion` takes resources plus offsets | none | **Emulated** — same shape |

### 4.4 Attachments and synchronization

| Concept | Metal 4 | D3D12 | Shader constraint | Vulkan (unscored) |
|---------|---------|-------|-------------------|-------------------|
| Attachments declared at the pass boundary | **Native** — `MTL4::RenderPassDescriptor` passed to `renderCommandEncoder` | **Native** — `BeginRenderPass` / `OMSetRenderTargets` | none | **Native** — dynamic rendering (VK 1.3) |
| No implicit barrier at pass boundaries | **Native, unverified** — Metal 4's barrier model is explicit; whether any implicit inter-encoder dependency remains is settled in Stage 2 | **Native** — enhanced barriers are fully explicit | none | **Native** |
| Stage-mask barrier with no resource list | **Native** — `MTL4::CommandEncoder::barrierAfterStages` / `barrierAfterQueueStages` / `barrierAfterEncoderStages(Stages, Stages, VisibilityOptions)` (`MTL4CommandEncoder.hpp`) | **Emulated** — enhanced barriers offer `D3D12_BARRIER_TYPE_GLOBAL` with sync/access scopes, but texture layouts persist and per-resource barriers remain the documented path | none | **Emulated** — `VK_KHR_unified_image_layouts` removes layouts but keeps the resource list |
| `Hazard::Descriptors` | **Unknown** — `MTL4::VisibilityOptions` offers only `None`, `Device`, and `ResourceAlias`; no descriptor-cache flag exists. Descriptor visibility is presumed implicit. **Not load-bearing** for the frozen workloads | **Emulated** — implied by heap-copy ordering rules | none | **Native** — descriptor-buffer invalidate flag |
| `Hazard::DrawArguments` | **Emulated** — expressed as a stage-mask dependency onto `MTL::StageDispatch`/vertex stages rather than as a flag; equivalence is verified in Stage 2 (case I2/I4) | **Native** — `D3D12_BARRIER_ACCESS_INDIRECT_ARGUMENT` | none | **Native** — `VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT` |
| `Hazard::DepthStencil` | **Emulated** — stage-mask dependency onto `StageFragment` / raster depth output; no dedicated flag | **Native** — depth-stencil access bits | none | **Native** — depth stage/access bits |
| Split barrier signalling a **memory counter** (`signalAfter` / `waitBefore`, atomic max/or, wait mask) | **Emulated (load-bearing)** — Metal offers `MTLFence` via `updateFence` / `waitForFence` (`MTL4CommandEncoder.hpp`): an object, not memory; no atomic-max/or semantics and no wait mask. See §6.2 | **Unavailable** — split barriers exist (BEGIN/END) but there is no in-command-list wait on a memory value; `ID3D12CommandQueue::Wait` is queue-level | none | **Emulated** — `VkEvent` is an object; the model names its usability as the reason nobody uses it |
| Timeline semaphore for CPU pacing | **Native** — `MTLSharedEvent` plus `MTL4::CommandQueue::signalEvent` / `wait` (`MTL4CommandQueue.hpp`) | **Native** — `ID3D12Fence` | none | **Native** — VK 1.2 timeline semaphores |
| `FrameRing` three-frames-in-flight retirement proof | **Native** — userland over the timeline semaphore | **Native** | none | **Native** |

### 4.5 Residency and capabilities

| Concept | Metal 4 | D3D12 | Shader constraint | Vulkan (unscored) |
|---------|---------|-------|-------------------|-------------------|
| Single `ResidencySet` | **Native, and required** — `MTLResidencySet` with `addAllocation` / `commit` / `requestResidency` (`MTLResidencySet.hpp`), attached via `MTL4::CommandQueue::addResidencySet` or `MTL4::CommandBuffer::useResidencySet`. The comparison model has no residency concept at all, so every line of this row is target-imposed | **Emulated** — `ID3D12Device::MakeResident` / `EnqueueMakeResident` over resource lists; no set object | none | **Native (as absence)** — Vulkan requires no residency declaration, matching the model |
| `Capabilities` query | **Emulated** — assembled from several device queries plus documented constants; `bindlessSlotStride` and the alignment minima are not individually queryable and are pinned by the prototype | **Emulated** — `CheckFeatureSupport` covers some fields; alignment minima are documented constants | none | **Emulated** — `VkPhysicalDeviceProperties` plus extension property structs |

### 4.6 Debugging and failure behavior

| Concept | Metal 4 | D3D12 | Shader constraint | Vulkan (unscored) |
|---------|---------|-------|-------------------|-------------------|
| Mandatory label on every object | **Native** — `setLabel` on every Metal object, including `ArgumentTable` and `ResidencySet` | **Native** — `SetName` | none | **Native** — debug-utils object names |
| Debug groups grouping a pass in a capture | **Native** — `MTL4::CommandBuffer::pushDebugGroup` / `popDebugGroup` | **Native** — PIX events | none | **Native** — debug-utils labels |
| `LMX_ASSERT` misuse with context | **Native** — prototype-side; not an API feature on any target | **Native** | none | **Native** |
| `Result`-based creation failure | **Native** — metal-cpp creation takes `NS::Error**` | **Native** — `HRESULT` plus the info queue | none | **Native** — `VkResult` |
| Capture replays with identical GPU addresses (`capturePersistsAddresses`) | **Unknown, gate-relevant** — no public replay-address API is visible in the vendored headers; the article states Metal's debugger uses undocumented internal APIs. Settled against a real Xcode capture in Stage 2 (gate 3) | **Native** — a public recreate-at-address path exists | none | **Native** — `VkMemoryOpaqueCaptureAddressAllocateInfo` |

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
- Whether Slang's specialization constants reach MSL function constants, and whether an
  address-valued specialization constant survives (§4.1).
- Whether the two-step Slang → readable MSL → runtime compile path of ADR 0003 changes any of
  §5.1–§5.4 under the offline `metallib` path; the ADR records open Slang → metallib bugs, so the
  prototype follows the same readable-MSL route the production build uses.

## 6. Recorded tensions

Places where the model and the target visibly disagree, recorded rather than resolved silently in
the interface. Each is re-checked against the built prototype in Stage 2.

**6.1 Root data is state on Metal 4, not a call parameter.** The model's whole per-draw binding
claim is that a draw *carries* its root address. Metal 4 requires the address to be argument-table
state set before the draw, so the prototype's `draw(rootVertex, rootPixel, …)` lowers to one or two
`setAddress` calls plus the draw. The binding-traffic dimension must therefore count those calls on
the prototype side; a measurement that omitted them would flatter the model.

**6.2 Split barriers are object-based on Metal 4.** `signalAfter` / `waitBefore` over a GPU memory
counter — with atomic-max timeline semantics, atomic-or multi-producer semantics, and a wait mask —
has no Metal 4 counterpart. `MTLFence` provides ordering but not values, comparisons, or masks. If
the scored workloads need only ordering, the emulation is bounded; if any case needs a value
comparison, the prototype must record the gap rather than widen `MTLFence`'s meaning.

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
