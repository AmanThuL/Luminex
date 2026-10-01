# Apple Metal geometry constraints: Metal 4, Apple GPUs and the Slang Metal target

**Status**: Frozen — non-normative  
**Research date:** 2026-10-01

Evidence notebook for the [geometry direction review](../2026-10-01-geometry-direction-review.md),
establishing what Metal 4, Apple GPUs and the Slang Metal target can and cannot do for a
cluster-geometry pipeline (cluster culling, mesh-shader, indirect or compute rasterization, a
visibility buffer, streaming). It was collected from the macOS SDK headers, Apple's Metal Feature
Set Tables and Metal Shading Language specification, Apple session transcripts, the Slang, wgpu,
Bevy and Unreal Engine sources, and compile and runtime probes run on the development machine.
Items marked **[UNVERIFIED]** were not confirmed against a primary source; recheck them before a
plan depends on them.

Credit: Nanite and the other Unreal Engine systems described here are the work of Epic Games.
Unreal Engine source is copyright Epic Games, Inc. and was read under the Unreal Engine EULA; this
notebook describes mechanisms in its own words and reproduces no engine code, shader text or
comments. Unreal, Unreal Engine and Nanite are trademarks of Epic Games, Inc.

## Summary

1. **The roadmap's hardware floor is wrong as worded.** Mesh shaders and ray tracing are *available*
   on every Metal 4 device (M1/A14 and later). What arrives with M3/A17 Pro (GPU family Apple9) is
   hardware acceleration of both, plus indirect mesh-draw arguments, mesh draws inside indirect
   command buffers, and a mesh grid limit of 1,048,575 threadgroups instead of 1,024.
2. **64-bit atomic min/max exists from M2 on macOS** (Apple8), on buffers and on `RG32Uint`
   textures, and MSL defines no other 64-bit atomic operation: no load, store, add or exchange.
   This is exactly why Unreal's Nanite needs an M2. It ran correctly here on the M3 Max.
3. **Slang reaches almost everything, with four traps.** Mesh, task, primitive ID, barycentrics,
   64-bit math, 32-bit atomics, framebuffer fetch and inline ray queries all compile at the pinned
   compiler. 64-bit atomics do not (Slang emits a function MSL lacks) but an eight-line intrinsic
   works. `SV_CullPrimitive` is silently dropped. A varying struct shared by a mesh and a fragment
   entry point fails to link at both the pin and the latest release. The pin miscompiles compound
   mesh primitive indices; the fix needs a bump that also changes what `SV_InstanceID` means.
4. **Metal has no multi-draw-indirect-count.** The substitutes are a CPU loop of indirect draws,
   indirect command buffers (measured here at about 692 bytes per indexed command), one draw over a
   GPU-compacted index buffer, or mesh draws with an indirect threadgroup count (Apple9 only).
5. **Local micro-probe, M3 Max:** in a synthetic, trivially shaded test, rasterizing 1.6 M to
   12.6 M pixel-sized triangles cost about the same through a mesh shader as through one indexed
   vertex-shader draw, and a compute rasterizer with 64-bit atomic max was 1.7 to 2.6 times faster
   than either at or below about 1.3 pixels per triangle and slower at about 10 pixels per
   triangle. The test's caveats are listed with its table.

## Environment and probe conventions

| Item | Value |
|---|---|
| Machine | Apple M3 Max (40 GPU cores), 64 GB, `MTLDevice.architecture.name` = `applegpu_g15s` |
| OS and tools | macOS 26.7 (25G229), Xcode 26.6 (17F113), macOS SDK 26.5 |
| Metal compiler | `Apple metal version 32023.883`; highest accepted language standard is `-std=metal4.0` (`metal4.1` is rejected although the MSL 4.1 specification is published) |
| Slang, pinned | `v2026.14.1`, released 2026-07-30 (`xmake/setup.lua`) |
| Slang, latest | `v2026.19`, released 2026-09-29, unpacked into a scratch directory |

Every result labeled **local probe** was produced on this machine on 2026-10-01 from a scratch
directory outside the repository. The Slang probes mirror the repository's shader rule exactly
(`xmake/shaders.lua`): no flags beyond target and output, entry points taken from `[shader(...)]`
attributes.

```
$SLANGC probe.slang -target metal -o probe.metal          # SLANGC = ThirdParty/slang/bin/slangc, or the 2026.19 binary
xcrun -sdk macosx metal -std=metal4.0 -o probe.metallib probe.metal
```

