# Platform and RHI research notes

**Status:** Frozen — non-normative  
**Research date:** 2026-08-09

These notes separate
vendor/API facts from the architectural recommendations derived from them. The final roadmap is
the normative document.

## Luminex baseline

The M3 frame is a single graphics-command-list sequence: one 2048-square directional shadow map,
one forward Blinn-Phong scene pass into `BGRA8Unorm` plus `D32`, a render-target-to-SRV barrier,
and an ImGui composition pass. Three frames are in flight. Resource binding already uses Metal 4
argument tables and bindless vertex pulling, while all live resources currently share one residency
set. The RHI deliberately has no compute dispatch, storage texture/buffer model, indirect drawing,
general stage/access barriers, timestamps, transient aliasing, multiple queues, sparse resources, or
ray tracing.

This is a good small baseline, but nearly every modern technique considered for the roadmap depends
on the same missing foundations: compute, explicit resource usages and subresources, temporal data,
GPU timestamps, and a scheduler that derives lifetimes and synchronization. Adding effects directly
to `Renderer::render` would make those dependencies harder to recover later.

## Metal 4 and Apple GPU facts

- [Discover Metal 4 (WWDC25)](https://developer.apple.com/videos/play/wwdc2025/205/) documents
  queue-independent command buffers, parallel encoding, explicit command allocators, a unified
  compute encoder for dispatch/blit/acceleration-structure work, render attachment maps, argument
  tables, residency sets, placement sparse resources, stage-to-stage barriers, flexible pipelines,
  compilation harvesting, MetalFX frame interpolation and denoised upscaling, and tensor/ML
  integration. Apple states that Metal 4 starts at M1/A14.
- The current [Metal feature-set tables](https://developer.apple.com/metal/capabilities/) are the
  source of truth for individual capabilities. Metal 4 availability does not mean every Apple GPU
  has identical barycentric, mesh, ray-tracing, sparse, or MetalFX support. Luminex therefore needs
  per-feature queries rather than one `isMetal4` branch.
- [Apple's guidance for tile-based deferred rendering](https://developer.apple.com/documentation/metal/tailor-your-apps-for-apple-gpus-and-tile-based-deferred-rendering)
  and [WWDC20's Apple-GPU talk](https://developer.apple.com/videos/play/wwdc2020/10631/) favor keeping
  data on-chip, using load/store actions deliberately, and avoiding unnecessary attachment traffic.
  This means a desktop-style wide G-buffer must not become the only opaque path without measurement.
- [MetalFX](https://developer.apple.com/documentation/metalfx) provides platform reconstruction
  implementations, but it does not remove the engine's responsibility for correct depth, jitter,
  motion vectors, exposure, masks, history resets, output sizing, and UI composition.
- [Metal fast resource loading](https://developer.apple.com/documentation/metal/loading-textures-and-models-using-metal-fast-resource-loading)
  and [sparse-texture management](https://developer.apple.com/documentation/metal/managing-sparse-texture-memory)
  provide a future path for background streaming and page residency. They do not justify virtual
  texturing before Luminex has ordinary mip streaming, budgets, and eviction telemetry.
- [Apple's Metal ray-tracing guidance (WWDC23)](https://developer.apple.com/videos/play/wwdc2023/10128/)
  covers BLAS/TLAS construction, refit, compaction, parallel builds, and ray queries from render or
  compute shaders. A shared GPU scene and explicit AS lifetimes should precede effects built on it.
- [HDR content with Metal](https://developer.apple.com/documentation/metal/hdr-content) provides the
  platform output path. Scene-linear HDR rendering and the display transform remain engine concerns.

## Cross-API implications

- Unreal's [Render Dependency Graph](https://dev.epicgames.com/documentation/en-us/unreal-engine/render-dependency-graph-in-unreal-engine)
  is production evidence for declaring pass parameters and resources, deriving transitions and
  transient lifetimes, culling dead work, merging compatible passes, scheduling async compute, and
  providing graph validation/inspection. The useful abstraction is a CPU-side frame scheduler, not
  a GPU work-graph API.
- Vulkan's [Synchronization 2 guide](https://docs.vulkan.org/guide/latest/extensions/VK_KHR_synchronization2.html)
  makes stage and access scopes explicit; its [synchronization examples](https://github.khronos.org/Vulkan-Site/guide/latest/synchronization_examples.html)
  show why resource state alone is insufficient. A portable Luminex barrier must describe producer
  stage/access, consumer stage/access, image aspect/subresource, and queue ownership where needed.
- Vulkan [descriptor indexing](https://docs.vulkan.org/samples/latest/samples/extensions/descriptor_indexing/README.html)
  and Metal argument tables both support a bindless scene, but their update and lifetime rules are
  not identical. Scene records should contain stable logical resource IDs; each backend maps IDs to
  its resident descriptor/argument-table representation.
- Microsoft's [DirectX specifications](https://microsoft.github.io/DirectX-Specs/) document enhanced
  barriers, DXR, DirectSR, and work graphs. Enhanced barriers and DXR inform future D3D12 mappings;
  work graphs are not a baseline RHI requirement because they solve a different, GPU-scheduling
  problem and have no equally general Metal/Vulkan contract.
- The Vulkan [ray-tracing guide](https://docs.vulkan.org/guide/latest/extensions/ray_tracing.html)
  exposes acceleration structures, ray queries, ray pipelines, and their synchronization. Luminex
  should begin with an optional acceleration-structure plus inline ray-query layer; a shader-binding
  table/ray-pipeline abstraction can wait for a concrete use case.

## Architectural conclusions

1. Keep the RHI thin and explicit. Put pass ordering, transient allocation, barrier derivation,
   aliasing, async eligibility, history ownership, and debug visualization in a render graph above
   it.
2. Add capabilities one feature at a time. A quality tier is a policy assembled from capabilities;
   it is not a proxy for an API name or GPU generation.
3. Represent resources by usage and view. The next format/texture additions need sampled, storage,
   color/depth attachment, copy, indirect, and acceleration-structure usages; array/3D/cube-array
   views; mip/layer/aspect ranges; and typed/raw/structured buffers.
4. Add graphics, compute, and copy as logical queues, but require a valid single-queue schedule.
   Async compute is an optimization selected only after timestamp evidence shows overlap and no
   harmful contention.
5. Preserve Apple tile locality with graph-level pass merging and load/store inference. Maintain a
   Forward+ opaque path while a compact visibility-buffer/deferred-texturing path is evaluated on
   real Apple and PC workloads.
6. Treat temporal reconstruction as an engine contract. A reference TAA/TAAU implementation is
   required even when MetalFX, FSR, DLSS, XeSS, or DirectSR adapters are available.
7. Add a second backend after the graph and temporal contracts stabilize, before Metal-only
   assumptions spread through GPU-scene, visibility, and ray-tracing code.

## Minimum RHI growth sequence

1. Formats and views: FP16 HDR, single/two-channel float and integer formats, depth-stencil,
   sRGB/unorm pairs, arrays/3D/cube arrays, and explicit subresource views.
2. Compute and transfer: compute pipelines, dispatch/direct-indirect, storage resources, copies,
   clears, resolves, and upload/readback resources.
3. Synchronization: stage/access scopes, texture layout/use, mip/layer/aspect ranges, aliasing, and
   queue hand-off.
4. Render passes: multiple color attachments, depth/stencil read/write modes, load/store/clear,
   resolves, and tile-memory hints that remain optional policy.
5. Observability: timestamps, occlusion/pipeline queries where supported, debug labels, memory
   budgets, and pipeline statistics.
6. GPU-driven work: indirect draw/dispatch, count buffers where supported, stable bindless tables,
   and backend-specific command generation hidden behind capabilities.
7. Allocation and queues: transient heaps/aliasing, background uploads, logical compute/copy queues,
   residency groups, and sparse/page mappings.
8. Hybrid rendering: BLAS/TLAS build/update/compact, inline ray queries, then optional ray pipelines.

## Local book cross-check

The local copy of Akenine-Moller et al., *Real-Time Rendering, Fourth Edition* (CRC Press, 2018)
was consulted as a fundamentals cross-check, especially chapters 5-12 (sampling, display encoding,
shadows, light/color, PBR, IBL/GI, and reprojection), 14 (volumetrics), 18-20 (measurement, culling,
LOD, deferred/tiled/clustered/deferred-texturing architectures), and 23 (hardware behavior). It
supports two important planning disciplines: image-space history needs explicit validity and
reprojection rules, and optimization decisions must be made from measured bottlenecks on the target
content and hardware.