Hand-written MSL used the second command alone. Runtime probes are single-file Swift programs
built with `swiftc -O probe.swift -o probe && ./probe`. The scratch directory is not durable; the
load-bearing sources are reproduced under [Probe sources](#probe-sources).

## GPU families: what "M3 or later" actually gates

Apple's Feature Set Tables (dated 2026-05-21) map chips to families: A14 and M1 are **Apple7**, A15,
A16 and M2 are **Apple8**, A17 Pro, A18, M3 and M4 are **Apple9**, A19 and M5 are **Apple10**. The
Metal 4 programming model "is available as of Apple7". The SDK 26.5 `MTLGPUFamily` enum ends at
`MTLGPUFamilyApple10`. Local probe: this M3 Max reports Apple7, Apple8, Apple9, Metal3 and Metal4 as
supported and Apple10 as unsupported.

| Capability | First family | M1 | M2 | M3 and later | Source |
|---|---|---|---|---|---|
| Mesh shading (object + mesh stages) | Apple7 | yes | yes | yes, hardware-accelerated | Feature tables; WWDC22 10162 |
| Indirect mesh draw arguments | Apple9 | no | no | yes | Feature tables |
| Mesh draws in indirect command buffers | Apple9 | no | no | yes | Feature tables; tech talk 111375 |
| Threadgroups per mesh grid | Apple7 | 1,024 | 1,024 | 1,048,575 (M5: 4,194,303) | Feature tables |
| Primitive ID, barycentric coordinates | Apple7 | yes | yes | yes | Feature tables |
| Pre-raster per-vertex values in fragment shaders | Apple10 | no | no | M5 only | Feature tables; MSL 4.1 §2.19 |
| 32-bit buffer atomics | all | yes | yes | yes | MSL §6.16 |
| Texture atomics (`R32Uint`, `R32Sint`) | Apple6 | yes | yes | yes | Feature tables; MSL §6.13 |
| 64-bit atomic min/max (buffer, `RG32Uint` texture) | Apple8 on macOS, Apple9 elsewhere | no | yes | yes | Feature tables footnote 7 |
| Ray tracing API (compute and render) | Apple6 | yes | yes | yes, hardware intersector | Feature tables; tech talk 111375 |
| Placement sparse buffers and textures | Apple7 on Mac, Apple8 elsewhere | yes | yes | yes | Feature tables footnote 9 |
| Tile shaders, imageblocks, raster order groups | Apple4 | yes | yes | yes | Feature tables |

Apple's own wording on the split: the M3 announcement says the new GPU "brings hardware-accelerated
mesh shading to the Mac" and that "hardware-accelerated ray tracing comes to the Mac for the first
time" (Apple Newsroom, 2023-10-30). The developer talk for that GPU says "With hardware-accelerated
mesh shading on Apple family 9 GPUs, the most notable improvement you'll observe is much improved
performance of your existing mesh shading code", because the GPU can "much more efficiently
schedule object and mesh threadgroups to keep intermediate meshlet data on chip" (tech talk 111375).
Existing code, new speed: the API predates the hardware.

## Mesh shaders

### API in Metal 3 and Metal 4

A mesh pipeline replaces the vertex stage with an optional **object** stage and a **mesh** stage.
Both run like compute kernels in grids of threadgroups. An object threadgroup writes a payload and
calls `mesh_grid_properties::set_threadgroups_per_grid` to launch its own grid of mesh threadgroups;
each mesh threadgroup fills one `metal::mesh<V, P, NV, NP, topology>` object with up to `NV`
vertices and `NP` primitives, which goes to the rasterizer. If there is no object stage the draw
call itself sizes the mesh grid.

| Purpose | Metal 3 (macOS 13) | Metal 4 (macOS 26) |
|---|---|---|
| Pipeline descriptor | `MTLMeshRenderPipelineDescriptor` | `MTL4MeshRenderPipelineDescriptor` (function descriptors, compiled by `MTL4Compiler`; no buffer-mutability hints, no binary archives) |
| Direct draw | `drawMeshThreadgroups:threadsPerObjectThreadgroup:threadsPerMeshThreadgroup:`, `drawMeshThreads:...` | same selectors on `MTL4RenderCommandEncoder` |
| Indirect draw | `drawMeshThreadgroupsWithIndirectBuffer:indirectBufferOffset:...` | `drawMeshThreadgroupsWithIndirectBuffer:` taking an `MTLGPUAddress` |
| In an ICB | `MTLIndirectCommandTypeDrawMeshThreadgroups`, `...DrawMeshThreads` (macOS 14) | `supportIndirectCommandBuffers` on the Metal 4 mesh descriptor; `executeCommandsInBuffer:` on the Metal 4 encoders |
| Bindings | `setObjectBuffer:`, `setMeshBuffer:` and friends | `setArgumentTable:atStages:` with `MTLRenderStageObject` and `MTLRenderStageMesh` |
| Reflection | `objectThreadExecutionWidth`, `meshThreadExecutionWidth`, `maxTotalThreadsPerMeshThreadgroup`, `maxTotalThreadgroupsPerMeshGrid` | same properties on the pipeline state |

The indirect buffer holds only the **threadgroup count**
(`MTLDispatchThreadgroupsIndirectArguments`, three `uint32`); the two threads-per-threadgroup sizes
are always CPU-side arguments. Source: SDK 26.5 headers `MTLRenderPipeline.h`,
`MTLRenderCommandEncoder.h`, `MTL4MeshRenderPipeline.h`, `MTL4RenderCommandEncoder.h`,
`MTLIndirectCommandBuffer.h`.

Local probe (Swift, Metal 3 API): an object + mesh + fragment pipeline rendered 64 single-triangle
meshes into `R32Uint`; the indirect variant produced a byte-identical image; the same shaders built
into a pipeline through `MTL4Compiler` and `MTL4MeshRenderPipelineDescriptor`. Reported widths:
`objectThreadExecutionWidth` = `meshThreadExecutionWidth` = 32.

### Limits

| Limit | Value | Evidence |
|---|---|---|
| Vertices per mesh | 256 | WWDC22 10162; local probe: `mesh<V,P,257,512,...>` fails with "number of vertices (257) exceeds maximum supported (256)". Indices are `uchar` (`set_index(uint, uchar)`). |
| Primitives per mesh | 512 | Same sources; 513 fails with "number of primitives (513) exceeds maximum supported (512)". |
| Mesh output size | 32,768 bytes of vertex plus primitive data on this device | Local probe: pipeline creation fails with "Total mesh size (38912) exceeds the maximum mesh size allowed (32768)". The reported size equals `NV * sizeof(V) + NP * sizeof(P)`; indices are not counted. WWDC22 stated 16 KB, so the limit differs by family or OS. |
| Scalars across vertex and primitive types | 124 | Local probe: "Mesh unique scalar count of 125 exceeds limit of 124". |
| Payload | 16,384 bytes, less 16 if the mesh function reads the grid size and less 16 under the Xcode debugger | Feature tables, footnote 11 |
| Threadgroups per object grid | no limit | Feature tables |
| Threadgroups per mesh grid | 1,024 (Apple7, Apple8); 1,048,575 (Apple9); 4,194,303 (Apple10) | Feature tables; tech talk 111375 ("from 1,024 to over 1 million") |
| Per-draw geometry on M1 and M2 | "up to 4 GB of payload and mesh geometry per draw" | Feature tables, footnote 10 |
| Threads per threadgroup | 1,024 | Feature tables |
| Mixing with render-stage ray tracing | not compatible; only private linked functions | Feature tables, footnote 6 |

Two undocumented behaviors found by local probe (`MTL_DEBUG_LAYER=1 ./meshgrid`), both **silent**:
the draw renders nothing, the command buffer completes without error and the validation layer
prints nothing.

- With no object stage, a grid of `65535 x 1` threadgroups renders all 65,535 meshes and `65536 x 1`
  renders none; `512 x 256` (131,072) and `1024 x 1023` (1,047,552) render completely. Each grid
  dimension must stay below 65,536.
- `1024 x 1024` (1,048,576) renders nothing, which is the documented Apple9 total, enforced without
  a diagnostic. The pipeline property `maxTotalThreadgroupsPerMeshGrid` reads 1,048,576.

Footnote 10 and the "keep intermediate meshlet data on chip" remark together imply how M1 and M2
implement the feature: mesh output is spilled to device memory and then drawn. A developer measured
a 2x slowdown against plain draws on an M1 in 2022, and an Apple engineer answered: "Mesh shaders on
M1 are intended to enable use-cases that cannot be expressed as draws (such as, dynamic geometry
expansion / culling). If draws are faster then, you should probably use that path instead" (Apple
Developer Forums thread 722047). The 1,024 limit is per object threadgroup ("the maximum number of
mesh threadgroups that each object threadgroup produces can't exceed 1024", WWDC22 10162), so a
meshlet renderer on M1 or M2 needs an object stage to draw more than 1,024 meshlets per draw.

Apple's tuning guidance for Apple9 (tech talk 111375): keep the vertex and primitive types and the
`NV`/`NP` template arguments as small as the assets need, because they set memory traffic and
occupancy; and when culling per primitive, do not emit the primitive at all rather than emit it for
the fixed-function culler.

### What Epic does

Unreal's Mac shader platform enables Nanite and leaves mesh shaders off: `bSupportsNanite=true`,
`bSupportsUInt64ImageAtomics=true`, `bSupportsMeshShadersTier0=false`,
`bSupportsMeshShadersTier1=false` under `[ShaderPlatform METAL_SM6]`
(`Engine/Config/Mac/DataDrivenPlatformInfo.ini`, branches `5.8` and `ue5-main`, read 2026-10-01).
With both tiers off, no Unreal feature on Mac, Nanite's hardware rasterizer included, can select a
mesh-shader pipeline. The integration notebook covers the rest of the Mac story.

## Atomics

### What MSL offers

| Target | Types | Operations | Since | Families |
|---|---|---|---|---|
| Buffer or threadgroup memory | `atomic_int`, `atomic_uint`, `atomic_bool` | load, store, exchange, compare-exchange (weak), fetch add/sub/and/or/xor/min/max | Metal 1 to 2 | all |
| Buffer (device memory only) | `atomic_float` | add and sub, plus load, store, exchange, compare-exchange | Metal 3 (threadgroup add/sub in 4.1) | Apple7 |
| Texture (1D, 2D, 3D, arrays, texture buffer) | `int`, `uint` color type on `R32Sint`/`R32Uint` | `atomic_load`, `atomic_store`, `atomic_exchange`, `atomic_compare_exchange_weak`, `atomic_fetch_{add,and,max,min,or,sub,xor}` | Metal 3.1 | Apple6; cube maps Apple10 |
| Buffer (device memory only) | `atomic_ulong` | `atomic_max_explicit`, `atomic_min_explicit` only, both returning `void` | Metal 2.4 | Apple8 on macOS; Apple9 |
| Texture | `ulong` color type on `RG32Uint` | `atomic_max`, `atomic_min` only | Metal 3.1 | Apple8 on macOS; Apple9 |

Sources: MSL 4.1 specification §2.6, §6.13, §6.16.4.5 and §6.16.4.6; Feature Set Tables rows
"Texture atomics", "64-bit atomics" and footnote 7 ("GPU devices in the Apple8 family support
64-bit atomic minimum and maximum using ulong, on both buffers and textures, only on macOS"), and
the pixel-format footnote "You can only apply the RG32Uint format to a ulong texture on a GPU that
supports the 64-bit atomics feature". Only `memory_order_relaxed` is accepted before MSL 4.1.

Local probe, hand-written MSL: `atomic_max_explicit` and `atomic_min_explicit` on
`device atomic_ulong*` compile, and so does `texture2d<ulong, access::read_write>::atomic_max`. A
kernel calling `atomic_load_explicit`, `atomic_store_explicit`, `atomic_fetch_add_explicit`,
`atomic_fetch_max_explicit`, `atomic_exchange_explicit` or `atomic_compare_exchange_weak_explicit`
on `atomic_ulong` fails with "no matching function" at every accepted standard (`metal3.1`,
`metal3.2`, `metal4.0`). The toolchain header gates the two 64-bit functions behind
`__HAVE_ATOMIC_ULONG_MIN_MAX__`, and the specification's 64-bit section (MSL 4.1 §6.16.4.6, Table
6.28) lists only `max` and `min`. So the feature table's "full set of 64-bit atomic operations" on
Apple9 means min and max on all platforms, not a wider set. The result is read back afterwards with
an ordinary non-atomic load.

Local probe, runtime (`./probe`): 65,536 threads racing `atomic_max_explicit` into 16 `atomic_ulong`
slots, and 65,536 threads racing `texture2d<ulong>::atomic_max` into a 4x4 `RG32Uint` texture
created with `MTLTextureUsageShaderAtomic`, both produced the exact expected maxima. In the texture
the red channel holds the low 32 bits and green the high 32 bits.

### Why Nanite needs an M2

A software rasterizer resolves visibility by writing `(depth << 32) | payload` with one atomic max
per pixel: depth in the high word makes the nearest (reversed-Z: largest) fragment win, and the
payload rides along indivisibly. That takes a 64-bit atomic max and nothing else. Unreal's Metal
RHI sets `GRHISupportsAtomicUInt64 = true` only when Shader Model 6 is supported, which it defines
as GPU family Apple8 on Mac and Apple9 on iOS, on macOS 15 or iOS 18 and later, and its comment
attributes 64-bit atomic support to M2 devices; the same block forces `r.Nanite.PersistentThreadsCulling` to 0 because Apple Silicon
gives no forward-progress guarantee (`Engine/Source/Runtime/Apple/MetalRHI/Private/MetalRHI.cpp`,
`ue5-main`, read 2026-10-01). Epic's requirements page lists "Nanite Virtualized Geometry and
Virtual Shadow Maps: Apple Silicon M2+ (Beta support)" (Unreal Engine 5.8 documentation). The two
rules, Apple8 on macOS and Apple9 on iOS, are Apple's footnote 7 verbatim.

### Alternatives when only 32-bit atomics exist (M1)

| Scheme | Mechanism | Cost and risk |
|---|---|---|
| Hardware raster only | Let the fixed-function depth test resolve visibility; no atomics | No small-triangle win; this is the natural M1 path |
| Packed 32-bit | `atomic_fetch_max` on `(depth << n) \| id` in one `uint` | Depth and ID share 32 bits. A cluster-and-triangle ID needs about 25 bits at Nanite scale, leaving 7 for depth, which is unusable; workable only for tiny ID spaces |
| Two-pass depth then payload | Pass 1: 32-bit atomic max of depth bits only. Pass 2: rasterize again and store the payload where the recomputed depth equals the stored depth | Roughly twice the raster work. Needs bit-identical depth in both passes (precise floating-point mode) and a payload that fits one 32-bit store so equal-depth ties cannot tear |
| Lock word with compare-exchange | Per pixel, a 32-bit word of 8-bit counter plus 24-bit depth guards a separate payload; writers loop on compare-exchange | The `ue5-nanite-macos` author measured "either 2.5x or 5x the overhead, based on number of atomic instructions" with unbounded worst case; the port froze the GPU and was archived unfinished (README, repository archived 2024-08-16) |

Local micro-probe (see [Raster back-end micro-probe](#raster-back-end-micro-probe)): the 64-bit
atomic max cost 4% to 48% more than a 32-bit packed max in the same rasterizer, the gap growing
with pixels written per triangle. A third-party microarchitecture study reports that Apple GPUs
emulate 64-bit integer add in four cycles (reported, `philipturner/metal-benchmarks`).

## Fragment inputs for a visibility buffer

`[[primitive_id]]` (a `uint`) and `[[barycentric_coord]]` (`float`, `float2` or `float3`,
perspective by default or `center_no_perspective`) are fragment inputs since MSL 2.2 on macOS and
2.3 on iOS, on Apple7 and later (MSL §5.2.3.4; Feature Set Tables). Local probe: this device
reports `supportsShaderBarycentricCoordinates = true`.

Apple presents the visibility buffer as the intended use. Its A14 talk stores `makeID(primid,
drawid)` plus two barycentrics in the geometry pass "without the need for any additional varyings",
then reconstructs in the lighting pass by a deferred vertex fetch, and reports "we save more than
40% of our G-buffer size compared to classic deferred" (tech talk 10858). WWDC19 session 601
introduced the two attributes for the same purpose.

Semantics that decide the ID layout (local probe `./meshgrid`):

- `primitive_id` **restarts at zero for every instance and every draw**, and counts from the draw's
  first index, not from the start of the index buffer. Four triangles drawn with two instances gave
  IDs 0 to 3 in each instance; drawing the last two triangles alone gave 0 and 1.
- From a mesh shader, the fragment `primitive_id` is the **primitive's index within its mesh**
  unless the mesh function writes a `[[primitive_id]]` per-primitive field.
- Clipped triangles inherit the parent's ID (tech talk 10858).

So a cluster visibility ID cannot come from `primitive_id` alone. It needs a visible-cluster or
instance index carried as a flat varying, a per-primitive mesh output, or a per-draw constant.

No Apple-specific performance caveat for these two inputs was found. The caveat in wgpu's
documentation ("enables geometry processing ... may come with a significant performance impact on
some hardware") concerns Vulkan implementations that route primitive ID through a geometry shader.
On Apple10 only, `vertex_value<T>` gives fragment shaders the three un-interpolated vertex values
(MSL 4.1 §2.19), which would let a material shader skip the deferred vertex fetch.

## Tile-based deferred rendering levers

| Lever | What it does | Family | Under the Metal 4 model | From Slang |
|---|---|---|---|---|
| Hidden surface removal | Per-pixel visibility resolved before any fragment shading; "pixel perfect and submission order independent" | all Apple GPUs | automatic | n/a |
| Programmable blending (framebuffer fetch) | Fragment reads the current pixel's attachment values from tile memory with `[[color(n)]]` | Apple2 | unchanged | yes: `SubpassInput` with `[[vk::input_attachment_index(n)]]` lowers to a `[[color(n)]]` parameter (local probe, compiles at the pin) |
| Memoryless attachments | Attachment exists only in tile memory | Apple2 | `MTLStorageModeMemoryless` unchanged | n/a (RHI) |
| Tile shaders | Compute or fragment function run per tile mid-pass, with access to the whole tile | Apple4 | `MTL4TileRenderPipelineDescriptor`, `dispatchThreadsPerTile:` | no stage; hand-written MSL |
| Imageblocks | Custom per-pixel structs in tile memory shared by fragment and tile functions | Apple4 | `imageblockSampleLength` on `MTL4RenderPassDescriptor`, `setImageblockWidth:` | no type; hand-written MSL |
| Raster order groups | Orders fragment threads that touch the same pixel | Apple4 | unchanged (device reports support) | documented: `RasterizerOrderedTexture2D` and `RasterizerOrderedBuffer` emit `[[raster_order_group(0)]]` (not probed) |

Tile size is at most 32x32 pixels (32x16 with 4x MSAA). Explicit imageblocks are limited to 64 bytes
per pixel; implicit ones to 128 bytes. One restriction matters for single-pass designs: families
Apple3 through Apple10 "don't support memory barriers that include the `MTLRenderStages.fragment`
or `.tile` stages in the `after` argument" (Feature Set Tables, footnote 3), so a pass cannot
sample a texture it rendered earlier in the same pass; data must flow through framebuffer fetch,
imageblocks or tile memory.

### Apple's guidance

- **Draw order.** "First, opaque. Second, alpha test, discard or depth feedback. And third, and
  finally, translucent meshes"; opaque draws need no sorting among themselves (WWDC20 10602).
- **Feedback fragments.** Fragments from functions containing `discard` or writing depth are
  "feedback" fragments; "Alpha tested foliage falls into this category" (WWDC20 10632). The
  rasterizer must run their shader before it knows visibility, so they cut hidden-surface-removal
  efficiency for everything under them.
- **Depth pre-pass.** "if you only perform a depth pre-pass for performance, then hidden surface
  removal serves the same purpose on Apple GPUs", with geometry processed once and no z-fighting
  (WWDC20 10632).
- **Deferred.** Merge the G-buffer and lighting phases into one pass with programmable blending and
  make the G-buffer memoryless (WWDC19 601, WWDC20 10632); for many lights, cull per tile with a
  tile shader, since "Programmable Blending only allows us to work on a single pixel at a time"
  (WWDC20 10602). Tiled forward is described as the alternative for "complex materials,
  anti-aliasing, transparency" (WWDC19 601).
- **Passes.** "Apple GPUs achieve peak performance when they are not system memory bandwidth bound";
  load and store actions dominate bandwidth, so do not split passes (WWDC20 10632).

### Consequences for a visibility buffer (inference from the above, not an Apple statement)

- For opaque geometry, hidden surface removal already shades each pixel once in a forward pass. The
  visibility buffer's classic gain, no overdraw shading, is therefore smaller on Apple GPUs than on
  immediate-mode GPUs. What remains is decoupling material cost from triangle density and removing
  varyings from the geometry pass.
- Alpha-masked clusters are feedback fragments in a visibility pass too. The pass must fetch UVs and
  sample alpha, so it is not "thin" for them, and they should be drawn after all opaque clusters.
- A hardware-rasterized visibility ID can stay in tile memory (memoryless target read by
  framebuffer fetch or a tile shader). A compute-rasterized one cannot: it lives in a device-memory
  buffer or texture written by atomics, outside tile memory and outside hidden surface removal, and
  the two must be merged by an explicit pass or by having both write the same target. If the
  hardware path writes that shared target with fragment-shader texture atomics, one driver defect is
  on record: wgpu's Metal backend works around an Apple bug in which fragment-shader texture-atomic
  writes are intermittently dropped (reproduced with `R32Uint` atomic max from a fullscreen fragment
  pass, read back in a later pass), by following every texture atomic in generated MSL with a
  dead-code texture write, citing a Blender workaround (wgpu PR 9185, merged 2026-03-13). Whether
  Slang-generated MSL or buffer atomics need the same guard was not probed.

## Indirect execution

Metal has no equivalent of `MultiDrawIndirectCount`. wgpu documents the feature as "DX12, Vulkan
1.2+" only. What exists:

| Mechanism | Count known to | Notes |
|---|---|---|
| `drawPrimitives:indirectBuffer:` and `drawIndexedPrimitives:...indirectBuffer:` | CPU (one call per draw) | Arguments are `MTLDrawIndexedPrimitivesIndirectArguments` (index count, instance count, index start, base vertex, base instance). Culled draws become zero-instance draws. Metal 4 takes `MTLGPUAddress` values and requires the buffers in a residency set |
| Indirect command buffer | GPU, via `executeCommandsInBuffer:indirectBuffer:` reading an `MTLIndirectCommandBufferExecutionRange` | Commands encoded on the CPU or by a compute kernel (`render_command`). Metal 4 keeps it: `executeCommandsInBuffer:` on both encoders, `resetCommandsInBuffer:`, `copyIndirectCommandBuffer:`, `optimizeIndirectCommandBuffer:` on the compute encoder, and new `inherit*` raster-state flags (the per-command raster and depth-stencil state itself is Apple10) |
| One draw over a GPU-compacted index buffer | GPU (one indirect draw) | A compute pass appends the indices of surviving clusters; needs no Metal feature beyond indirect arguments |
| Mesh draw with indirect threadgroup count | GPU | Apple9 only |
| `dispatchThreadgroupsWithIndirectBuffer:` | GPU | All families; Metal 4 adds `dispatchThreadsWithIndirectBuffer:` |

Limits and costs:

- "Maximum indirect command buffer length: No limit" (Feature Set Tables). Local probe: buffers of
  16,384 and 1,048,576 commands were both created.
- **Local probe, memory per command** (`allocatedSize / maxCommandCount`, inherited pipeline and
  buffers): about **692 bytes** for `drawIndexed`, **1,276 bytes** for `drawMeshThreadgroups`, 1,573
  bytes when both types are allowed. A million-command indexed ICB allocates 725 MB. One command per
  cluster is not viable; one command per instance or batch is.
- Buffer size: local probe reports `maxBufferLength` = 41,747,087,360 bytes on this 64 GB machine
  and `recommendedMaxWorkingSetSize` = 55,662,788,608. A 4 GiB private buffer was created.
- The repository's own [ICB spike](../2026-09-15-m7.3-icb-runtime-spike.md) already showed
  GPU-written indexed ICB execution passing under a Metal 4 encoder on this machine, and GPU frame
  capture refusing with "Capturing Metal 4 Compute Command Encoder is not supported". That capture
  gap applies equally to any compute-heavy cluster pipeline.

## Streaming and memory

**Fast resource loading.** `MTLIOCommandQueue` (macOS 13, Apple2 and later) executes
`MTLIOCommandBuffer` load commands that read from an `MTLIOFileHandle` straight into a texture, a
**buffer** ("loads to a Metal buffer for streaming scene or geometry data") or CPU memory, with
concurrent or serial queues, three priorities and per-command-buffer cancellation (WWDC22 10104).
Files may be chunk-compressed with `MTLIOCreateCompressionContext(path, method, chunkSize)`; the
header lists zlib, LZFSE, LZ4, LZMA and LZBitmap, the session's example uses 64 KB chunks, and
Metal translates a byte range to the chunks it must decompress. At most 8,192 I/O commands fit one
command buffer (Feature Set Tables). Local probe: `makeIOCommandQueue` succeeds. No published
throughput number was found.

**Sparse resources.** Automatic-backing sparse textures exist from Apple6 with 16, 64 or 256 KB
pages. Metal 4 adds **placement sparse buffers and textures**: the resource is created with no
pages, and the application maps pages from a placement heap through
`MTL4CommandQueue updateBufferMappings:heap:operations:count:` (and the texture equivalent, plus
`copyBufferMappingsFromBuffer:`), ordered against rendering with queue events (WWDC25 205; SDK
header `MTL4CommandQueue.h`). `supportsPlacementSparse` is a macOS 26.4 property. Local probe: it
is true here; `sparseTileSizeInBytes` is 16,384; placement sparse buffers of 16 GiB and 256 GiB with
64 KB pages were created with `allocatedSize` 0. A cluster page pool can therefore be one large
virtual buffer whose pages are committed as geometry streams in, addressed by fixed offsets.

**Unified memory.** `hasUnifiedMemory` is true. A shared-storage buffer is the same memory for CPU
and GPU, so geometry upload is a write (or an `MTLIO` load) into the final buffer with no staging
copy; private storage still needs a blit or an `MTLIO` load. The GPU budget is system RAM: the
working-set recommendation above is about 81% of physical memory.

## Ray tracing and clusters

Metal's acceleration structures (SDK `MTLAccelerationStructure.h`, `MTL4AccelerationStructure.h`;
WWDC23 10128; WWDC25 211):

| Capability | Support |
|---|---|
| Geometry types | triangles, bounding boxes (procedural), curves (four bases, round or flat), each with a motion variant |
| Instancing | instance structures over primitive structures; multi-level instancing (instance of instance); traversal depth 32 levels for an intersector, 16 for an intersection query |
| Instance descriptors | default, user ID, motion, and GPU-filled **indirect** variants whose instance count is set on the GPU |
| Refit | `MTLAccelerationStructureUsageRefit`; refit options select vertex data and per-primitive data |
| Compaction | size query then copy-and-compact |
| Per-primitive data | `primitiveDataBuffer`, stride and element size on every geometry descriptor |
| Build controls | prefer fast build, extended limits, and in macOS 26 prefer fast intersection and minimize memory, chosen per structure |
| Metal 4 | builds, refits and copies are encoded on the unified compute encoder; intersection function buffers; address-driven builds (Apple9) |
| Hardware acceleration | intersector is fixed-function on Apple9 and later; one public measurement gives 25.20 ms on M2 versus 12.66 ms on M3 for one ray per pixel in Sponza (reported, nelari.us, 2024-09-01) |

**Nothing resembles a cluster-level acceleration structure.** A search of every Metal header in SDK
26.5 for "cluster" returns no match. Each primitive structure is built from whole vertex and index
buffers; there is no API to build many small cluster structures in one batched call and assemble
them into a bottom-level structure by reference. Ray tracing against cluster geometry on Metal
therefore means a separate, conventionally built proxy mesh per object, with refit or rebuild when
the proxy changes.

## Slang to Metal

Local probes at the pinned `v2026.14.1` and at `v2026.19`. "OK" means `slangc` succeeded and
`xcrun metal -std=metal4.0` built a metallib from its output.

| Feature | Slang construct | 2026.14.1 | 2026.19 | Notes |
|---|---|---|---|---|
| Mesh stage | `[shader("mesh")]`, `OutputVertices`, `OutputIndices`, `OutputPrimitives`, `SetMeshOutputCounts` | OK | OK | Emits `[[mesh]]` with `metal::mesh<V, P, NV, NP, topology::triangle>`. `[numthreads]` is **not** emitted; the host must pass it as `threadsPerMeshThreadgroup` |
| Object (task) stage | `[shader("amplification")]`, `DispatchMesh`, `in payload` | OK | OK | Emits `[[object]]`, an `object_data` payload and `set_threadgroups_per_grid`; object + mesh + fragment pipeline links at runtime |
| Mesh limits | 256 vertices, 512 primitives | OK | OK | 257 or 513 pass Slang and fail in the Metal compiler |
| Per-primitive ID | `SV_PrimitiveID` in `OutputPrimitives` | OK | OK | Emits `[[primitive_id]]` |
| Per-primitive culling | `SV_CullPrimitive` | **silently wrong** | **silently wrong** | Emitted as a plain `bool` field with no `[[primitive_culled]]`; compiles, links and culls nothing. No upstream issue found |
| Compound primitive index | `tris[base + 1u] = ...` | **miscompiled** | OK | Pin emits `set_index(base + 1U*3+0, ...)`; fixed by Slang PR 12886 in `v2026.17.1` |
| Mesh to fragment varyings | struct with user semantics | see matrix below | see matrix below | |
| Fragment primitive ID | `SV_PrimitiveID` | OK | OK | |
| Barycentrics | `SV_Barycentrics` | OK | OK | Emits `[[barycentric_coord]]`; a `noperspective` qualifier is dropped |
| 64-bit integers | `uint64_t` math, `RWStructuredBuffer<uint64_t>` | OK | OK | Emits `ulong` |
| 32-bit atomics, buffer | `InterlockedAdd/Max/Min/Or/CompareExchange`, `Atomic<uint>` | OK | OK | Emits `atomic_fetch_*_explicit` on `atomic_uint` |
| 32-bit atomics, texture | `InterlockedMax` on `RWTexture2D<uint>` | OK | OK | Emits `texture.atomic_fetch_max` |
| 64-bit atomics, buffer | `InterlockedMax` on `uint64_t`, `Atomic<uint64_t>.max` | **fails in Metal** | **fails in Metal** | Slang emits `atomic_fetch_max_explicit`, which MSL does not define for `atomic_ulong`: "no matching function for call to 'atomic_fetch_max_explicit'" |
| 64-bit atomics, texture | `InterlockedMax` on `RWTexture2D<uint64_t>` | **fails in Metal** | **fails in Metal** | "no member named 'atomic_fetch_max' in 'metal::texture2d<unsigned long, ...>'" |
| 64-bit atomics, byte buffer | `RWByteAddressBuffer.InterlockedMaxU64` | fails in Slang | fails in Slang | E36107 "uses features that are not available in 'compute' stage for 'metal'" |
| 64-bit atomics, workaround | `__intrinsic_asm` emitting `atomic_max_explicit` or `.atomic_max` | OK | OK | Sources below. Runtime-verified from the Slang-generated metallib for both the buffer and the texture form (`./slangatomic`) |
| Pointers | `&buffer[i]` | fails in Slang | fails in Slang | E36107; use `__ref` parameters |
| Framebuffer fetch | `SubpassInput` | OK | OK | |
| Inline ray query | `RayQuery`, `TraceRayInline`, `RaytracingAccelerationStructure` | OK | OK | Emits `acceleration_structure<instancing>` and `intersection_query`; Slang's documentation table still says "Not supported" |
| Tile shaders, imageblocks | none | n/a | n/a | No Slang stage or type; the Metal target documentation does not mention them |

Mesh-to-fragment linkage, checked by creating a real `MTLMeshRenderPipelineDescriptor` pipeline
from the generated metallibs (local probe `./linkprobe`):

| Source shape | 2026.14.1 | 2026.19 |
|---|---|---|
| One struct type shared by the mesh output and the fragment input, both entry points compiled in one invocation (the repository's build shape) | **link fails** | **link fails** |
| Shared struct, each entry point compiled in its own invocation | link fails | links |
| Two distinct struct types, indexed semantics (`TEXCOORD0`, `TEXCOORD1`) | link fails | links |
| Two distinct struct types, semantic names without a trailing index (`UVA`, `UVB`) | links | links |
| No user varyings (position, per-primitive ID only) | links | links |

The failure is Metal's "Fragment input(s) `user(TEXCOORD),user(TEXCOORD_1)` mismatching mesh shader
output type(s) or not written by mesh shader". Two causes: at the pin the mesh side spells an
indexed semantic `user(TEXCOORD0)` and the fragment side `user(TEXCOORD)` (Slang issue 12997, fixed
by PR 12885 in `v2026.18`); and in both versions, when one struct serves both stages in one
invocation, the mesh-side struct is emitted with **no** `[[user(...)]]` attributes at all. Slang
issue 12998 (open) records that the merged fix is temporary.

Upstream context: issue 12883 (2026-09-02, a Vulkan/D3D12 engine port to macOS) reported four Metal
bugs fixed in `v2026.17.1` and `v2026.18`: the compound mesh index, the mesh semantic naming,
`DispatchMesh` called from a helper or more than once, and `SV_InstanceID`. The last one is a
**behavior change**: at the pin `SV_InstanceID` lowers to `[[instance_id]]`, which includes the base
instance; from `v2026.17.1` it lowers to `[[instance_id]] - [[base_instance]]`, and
`SV_VulkanInstanceID` gives the old value (local probe confirms both emissions). The repository's
scene and shadow vertex shaders take `SV_InstanceID`, and the RHI component carries a test that
pins this lowering, so a compiler bump made for mesh shaders will trip that test by design.

Metal ray-tracing pipeline stages (intersection functions, non-inline) are still at the design
stage upstream (Slang issues 11296, 11516, 12241, 13067, all open); inline queries landed with PR
9926 (merged 2026-02-21).

## Device probe

Local probe: `swiftc -O probe.swift -o probe && ./probe`.

| Query | Result |
|---|---|
| `name`, `architecture.name` | Apple M3 Max, `applegpu_g15s` |
| `supportsFamily` | apple7, apple8, apple9, mac2, common3, metal3, metal4: true; apple10: false |
| `hasUnifiedMemory` | true |
| `maxBufferLength` | 41,747,087,360 |
| `recommendedMaxWorkingSetSize` | 55,662,788,608 |
| `maxThreadsPerThreadgroup` | 1024 x 1024 x 1024 |
| `maxThreadgroupMemoryLength` | 32,768 |
| `argumentBuffersSupport` | tier 2; `maxArgumentBufferSamplerCount` 500,000 |
| `readWriteTextureSupport` | tier 2 |
| `areRasterOrderGroupsSupported`, `supportsShaderBarycentricCoordinates`, `supportsPullModelInterpolation`, `supportsQueryTextureLOD`, `supportsBCTextureCompression` | all true |
| `supportsRaytracing`, `supportsRaytracingFromRender`, `supportsFunctionPointers`, `supportsFunctionPointersFromRender`, `supportsPrimitiveMotionBlur` | all true |
| `sparseTileSizeInBytes`; `supportsPlacementSparse` | 16,384; true |
| Compute pipeline `threadExecutionWidth` | 32 |
| Mesh pipeline `objectThreadExecutionWidth`, `meshThreadExecutionWidth` | 32, 32 |
| `max_total_threadgroups_per_mesh_grid(N)` accepted at pipeline creation | 1,024 through 4,194,303 all accepted (the attribute is not validated against the family limit) |
| Metal 4 queue, compiler, `MTL4MeshRenderPipelineDescriptor` pipeline | created |

No query exposes the mesh vertex, primitive or output-size limits; they surface only as compiler or
pipeline-creation errors.

## Raster back-end micro-probe

Local probe: `swiftc -O rasterbench.swift -o rasterbench && ./rasterbench`. This is **not** a
renderer benchmark. Geometry is a regular screen-filling grid of meshlets (63 vertices and 96
triangles each), the transform is an identity matrix multiply, the fragment shader writes one
`uint` ID to `R32Uint` with a `Depth32Float` test, nothing is culled, and every path draws the whole
scene in one call at 1920x1080. Times are the median of 30 command buffers after 8 warm-ups, from
`MTLCommandBuffer.gpuEndTime - gpuStartTime`; wall-clock commit-to-completion exceeded the GPU
time by 0.2 to 0.7 ms, more for the render paths than for the compute paths, so only the GPU
timestamps are quoted. Three runs on an otherwise idle GPU agreed within about 5% on every median; a
run that overlapped another GPU workload was 1.3 to 4 times slower, so the figures assume an idle
GPU. The hardware paths were checked pixel-identical to each other.

| Triangles | Depth layers | Mean area per triangle | Indexed draw, vertex shader | Mesh shader (64 threads) | Object + mesh | Compute, 64-bit atomic, buffer | Compute, 64-bit atomic, texture | Compute, 32-bit packed |
|---|---|---|---|---|---|---|---|---|
| 196,608 | 1 | 10.5 px | 0.062 ms | 0.069 | 0.069 | 0.096 | 0.108 | 0.065 |
| 1,572,864 | 4 | 5.3 px | 0.408 | 0.408 | 0.413 | 0.305 | 0.326 | 0.216 |
| 1,572,864 | 1 | 1.3 px | 0.475 | 0.443 | 0.447 | 0.179 | 0.192 | 0.165 |
| 12,582,912 | 1 | 0.16 px | 2.343 | (grid too wide) | 2.262 | 1.317 | 1.374 | 1.269 |

What the numbers support, and no more:

- In this test, on this Apple9 device, a mesh shader rasterized meshlets at **about the same cost**
  as an ordinary indexed draw (from 7% faster to 11% slower), and 32, 64 or 128 threads per meshlet
  made no difference. The mesh path is not a raster speedup here; its value is per-cluster culling
  inside the draw and no compacted index buffer.
- In these runs hardware raster cost tracked triangle count rather than covered pixels: the same
  1.57 M triangles cost about the same at 1.3 and at 5.3 pixels each.
- A naive compute rasterizer (per-cluster vertex transform into threadgroup memory, one thread per
  triangle, bounding-box loop, atomic max) beat the indexed draw by 2.6x at 1.3 pixels per triangle,
  1.8x at 0.16 (1.7x against the object + mesh path) and 1.3x at 5.3 pixels with four layers, and
  lost at 10.5 pixels. Its check is weaker than the hardware paths' pixel identity: full coverage
  plus the same triangle ID as the hardware image on 93.7% to 99.6% of pixels, with the rest
  attributed, not verified, to shared-edge and tie differences from its simplified fill rule; depth
  order was not checked independently.
- The indexed draw reads a 32-bit index stream (12 bytes per triangle, 151 MB in the fourth row)
  while the mesh and compute paths share one 288-byte local index table, so the comparison favors
  the latter two on memory traffic.
- The compute path includes clearing its 16 MB target (0.02 ms). A buffer target is 4% to 12%
  faster than an `RG32Uint` texture target.
- The fourth row's blank is the silent 65,536-per-dimension failure described under Limits.

It says nothing about M1 or M2, about real vertex shaders, culling, clipping (all geometry is on
screen with w = 1), overdraw from real scenes, or alpha masking.

## Published performance data on Apple GPUs

| Source | Workload | Result | Standing |
|---|---|---|---|
| Apple tech talk 111375 | Mesh shading on Apple9 | "much improved performance of your existing mesh shading code"; no numbers | Primary, qualitative |
| Apple Developer Forums 722047 (Dec 2022) | GPU-driven mesh shader path versus draw calls, M1, macOS 13 | 2x slower on M1; the same code was 1.5x faster on an RTX 3070. Apple engineer: prefer draws on M1 if faster | Reported by a developer; Apple reply is primary |
| Unreal Engine 5.8 requirements page | Nanite and virtual shadow maps on Mac | M2 or later, Beta; no frame times published | Primary, no numbers |
| `ue5-nanite-macos` README | Earlier community Nanite port | "around 15 frames per second", hardware not stated; successor froze an M1 Max | Reported |
| `light-system` README | Vulkan through MoltenVK on an M4, 1280x720, hardware visibility-buffer raster, CPU cluster selection, no mesh shaders | Stanford Dragon, 871 K triangles: 15.4 ms median. Generated city, 1 M triangles: 30.6 ms | Primary (project's own numbers) |
| Bevy meshlets, 0.15 post | Software plus hardware raster | 0.42 ms raster, 0.93 ms visibility buffer, on an RTX 3080; no Apple measurement | Primary, not Apple |
| nelari.us (2024-09-01) | One ray per pixel, Sponza | M2 25.20 ms, M1 Pro 17.56 ms, M3 12.66 ms | Reported |
| This notebook | Raster back ends, M3 Max | Table above | Local probe |

Bevy's plugin states it "currently works only on the Vulkan and Metal backends" and requires
`TEXTURE_INT64_ATOMIC` (`crates/bevy_pbr/src/meshlet/mod.rs`, `main`, read 2026-10-01). wgpu's Metal
backend enables that feature only on Apple9, and buffer 64-bit min/max on Apple9 or Apple8 Macs,
and exposes mesh shaders from Apple7 with a mesh workgroup count of 1,024, 2^20 or 2^22 by family
(`wgpu-hal/src/metal/adapter.rs`, `trunk`, read 2026-10-01). So Bevy's virtual geometry runs on M3
and later, not M2, for a library reason rather than a hardware one. No public frame-time table for
cluster culling or cluster raster on native Metal was found.

## Capability table

"RHI" is the project's Metal 4 backend, which today has no mesh-shader, 64-bit atomic, ray-tracing
or sparse-resource surface.

| Feature | M1 | M2 | M3 and later | Metal 4 name | From Slang | RHI would need | Verdict |
|---|---|---|---|---|---|---|---|
| Cluster culling in compute, indirect draws | yes | yes | yes | `drawIndexedPrimitives:...indirectBuffer:`, `dispatchThreadgroupsWithIndirectBuffer:` | yes | nothing new | The reference path on every device |
| One draw over a GPU-compacted index buffer | yes | yes | yes | same | yes | nothing new | Cheapest way to draw many clusters without mesh shaders |
| ICB with GPU-written draws | yes | yes | yes | `executeCommandsInBuffer:indirectBuffer:` | prelude only (see the ICB spike) | ICB object, encode kernels | Avoid per cluster: 692 B per command, and capture is unresolved |
| Mesh shader raster | yes, not hardware-accelerated, 1,024 groups per grid | same | yes, hardware, 1,048,575 per grid | `MTL4MeshRenderPipelineDescriptor`, `drawMeshThreadgroups:` | yes, with the varying and culling rules above | mesh pipeline descriptor, two stages in argument-table binding, draw calls, capability bits | Optional path; about the same raster cost as draws on M3 in the micro-probe |
| GPU-driven mesh draw count | no | no | yes | `drawMeshThreadgroupsWithIndirectBuffer:` | n/a | indirect mesh draw | The real "M3 or later" gate |
| Compute rasterizer, 64-bit visibility | no | yes | yes | compute pass; `atomic_max_explicit` | workaround intrinsic | a capability bit; for the texture form, `RG32Uint` with atomic usage | Feasible; pays off only near one pixel per triangle |
| Compute rasterizer, 32-bit two-pass | yes | yes | yes | compute pass | yes | nothing new | Fallback if M1 must rasterize in compute; about double the work |
| Visibility buffer from hardware raster | yes | yes | yes | `[[primitive_id]]`, `[[barycentric_coord]]` | yes | an integer color target if not present | Supported; ID needs a cluster index beside the primitive ID |
| Single-pass shading from tile memory | yes | yes | yes | framebuffer fetch, memoryless targets, tile shaders | fetch yes, tile shaders no | memoryless storage, color-input declaration, optionally tile pipelines | Apple-specific lever worth measuring in the surface-path experiment |
| Geometry page streaming | yes | yes | yes | `MTLIOCommandQueue`, placement sparse buffers | n/a | I/O queue wrapper, sparse buffer and mapping update | Native primitives exist for the later streaming milestone |
| Ray tracing against clusters | yes, not hardware-accelerated | same | hardware intersector | `buildAccelerationStructure:` on the compute encoder | inline queries yes | acceleration-structure objects and builds | Proxy meshes only; no cluster structure in Metal |

## Corrections to earlier research

- **"Mesh shaders need M3/A17 Pro"** (the cluster-geometry row of the
  [rendering direction review](../2026-09-14-rendering-direction-review.md), the meshlet and
  mesh-shader rows of `pipeline-state-of-the-art-m7-m11.md`, and the "Hardware floor" paragraph of
  the [roadmap part](../../roadmap/gpu-driven-hybrid-rendering.md), repeated in `docs/roadmap.md`
  and the neural-rendering part) is wrong. The API is Apple7
  (M1). Apple9 adds hardware acceleration, indirect mesh draws, mesh draws in ICBs and the large
  grid. The same sentence's ray-tracing half needs the same split: API from Apple6, hardware
  intersector from Apple9.
- **"`RaytracingAccelerationStructure` ... NOT supported on Metal target"**
  ([apple-metal4-and-on-gpu-ml.md](../2026-09-14-roadmap-review/apple-metal4-and-on-gpu-ml.md)) is
  stale. Inline ray queries compile to MSL at the pinned compiler; Slang's documentation table has
  not caught up. Only ray-tracing pipeline stages remain unimplemented.
- **GPU family ladder.** That notebook says `.apple10` and `.apple11` exist with no documented chip
  mapping and that M5 may report `apple9`. The SDK 26.5 enum ends at Apple10, and the Feature Set
  Tables of 2026-05-21 map A19 and M5 to Apple10. Not checked on an M5.
- **Placement sparse support** was marked "verify against Feature Set Tables". Verified: Apple8 for
  all devices, Apple7 on all Macs, queried by `supportsPlacementSparse`.
- **"Metal ICB is Apple's recommended path (WWDC25, Discover Metal 4)".** The WWDC25 session 205
  transcript, as fetched, does not mention indirect command buffers. Apple's ICB guidance is WWDC19
  session 601. ICBs remain available under Metal 4, but nothing fetched calls them the recommended
  Metal 4 path.
- **Bevy "GPU with atomic storage-texture support"** understates it: 64-bit texture atomics, which
  wgpu grants only to Apple9.
- **`MTLIOCommandQueue`** was marked unverified live. Confirmed from the SDK header, the WWDC22
  session and by creating a queue.

## Implications for Luminex

Facts first, then what I would do.

1. **Reword the hardware floor.** Suggested: mesh shaders, ray tracing and 32-bit compute raster run
   on every supported Mac; M2 adds 64-bit atomic visibility; M3 adds hardware mesh shading,
   GPU-sized mesh draws and hardware ray traversal. The development machine has all of it, so no
   slice is blocked; the wording only affects which path is called the fallback.
2. **"Optional mesh-shader execution on M3" is the right shape, for a different reason.** In the
   micro-probe the mesh path did not rasterize faster than a draw on M3. Its benefit is structural: culling in the object
   stage and no index compaction. A GPU-driven version needs the indirect mesh draw, which is the
   Apple9 gate. Epic ships Nanite on Mac with mesh shaders off. I would keep the vertex path as the
   reference, treat mesh shaders as a measured slice that must justify itself against the
   compacted-index draw, and expect a tie.
3. **Software rasterization is not blocked by Metal.** The deferral in the accepted M9 is a scope
   choice. The micro-probe suggests a real win only when triangles approach one pixel, which none of
   the current content reaches. If a later milestone adds it, the floor is M2 and the Slang side is
   one intrinsic.
4. **A mesh-shader slice has a toolchain prerequisite.** Either bump Slang to `v2026.18` or later
   (fixes compound indices and semantic naming, and changes `SV_InstanceID`, which the RHI test will
   flag; shaders that rely on the base instance switch to `SV_VulkanInstanceID`), or stay on the pin
   with rules: distinct mesh-output and fragment-input structs, un-indexed semantic names, simple
   primitive indices. Both versions need: no `SV_CullPrimitive` (compact instead, which Apple
   recommends anyway), `numthreads` duplicated on the host, grid dimensions under 65,536.
5. **Visibility ID design.** Because `primitive_id` restarts per draw and per instance, the ID is
   (visible-cluster index, triangle index). With one draw over a compacted index buffer the
   primitive ID is global to the draw and the cluster comes from a side table indexed by
   `primitive_id / trianglesPerSlot`, or from a flat varying.
6. **The surface-path experiment has an Apple-only arm.** Forward with hidden surface removal,
   visibility buffer, and single-pass deferred in tile memory are three different bandwidth
   profiles here, and alpha-masked foliage (San Miguel) is the case that separates them. Reaching
   the third from Slang is possible for framebuffer fetch and not for tile shaders.
7. **Do not plan on per-cluster ICB commands or on GPU capture of compute-encoded cluster passes**
   until the capture gap recorded in the ICB spike is resolved.
8. **Streaming has native primitives** (file-to-buffer loads with chunk compression, placement
   sparse buffers). Nothing needs inventing at the API level when geometry residency is scheduled.
9. **Ray tracing will see proxies.** Without cluster acceleration structures, M10 traces a
   conventional mesh per object regardless of what M9 rasterizes; the two milestones couple only
   through which LOD the proxy is.

## Open questions and what could not be confirmed

- Mesh-shader cost on M1 and M2 for a cluster workload. Only the 2022 forum report exists; no
  device was available.
- Whether an indirect mesh draw on Apple7 or Apple8 fails at pipeline creation, at encode, or
  silently. The header marks the method macOS 13; the feature table says Apple9.
- Why a mesh grid dimension of 65,536 renders nothing, and whether it is specific to this OS build.
  Not documented anywhere found.
- Whether the mesh output limit is 16 KB on older families (WWDC22) and 32 KB only on Apple9.
- Whether the `SV_CullPrimitive` drop and the shared-struct link failure are known upstream beyond
  issue 12998. No report was filed from this task.
- Whether the dropped fragment-shader texture atomics that wgpu works around (PR 9185) affect
  Slang-generated MSL, buffer atomics or the compute-dispatched rasterizer. Not probed.
- When a Metal compiler accepting `-std=metal4.1` ships, and whether MSL 4.1's acquire and release
  orderings change anything for a rasterizer.
- Indirect command buffers containing mesh draws, and ICB execution of mesh draws under the Metal 4
  encoder, were not probed.
- First-party frame times for Nanite on Mac. None found.
- The micro-probe's absolute numbers (several billion triangles per second) come from a trivially
  shaded, perfectly regular scene with an index-stream asymmetry between its paths and should not be
  quoted as renderer throughput; its ratios held across three idle-GPU runs and nothing more.
- How Unreal's hardware and software Nanite rasterizers share the 64-bit target on Metal was not
  read here; see the Nanite notebooks.

## Probe sources

Slang, 64-bit atomic max as written naturally (compiles in Slang, fails in the Metal compiler):

```slang
RWStructuredBuffer<uint64_t> visBuffer;
[shader("compute")] [numthreads(64, 1, 1)]
void csMain(uint3 tid : SV_DispatchThreadID)
{
    uint64_t packed = (uint64_t(asuint(1.0f / float(tid.x + 1))) << 32) | uint64_t(tid.x);
    InterlockedMax(visBuffer[tid.x & 1023], packed);
}
```

Slang, working replacements for a buffer and for a texture (same entry point, calling these). The
runtime check compares against CPU-computed values, so its metallib was built with
`-fno-fast-math`; with Metal's default fast math the GPU's `1.0f / x` differs from IEEE by one unit
in the last place on 65 of 1,024 values, which is a property of the test value, not of the atomic:

```slang
[ForceInline] void atomicMaxU64(__ref uint64_t dest, uint64_t value)
{
    __target_switch
    {
    case metal: __intrinsic_asm "atomic_max_explicit((atomic_ulong device*)($0), $1, memory_order_relaxed)";
    }
}
[ForceInline] void atomicMaxU64(RWTexture2D<uint64_t> tex, uint2 coord, uint64_t value)
{
    __target_switch
    {
    case metal: __intrinsic_asm "$0.atomic_max($1, vec<ulong, 4>($2))";
    }
}
```

Slang, the shared-struct shape that fails to link at both versions (split `V` into two types with
semantics `UVA`/`UVB` to link at the pin):

```slang
struct V { float4 pos : SV_Position; float2 uv0 : TEXCOORD0; float2 uv1 : TEXCOORD1; };
[shader("mesh")] [outputtopology("triangle")] [numthreads(1, 1, 1)]
void meshMain(OutputVertices<V, 3> verts, OutputIndices<uint3, 1> tris)
{
    SetMeshOutputCounts(3, 1);
    for (uint i = 0; i < 3; ++i) { V v; v.pos = float4(float(i), 0, 0, 1); v.uv0 = float2(0); v.uv1 = float2(1); verts[i] = v; }
    tris[0] = uint3(0, 1, 2);
}
[shader("fragment")] float4 fragMain(V input) : SV_Target { return float4(input.uv0, input.uv1); }
```

MSL, limits and 64-bit atomics (vary the two mesh template numbers; add fields to `V` for the size
limit):

```cpp
#include <metal_stdlib>
using namespace metal;
struct V { float4 position [[position]]; };
struct P { uint id [[flat]]; bool culled [[primitive_culled]]; };
[[mesh, max_total_threads_per_threadgroup(128)]]
void meshMain(metal::mesh<V, P, 256, 512, metal::topology::triangle> out, uint tid [[thread_index_in_threadgroup]]) {
    if (tid == 0) out.set_primitive_count(512);
    V v; v.position = float4(float(tid), 0, 0, 1); out.set_vertex(tid, v);
    P p; p.id = tid; p.culled = false; out.set_primitive(tid, p);
    out.set_index(tid * 3, 0); out.set_index(tid * 3 + 1, 1); out.set_index(tid * 3 + 2, 2);
}
kernel void max64(device atomic_ulong* vis [[buffer(0)]], uint3 tid [[thread_position_in_grid]]) {
    atomic_max_explicit(&vis[tid.x & 15], (ulong(tid.x) << 32) | ulong(tid.x ^ 0xABCDu), memory_order_relaxed);
}
kernel void texmax64(texture2d<ulong, access::read_write> vis [[texture(0)]], uint3 tid [[thread_position_in_grid]]) {
    vis.atomic_max(uint2(tid.x & 3, tid.y & 3), ulong4((ulong(tid.x * 4096 + tid.y) << 32) | ulong(tid.y)));
}
```

The Swift programs are plain Metal 3 host code around these shaders: `probe` prints the device
queries and checks the atomic results against closed-form maxima; `linkprobe` loads two metallibs
and calls `makeRenderPipelineState(descriptor:options:)` on an `MTLMeshRenderPipelineDescriptor`;
`meshgrid` draws one two-triangle mesh per threadgroup onto a 1024x1024 cell grid and counts
distinct IDs; `slangatomic` runs the Slang-generated workaround kernels; `rasterbench` is described
in its section.

## Sources

Apple, primary:

- Metal Feature Set Tables, dated 2026-05-21: https://developer.apple.com/metal/Metal-Feature-Set-Tables.pdf
- Metal Shading Language Specification, version 4.1, dated 2026-06-04: https://developer.apple.com/metal/Metal-Shading-Language-Specification.pdf
- macOS SDK 26.5 Metal framework headers (Xcode 26.6): `MTLDevice.h`, `MTLRenderPipeline.h`,
  `MTLRenderCommandEncoder.h`, `MTLComputeCommandEncoder.h`, `MTLIndirectCommandBuffer.h`,
  `MTLIndirectCommandEncoder.h`, `MTLAccelerationStructure.h`, `MTLIOCompressor.h`,
  `MTL4MeshRenderPipeline.h`, `MTL4RenderCommandEncoder.h`, `MTL4ComputeCommandEncoder.h`,
  `MTL4TileRenderPipeline.h`, `MTL4RenderPass.h`, `MTL4CommandQueue.h`,
  `MTL4AccelerationStructure.h`; Metal toolchain headers `metal_atomic` and `metal_mesh`
- Apple Newsroom, "Apple unveils M3, M3 Pro, and M3 Max", 2023-10-30: https://www.apple.com/newsroom/2023/10/apple-unveils-m3-m3-pro-and-m3-max-the-most-advanced-chips-for-a-personal-computer/
- Tech talk "Explore GPU advancements in M3 and A17 Pro": https://developer.apple.com/videos/play/tech-talks/111375/
- Tech talk "Discover Metal enhancements for A14 Bionic": https://developer.apple.com/videos/play/tech-talks/10858/
- WWDC22 "Transform your geometry with Metal mesh shaders": https://developer.apple.com/videos/play/wwdc2022/10162/
- WWDC22 "Load resources faster with Metal 3": https://developer.apple.com/videos/play/wwdc2022/10104/
- WWDC19 "Modern Rendering with Metal": https://developer.apple.com/videos/play/wwdc2019/601/
- WWDC20 "Harness Apple GPUs with Metal": https://developer.apple.com/videos/play/wwdc2020/10602/
- WWDC20 "Optimize Metal Performance for Apple silicon Macs": https://developer.apple.com/videos/play/wwdc2020/10632/
- WWDC20 "Optimize Metal apps and games with GPU counters": https://developer.apple.com/videos/play/wwdc2020/10603/
- WWDC23 "Your guide to Metal ray tracing": https://developer.apple.com/videos/play/wwdc2023/10128/
- WWDC25 "Discover Metal 4": https://developer.apple.com/videos/play/wwdc2025/205/
- WWDC25 "Go further with Metal 4 games": https://developer.apple.com/videos/play/wwdc2025/211/
- WWDC25 "Explore Metal 4 games": https://developer.apple.com/videos/play/wwdc2025/254/
- "Tailor your apps for Apple GPUs and tile-based deferred rendering": https://developer.apple.com/documentation/metal/tailor-your-apps-for-apple-gpus-and-tile-based-deferred-rendering
- Apple Developer Forums, "Bad mesh shader performance" (Dec 2022, with an Apple engineer's reply): https://developer.apple.com/forums/thread/722047

Unreal Engine, primary (source read on 2026-10-01; mechanisms described in this notebook's words):

- `Engine/Source/Runtime/Apple/MetalRHI/Private/MetalRHI.cpp`, branch `ue5-main`
- `Engine/Config/Mac/DataDrivenPlatformInfo.ini`, branches `5.8` and `ue5-main`
- macOS development requirements, Unreal Engine 5.8: https://dev.epicgames.com/documentation/en-us/unreal-engine/macos-development-requirements-for-unreal-engine

Slang, primary:

- Metal target documentation at `v2026.14.1` and `v2026.19`: https://github.com/shader-slang/slang/blob/v2026.19/docs/user-guide/a2-02-metal-target-specific.md
- Releases: https://github.com/shader-slang/slang/releases
- Issue 12883, "Various Metal backend errors found when porting": https://github.com/shader-slang/slang/issues/12883
- Issue 12997 and PR 12885 (mesh semantic naming): https://github.com/shader-slang/slang/issues/12997
- Issue 12998 (open design issue on varying semantics): https://github.com/shader-slang/slang/issues/12998
- PR 12886 (mesh index emission), PR 12884 (`SV_InstanceID`), PR 12887 (`DispatchMesh`): https://github.com/shader-slang/slang/pull/12886
- PR 9926, "Metal inline ray tracing support": https://github.com/shader-slang/slang/pull/9926
- Issue 11296, Metal ray-tracing umbrella: https://github.com/shader-slang/slang/issues/11296

Other engines and projects:

- wgpu `wgpu-types/src/features.rs` and `wgpu-hal/src/metal/adapter.rs`, branch `trunk`: https://github.com/gfx-rs/wgpu
- wgpu PR 9185, "Workaround Metal driver bug with atomic texture writes" (merged 2026-03-13): https://github.com/gfx-rs/wgpu/pull/9185
- Bevy `crates/bevy_pbr/src/meshlet/mod.rs`, branch `main`: https://github.com/bevyengine/bevy
- "Virtual Geometry in Bevy 0.15": https://jms55.github.io/posts/2024-11-14-virtual-geometry-bevy-0-15/
- `ue5-nanite-macos` README and `AtomicsWorkaround/README.md` (archived): https://github.com/philipturner/ue5-nanite-macos
- `light-system` README: https://github.com/usestemframework/light-system

Reported (no primary confirmation):

- `metal-benchmarks`, Apple GPU microarchitecture notes: https://github.com/philipturner/metal-benchmarks
- "A quick look at Apple Silicon's hardware accelerated ray tracing performance", 2024-09-01: https://nelari.us/post/metal-raytracing-performance/
