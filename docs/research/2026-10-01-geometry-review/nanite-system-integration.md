# Nanite system integration: shadows, ray tracing, Lumen, materials, streaming and platforms

**Status**: Frozen — non-normative  
**Research date:** 2026-10-01

Evidence notebook for the [geometry direction review](../2026-10-01-geometry-direction-review.md),
covering how Nanite couples to the rest of Unreal's frame (Virtual Shadow Maps, ray tracing, Lumen,
material shading, streaming, temporal and editor passes) and to its platforms, Apple hardware in
particular. Mechanisms were read from the `EpicGames/UnrealEngine` source on 2026-10-01
(`ue5-main` at `8b2f1e5201d0`, then `5.8` at `de7c5b3f3cff`, with `5.7` and `release` for dating)
and are described in original wording; support status comes from Epic pages fetched the same day.
Items marked **[UNVERIFIED]** were not confirmed against a primary source; recheck them before a
plan depends on them.

Source citations are a path plus a branch. Unless a branch is named, a statement was checked on
`ue5-main` and holds on `5.8` as well. The static-mesh pipeline itself (build, culling,
rasterizers, shading internals) is in [nanite-core-architecture.md](nanite-core-architecture.md);
tessellation, skinning, voxels and UE6 are in
[nanite-geometry-extensions.md](nanite-geometry-extensions.md).

Credit: Nanite and the other Unreal Engine systems described here are the work of Epic Games.
Unreal Engine source is copyright Epic Games, Inc. and was read under the Unreal Engine EULA; this
notebook describes mechanisms in its own words and reproduces no engine code, shader text or
comments. Unreal, Unreal Engine and Nanite are trademarks of Epic Games, Inc.

## One cull-and-raster service, many consumers

Nanite is exposed to the rest of the renderer as a single service that takes an array of views and
a target and returns a filled depth or visibility target. Every downstream system in this notebook
is a different configuration of that one call, which is the main reason Epic could add consumers
without touching the geometry format.

The interface (`Engine/Source/Runtime/Renderer/Private/Nanite/NaniteCullRaster.h`,
`NaniteShared.cpp`) has four parts:

- **A packed view record per view.** It holds current and previous view and projection matrices,
  the view rectangle inside the target, a clip-space scale and offset that places that rectangle,
  LOD scales derived from the rectangle size and a per-view multiplier, a rectangle for the HZB
  test, flag bits (HZB test, near clip, distance cull, uncached, reverse culling, receiver mask),
  a streaming priority category, a target layer and mip index for virtual targets, and a lighting
  channel mask. Up to 4096 views fit one pass (`NANITE_MAX_VIEWS_PER_CULL_RASTERIZE_PASS`,
  `Engine/Shaders/Shared/NaniteDefinitions.h`).
- **A raster context.** It chooses the output: a 64-bit visibility target (depth plus cluster and
  triangle identity) or a 32-bit depth-only target, optionally an externally owned depth array
  (the shadow page pool).
- **A configuration.** Flags name the consumer: two-pass occlusion, update streaming, shadow
  pass, scene capture, reflection capture, Lumen capture, material cache, draw only root
  geometry, force hardware raster, disable programmable raster, extract shadow performance
  feedback.
- **The instance source.** Either the whole GPU scene, a scene instance-culling query, or an
  explicit list of (instance, view) pairs.

Every candidate node, candidate cluster and visible cluster carries a view index. LOD selection,
frustum and occlusion tests run per (cluster, view) pair, and the rasterizers scissor each
triangle to its view's rectangle. In the hardware path the scissor is done by hand in the pixel
shader, because one draw contains clusters from many views.

| Consumer | Views | Output | Instance source | Notes |
|---|---|---|---|---|
| Main view | 1 (a two-view instanced-stereo path exists in code) | Visibility | Whole scene | Two-pass occlusion, drives streaming |
| Virtual Shadow Maps | One per clipmap level or per local-light mip | Depth only, into the page pool | Culling query | Page-aware culling and raster |
| Classic shadow atlas, cascades, cube maps | One per shadow in the atlas | Depth only, private target | Whole scene, filtered by mobility | Copied into the atlas afterwards |
| Lumen card capture | One orthographic view per card page | Visibility, atlas with one rectangle per view | Explicit (instance, view) list | Programmable raster disabled |
| Editor selection | 1 | Visibility, private target | Explicit list of selected instances | Feeds the outline |
| Scene and reflection captures, custom depth, material cache | Varies | Either | Varies | Flag bits only |

Sources: `NaniteCullRaster.h`, `VirtualShadowMaps/VirtualShadowMapArray.cpp`,
`ShadowDepthRendering.cpp`, `Lumen/LumenSceneRendering.cpp`, `Nanite/NaniteEditor.cpp`, all under
`Engine/Source/Runtime/Renderer/Private/`.

## Virtual Shadow Maps

### Shape of the system

A Virtual Shadow Map (VSM) is a 16384 by 16384 texel virtual depth map per light, backed by 128
by 128 texel physical pages allocated only where the camera needs them. Constants from
`Engine/Shaders/Shared/VirtualShadowMapDefinitions.h`: page size 128, 128 by 128 pages at level
0, 8 mip levels, a raster window of 4 by 4 pages. `r.Shadow.Virtual.MaxPhysicalPages` defaults to
2048 (`VirtualShadowMapArray.cpp`), which is 128 MiB per 32-bit array slice by arithmetic.
Directional lights use a clipmap of such maps, levels 6 through 22 by default
(`VirtualShadowMapClipmap.cpp`), each covering twice the radius of the previous one; spot lights
use one map with mips; point lights use six (Epic VSM documentation for 5.8).

Each frame, a marking pass projects visible depth samples into every light and flags the pages
they land in; an allocation pass maps flagged pages to physical pages, reusing cached ones; then
geometry is rendered only into pages flagged as uncached. Nanite is one of two geometry sources
for that last step.

### How Nanite renders into pages

**Views.** `AddRenderViewsClipmap` and `AddRenderViewsLocal` create one packed view per clipmap
level, or per local-light face, each with a 16384-texel rectangle and the layer index of its
virtual map. A compute pass (`VirtualShadowMapCompactViews.usf`) then expands each of these
primary views into one view per mip, halving the rectangle and LOD scale each time, but only for
mips whose rectangle of uncached pages is non-empty. The cull pass therefore only sees views
with work. The view's pixels-per-edge multiplier is the inverse of a shadow LOD scale factor, so
shadow LOD is chosen in shadow texels, independently of the camera.

**Culling against pages.** With the virtual-target permutation, the shared box-cull routine
(`Engine/Shaders/Private/Nanite/NaniteCullingCommon.ush`) does three extra things after the
frustum test:

1. It converts the projected bounds to an inclusive rectangle of pages and rejects the node or
   cluster unless at least one overlapped page carries the flags this geometry may write
   (`OverlapsAnyValidPage`, `VirtualShadowMapPageOverlap.ush`). Page flags are kept in a mip
   hierarchy, so the test is one 2 by 2 gather at the mip matching the rectangle size.
2. It picks which pages are legal by cache class. An instance classified as static may only
   write pages whose static layer is uncached; a dynamic instance only the dynamic layer.
3. It tests occlusion against a per-page HZB instead of a screen HZB. The HZB is built only for
   physical pages (7 levels, one per power of two of the page size), and looked up through the
   page table, last frame's table for the main pass and this frame's for the post pass.

**Cluster emission.** A visible cluster is emitted with the page rectangle it covers. A cluster
headed for the software rasterizer is emitted once per 8 by 8 page block; one headed for the
hardware rasterizer once per 4 by 4 page raster window (`NaniteClusterCulling.usf`). While
looping over the pages, the same shader writes dirty flags for each page it will touch, which is
how invalidation is recorded without a second pass.

**Raster.** If the cluster covers exactly one page, the rasterizer translates the triangle setup
into that physical page and scissors to it. Otherwise the software rasterizer stays in virtual
space and translates each pixel through a small per-thread-group cache of page-table entries;
the hardware path draws into a 512-texel viewport that represents the raster window and its pixel
shader translates each fragment to a physical address (`NaniteRasterizer.usf`). In both cases
the depth write is an atomic maximum on the page pool, bound as a storage texture
(`NaniteWritePixel.ush`). There is no depth attachment in the hardware path.

**Occlusion.** `r.Shadow.Virtual.UseHZB` (default 1) enables the same two-pass scheme the main
view uses, against the per-page HZB.

### Why VSM leans on Nanite

The cost model only works if geometry can be clipped to pages and rendered once for many views.
Nanite gives it four things non-Nanite geometry cannot:

- Cluster-granular culling against the page mask. A large mesh that touches one dirty page draws
  only the clusters over that page.
- LOD chosen per shadow view in shadow texels, so triangle count tracks page resolution.
- One cull-and-raster pass for all views of all lights in a batch, where the non-Nanite path
  issues draws per light and per level.
- Two-pass occlusion in light space.

Epic's documentation puts it this way: "Non-Nanite geometry is much more expensive to render
into VSMs than Nanite geometry", non-Nanite meshes without LODs "become extremely expensive to
render into small pages", and "Multiple local lights will perform far better with Nanite geometry
than non-Nanite geometry."

### Non-Nanite geometry

Non-Nanite meshes go through a separate path that reuses the same culling struct but can only
cull whole instances. A compute pass (`VirtualShadowMapBuildPerPageDrawCommands.usf`) loops over
instances and views, tests each against the page flags and optionally the page HZB, and emits
one instance record per (instance, mip view) that overlaps a page needing a render. Ordinary
mesh draw commands then run with those instance lists; the pixel shader does the
virtual-to-physical translation. An instance that overlaps one dirty page draws all of its
triangles at its regular LOD. The buffer for emitted records is sized for the worst case of every
instance in every level and then scaled down by a console variable, with a stated 1.5 GB clamp
(`r.Shadow.Virtual.NonNanite.MaxCulledInstanceAllocationSize`). Projects can compile the path out
with `r.Shadow.Virtual.NonNanite.ProjectEnabled`
(`Engine/Source/Runtime/RenderCore/Private/RenderUtils.cpp`).

### Caching and invalidation

Each physical page keeps two depth layers when static caching is on: one for geometry that has
not moved for `r.Shadow.Virtual.Cache.FramesStaticThreshold` frames (default 100) and one for
the rest. Moving geometry dirties only the dynamic layer; the layers are merged for sampling.

Invalidation has two sources, and Nanite participates in both:

- **CPU-known changes.** Added, removed or moved instances are projected into every cached light
  by a GPU pass that clears the cached flag on the pages their bounds cover
  (`VirtualShadowMapCacheGPUInvalidation.usf`), optionally skipping instances hidden by the HZB.
- **Render-time flags.** During Nanite cluster culling, a cluster whose material animates
  vertices, or whose instance is deforming, marks the pages it touches so they are redrawn next
  frame. Bounds are expanded by the maximum vertex offset even when the offset is disabled by
  distance, so a later re-enable cannot leave stale texels.

Streaming creates a third case. A page rendered while the wanted cluster detail was not yet
loaded holds coarser geometry than it should. `r.Nanite.VSMInvalidateOnLODDelta` (default 0,
labeled experimental, present on `5.8` and `ue5-main`) lets cluster culling flag such pages on a
separate channel, consumed under `r.Shadow.Virtual.DeferredInvalidationBudget` pages per frame so
a slow disk does not turn into a redraw storm. With it off, cached pages keep the coarse result
until something else invalidates them.

A view flagged as uncached skips the split and writes everything to the dynamic layer. A
"receiver mask" option restricts dynamic geometry to the sub-page regions that visible receivers
requested (8 by 8 cells per page).

### Classic shadow maps use the same service

Nanite also renders ordinary cascades, spot atlases and point-light cube maps
(`RenderShadowDepthAtlasNanite`, `ShadowDepthRendering.cpp`). One cull-and-raster pass is issued
per atlas with one packed view per shadow, each with its own rectangle; output is a private 32-bit
depth target the size of the atlas. A full-screen pass per shadow then copies depth into the real
atlas, converting it to the atlas convention and adding bias
(`Engine/Shaders/Private/Nanite/NaniteEmitShadow.usf`, three depth conventions: unchanged,
orthographic, perspective). Cube maps emit one face at a time. Shadow maps that cache static
casters and those that hold movable casters are drawn in separate passes with a mobility filter
on the instances. So `NaniteEmitShadow.usf` belongs to the classic path; VSM needs no copy
because it rasterizes straight into the page pool.

### What to design in advance

A renderer that ships cluster geometry before shadows keeps all three shadow designs open if the
cluster path has these properties from the start:

1. **Views are data.** Culling takes an array of view records and every candidate and visible
   cluster carries a view index. A cascade set is N orthographic views; an atlas is N views with
   sub-rectangles of one target.
2. **Per-view rectangle and scissor**, enforced in the shader when many views share a draw.
3. **A depth-only output mode** with no payload, separate from the visibility or color mode.
4. **LOD scale per view**, in that view's pixels, with a per-view multiplier.
5. **An occlusion source per view**, not one global screen HZB.
6. **An optional per-cluster target rectangle** in the visible-cluster record. Unused by cascades
   and atlases; it is the hook a page cache needs.
7. **A static or dynamic classification per instance**, visible to the cull shader, so cached
   targets can skip static geometry.
8. **Depth convention conversion outside the rasterizer**, as Unreal does with a copy pass, or a
   depth attachment per target if the hardware path is the only path.

In this notebook's judgment, items 1 to 5 cost little if done first and are expensive to
retrofit, because the view index changes the candidate and visible-cluster record layouts.

## Ray tracing

### Three modes

`r.RayTracing.Nanite.Mode` selects how Nanite meshes appear in the ray tracing scene
(`Engine/Source/Runtime/Engine/Private/Rendering/NaniteResources.cpp`).

| Mode | What is traced | Where it works | Status |
|---|---|---|---|
| 0, fallback mesh (default) | The fallback mesh built offline at a fixed relative error; an ordinary BLAS | Every ray tracing platform | Default. The Nanite page for 5.8 says the fallback mesh is used "by default" |
| 1, streamed-out mesh | A view-independent cut of the cluster hierarchy, decoded on the GPU into flat vertex and index buffers and built into an ordinary BLAS per mesh | No platform gate found in the Nanite code | The same page calls it "(Experimental) Initial support for native ray-tracing of Nanite meshes" |
| 2, cluster acceleration structures (CLAS) | One small acceleration structure per cluster, kept for resident pages; a BLAS per mesh is assembled from the clusters of the chosen cut | Only where the RHI reports cluster operations: NVIDIA through NVAPI on D3D12 | New in 5.8; not in the 5.8 documentation pages fetched |

Mode 2 falls back to mode 0 with a warning when cluster operations are missing
(`GetRayTracingMode`, same file). The `5.7` branch lists only modes 0 and 1; `5.8` adds mode 2.

### Stream-out (mode 1)

Introduced on 2022-09-27 (commit history of `NaniteRayTracing.cpp`). The mechanism
(`NaniteRayTracing.cpp`, `NaniteStreamOut.cpp`, `Engine/Shaders/Private/Nanite/NaniteStreamOut.usf`):

1. When streaming installs or removes pages of a mesh, the mesh is queued for update.
2. A compute traversal walks that mesh's hierarchy with a fixed object-space error threshold
   (`r.RayTracing.Nanite.MinCutError`), not a view. A first run counts vertices and indices per
   material segment; an allocation pass reserves ranges with atomic adds; a second run decodes
   cluster positions and indices into shared vertex and index buffers.
3. The same pass writes one word per triangle recording page, cluster and triangle index.
4. A small header per mesh is read back. On arrival, the CPU creates the geometry, copies the
   per-triangle words into a persistent buffer and schedules the BLAS build.

Budgets: 16 M vertices and 64 M indices per frame, 8 M triangles of BLAS builds per frame, a
1024 MB staging buffer. Because instances share one BLAS, detail is per mesh, not per instance:
a "reference instance" error is computed per resource from all instances in the ray tracing
scene, with a looser target for off-screen instances (LOD bias 1, minimum cut error 4).

### Cluster acceleration structures (mode 2)

Unreal's RHI has a "multi-indirect cluster operation" with five kinds: build CLAS from
triangles, build CLAS templates, instantiate templates, move, and build a BLAS from CLAS addresses
(`Engine/Source/Runtime/D3D12RHI/Private/D3D12RayTracing.cpp`). It maps one-to-one onto NVAPI's
D3D12 cluster calls, and support is detected by asking NVAPI for the cluster-operations
capability (`WindowsD3D12Device.cpp`). Only `PCD3D_SM6` sets `bSupportsRayTracingClusterOps`
(`Engine/Config/Windows/DataDrivenPlatformInfo.ini`). A code search of the default branch found
no cluster-operation code under the Vulkan or Metal RHI.

The Nanite side (`NaniteRayTracing.cpp`, `NaniteClusterRayTracing.usf`):

1. **Per page, on install.** Each cluster of a newly resident page is decoded to scratch vertex
   and index buffers (256 vertices and 128 triangles reserved per cluster), plus a per-triangle
   geometry index so material sections survive. One cluster operation builds all CLAS into a
   staging buffer and returns their sizes. A compute allocator hands out fixed-size blocks
   (16384 bytes by default) from a persistent pool (512 MB by default), and a move operation
   relocates the CLAS there. Allocation results are read back for bookkeeping.
2. **Per frame.** A hierarchy traversal per mesh, or per assembly part, picks the cut using the
   reference-instance error and writes the addresses of the matching CLAS; a second cluster
   operation builds one BLAS per mesh from those addresses. A cache (64 MB) reuses a BLAS while
   the wanted error stays within a relative tolerance (0.5).
3. **Hit shading.** The hit shader gets the cluster identity from an NVAPI intrinsic, which
   encodes page and cluster index, and the primitive index is the triangle within the cluster.
   In mode 1 the per-triangle word plays that role. Either way the shader decodes the three
   positions from Nanite page data (`NaniteRayTrace.ush`,
   `Engine/Shaders/Private/RayTracing/RayTracingHitGroupCommon.ush`).

Work on `ue5-main` dated 2026-09-25 and 2026-09-26, absent from `5.8`, makes this
demand-driven: the traversal tags the streaming requests it emits with a ray tracing bit
(`NANITE_STREAMING_REQUEST_FLAG_RAYTRACING`), CLAS are built only for pages that carry the tag,
reclaimed when the tag lapses (128 pages per update), and a quality scale lowers ray tracing LOD
when pool demand passes 85 percent and raises it below 70 percent. A cluster flag marks a cluster
as a leaf of the ray tracing cut when its children have no CLAS yet.

### Relation to RTX Mega Geometry and NvRTX

CLAS and BLAS-from-clusters are the D3D12 form of NVIDIA's RTX Mega Geometry. NVIDIA's OptiX 9
article (2025-04-24) defines a CLAS as an acceleration structure over a small user-defined
triangle cluster that becomes the input of a cluster-level geometry structure, with reusable
templates for repeated topology; it says templates can speed up a rebuild "by an order of
magnitude" over one flat structure, and that the structure built over CLAS has about 100 times
fewer entries than one built directly over micro-triangles. NVIDIA's RTX branch page lists NvRTX
branches for 5.4 through 5.7 and a 5.8 preview, and says the experimental branch
(`nvrtx-5.4_zorah_experimental`) features Mega Geometry "for real-time path tracing of Nanite
geometry". Press coverage reports an experimental release in NvRTX 5.6 (reported, not confirmed
on an NVIDIA page here).

The new fact from source: Epic's own `5.8` branch contains the NVAPI cluster path and the Nanite
mode that uses it, landed on `ue5-main` on 2026-02-04. It is no longer NvRTX-only.

### Two dormant pieces and two naming traps

- `NaniteRayTrace.ush` has a procedural-primitive path that walks the cluster hierarchy inside
  an intersection routine with a small stack, a per-cluster box test and a watertight triangle
  test. It is behind a define that defaults to 0, and a code search found no other reference.
- The builder computes a compressed 8-wide BVH per cluster with Embree (80-byte nodes, at most 3
  triangles per leaf), but the code that would store it is compiled out
  (`Engine/Source/Developer/NaniteBuilder/Private/Encode/NaniteEncodeRayTracing.cpp`).
- `NaniteBuilder/Private/NaniteRaytracingScene.cpp` is not runtime ray tracing. It wraps a CPU
  Embree scene of clusters for the offline builder, used by voxel and assembly code.
- `NaniteClusterRayTracing.usf` is not a software ray tracer. It holds the compute passes for
  mode 2: cut traversal, per-cluster stream-out, the block allocator and BLAS compaction.

### On Metal

`METAL_SM6` declares ray tracing, inline ray tracing, ray tracing shaders, procedural primitives
and path tracing (`Engine/Config/Mac/DataDrivenPlatformInfo.ini`), and Epic's macOS requirements
page lists Lumen hardware ray tracing and MegaLights as Experimental on Apple Silicon M2 and
later. Mode 2 cannot run: Unreal's Metal RHI has no cluster-operation path, and the Apple
notebook finds no cluster-level acceleration structure anywhere in the Metal SDK. Mode 0 is the
default and needs nothing from Nanite at runtime. Mode 1 has no Metal-specific exclusion in the
code read; whether it works on Metal was not tested **[UNVERIFIED]**.

## Lumen

**Card capture.** Lumen's surface cache stores albedo, normal, emissive and depth for "cards",
axis-aligned orthographic captures placed around each mesh offline (12 per mesh by default per
the Lumen documentation). When a card page needs a capture, Lumen builds one orthographic packed
view per page, with the page's rectangle in a capture atlas, and an explicit list of (instance,
view) pairs; it calls the Nanite renderer in batches of up to 4096 views into one visibility
atlas, with programmable raster disabled, and then runs a dedicated material pass
(`ENaniteMeshPass::LumenCardCapture`, a compute pass with its own shading bins) that writes the
three attributes (`Lumen/LumenSceneRendering.cpp`, `Lumen/LumenSceneCardCapture.cpp`). LOD is
chosen in card texels (`r.LumenScene.SurfaceCache.NaniteLODScaleFactor`, default 1). Lighting is
computed later on the cache, not during capture.

The documentation states the dependency: Lumen "relies on Nanite's Level of Detail (LOD) and
Multi-View rasterization for fast scene captures", does not require Nanite, but "becomes very
slow in scenes with lots of high polygon meshes that have not enabled Nanite"; foliage and
instanced static meshes are supported only with Nanite.

**Software tracing does not use Nanite triangles.** It traces per-mesh signed distance fields
for the first two meters and a merged global distance field beyond, covering 200 m from the
camera, and looks lighting up in the surface cache at the hit. Thin geometry and large single
meshes are named limitations.

**Hardware tracing** uses whichever representation the ray tracing mode selects, the fallback
mesh by default, with surface-cache lighting or optional hit lighting for reflections. The
documentation says screen traces "cover the mismatch between the full triangle meshes rendered by
Nanite and the Fallback Mesh".

## Materials and shading

### Binding

Material evaluation for a Nanite pixel is resolved through two tables. Each cluster stores
material ranges that map a triangle index to a mesh-local material slot. A per-primitive table,
uploaded by a scene extension, maps (primitive, mesh pass, slot) to a raster bin and a shading
bin; the passes are base pass, Lumen card capture and material cache
(`Nanite/NaniteMaterialsSceneExtension.h`). A depth-export pass reads the visibility target and
writes scene depth, stencil, velocity and a per-pixel shading mask holding the shading bin, a
shading rate, a decal-receiver bit, a has-ray-tracing-representation bit, a cast-shadow bit and
lighting channels (`NaniteDepthExport.usf`, `NaniteExportGBuffer.usf`).

Shading runs as compute. A count, reserve and scatter sequence sorts 2 by 2 pixel quads by
shading bin; then one indirect compute dispatch per bin evaluates that material and writes the
G-buffer targets through storage bindings (pass name `ShadeGBufferCS` on both branches,
`Nanite/NaniteShading.cpp`). Three dispatch methods exist: a shader bundle submitted natively, an
emulated bundle, and D3D12 work graphs (`r.Nanite.AllowWorkGraphMaterials`). A bindless variant
groups bins that share a shader into one dispatch and selects the material's constants by
descriptor index; it is opt-in per shader platform (`UseNaniteBindlessShading`, `RenderUtils.cpp`).
Software variable-rate shading (`r.Nanite.SoftwareVRS`) falls out of the quad binning.

### Substrate

With Substrate's adaptive G-buffer, the shading compute pass writes Substrate's material texture
array and top-layer texture through storage bindings in place of the classic targets
(`CreateNaniteShadingPassParams`, `NaniteShading.cpp`). The visibility buffer and binning are
unchanged, so the coupling is confined to the output layout.

### Forward shading is not supported

`DoesRuntimeSupportNanite` returns false whenever forward shading is enabled for the shader
platform (`RenderUtils.cpp`, both branches), and Epic's Nanite page for 5.8 lists "Forward
Rendering", "Stereo rendering for Virtual Reality", MSAA and lighting channels as unsupported.
Every Nanite shading pass registers under the deferred shading path. No forward-lit opaque path
exists in the Nanite source. With Nanite off, Nanite-enabled meshes render their fallback mesh
through the normal pipeline (`r.Nanite.ProxyRenderMode` 0).

Consequences in Unreal: no Nanite in the desktop forward renderer, in VR or in the mobile
renderer. `MobileShadingRenderer.cpp` on `ue5-main` contains no reference to Nanite, and the
Android shader platforms carry no Nanite flag.

One exception shows the limit is not structural. Nanite translucency (`r.Nanite.AllowTranslucency`,
default 0 on both branches; described as heavy work in progress on `5.8`) draws culled
translucent cluster bins with the hardware path only, using a dedicated vertex factory whose
vertex or mesh shader decodes clusters and whose pixel shader is the material's ordinary
translucency base-pass shader (`Nanite/NaniteTranslucency.cpp`). Unreal's translucency is forward
lit, so this is cluster geometry feeding a forward-lit pixel shader directly, with no visibility
buffer in between. It is in the extensions notebook's scope; it matters here as proof that the
decode-in-vertex-stage path composes with forward shading.

What blocks opaque forward in Unreal is specific to Unreal: the forward renderer's MSAA has no
equivalent in a one-sample visibility target, lighting channels are not carried, and nobody wrote
the shading pass that would evaluate material and lighting together. The pieces it would need
are present: depth, a shading mask and per-pixel material evaluation.

## Streaming

`NaniteFeedback.cpp` is not the streaming feedback path. It is a development-only GPU-to-CPU
message channel that reports peak use of the node, candidate-cluster and visible-cluster buffers
and logs overflow warnings. Streaming requests take the route below.

### Request generation on the GPU

During hierarchy traversal in any pass that has "update streaming" set, each visited child that
is a hierarchy leaf (it refers to a cluster group, loaded or not), is visible, and whose LOD
test says its detail is wanted calls `RequestPageRange` (`NaniteStreaming.ush`, called from
`NaniteClusterCulling.usf`). A request is
three words: runtime resource id, a page-range key, and a priority. The key encodes a start page
and count, or an index into a multi-range table, plus a bit saying the range contains streaming
pages at all, so fully resident ranges never emit requests. Priority puts the view's streaming
category in the top two bits and the bits of a float below it; the float is the ratio of the
node's error threshold to its projected size, so nodes further past the cut rank higher.
Requests are appended with one atomic add on a counter in element 0. The buffer holds between
64 K and 1 M entries and is resized from observed demand (`NaniteReadbackManager.cpp`); overflow
drops requests.

Only visible geometry requests pages, because the call sits after the frustum and occlusion
tests. Shadow passes also request (`r.Shadow.NaniteUpdateStreaming`), with VSM views forced to
priority category zero.

### Readback and selection on the CPU

`FStreamingManager` (`Engine/Source/Runtime/Engine/Private/Rendering/NaniteStreamingManager.cpp`)
runs once per frame:

1. Queue a readback of this frame's request buffer and lock the newest completed one of four
   buffers, so the CPU works from requests one or more frames old.
2. On a worker thread, expand each request into pages and record the maximum priority per page.
   Pages already resident go to the back of an LRU list. Pages not resident become new requests.
3. Add every dependency of a requested page, recursively, at the child's priority plus one, so a
   parent always outranks its children and is never evicted before them.
4. Take the highest-priority new pages from a heap, up to the free slots in the pending queue
   (128). For each, claim the least recently used resident page that was not referenced this
   update and has no dependents, start an asynchronous read into a staging ring buffer, and
   register the new page in that slot. If no slot qualifies, stop.
5. Requests may also come from explicit calls and from prefetch (`PrefetchResource`), merged in
   the same step.

### Install and eviction

Completed reads are installed at the start of a later frame, at most 128 per frame
(`InstallReadyPages`). Install uninstalls the page occupying the slot, copies the page bytes to
an upload buffer, and applies fixups: small patches to hierarchy nodes and to clusters in other
pages that flip "is a leaf of the resident cut" as children arrive or leave. Uploads and patches
are queued through an ordered scatter updater so several writes to one address resolve as if
serial. A GPU transcode pass then expands the page, reading its dependency pages for pages that
use relative encoding. The pass that rendered with old data and the pass that sees new data are
separated by a frame boundary; no sub-frame synchronization is needed.

There is no eviction pass. A page stays until its slot is reused or its resource is removed.

### What is always resident, and budgets

- **The hierarchy.** All BVH nodes of every registered resource live in one GPU buffer from
  registration (`FStreamingManager::Add`). Culling can always traverse, and learns from the node
  whether children are loaded.
- **Root pages.** Each resource has root pages holding its coarsest clusters, always resident.
  Root pages are 32 KiB, streaming pages 128 KiB (`NaniteDefinitions.h`); the initial root
  allocation is 2048 pages and grows on demand.
- **The streaming pool.** `r.Nanite.Streaming.StreamingPoolSize` defaults to 512 MB, which is
  4096 streaming pages by arithmetic, in the same GPU buffer as the root pages.

| Budget | Default | Variable |
|---|---|---|
| Streaming pool | 512 MB | `r.Nanite.Streaming.StreamingPoolSize` |
| Pending pages | 128 | `r.Nanite.Streaming.MaxPendingPages` |
| Installs per frame | 128 | `r.Nanite.Streaming.MaxPageInstallsPerFrame` |
| Bandwidth | Unlimited | `r.Nanite.Streaming.BandwidthLimit` |
| Quality scale thresholds | Reduce quality above 85 percent pool load, restore below 70, floor 0.3 | `r.Nanite.Streaming.QualityScale.*` |
| Fallback (coarse) mesh and its BLAS | 220 MB | `r.Nanite.CoarseStreamingMeshMemoryPoolSizeInMB` |

The quality scale is the pressure valve: when unique requested pages exceed 85 percent of the
pool, a global factor relaxes the LOD target so the visible set fits. Defaults match on `5.8`.

## Platform support

### Matrix

| Platform | Nanite in source configuration | Mesh-shader raster | Official status |
|---|---|---|---|
| Windows D3D12, SM6 | Yes | Yes (tiers 0 and 1) | Supported; "DirectX 12 with Shader Model 6" (Nanite page, 5.8) |
| Windows D3D11 or SM5 | No | n/a | Unsupported |
| Vulkan desktop, SM6 | Yes | Yes | VSM page lists Linux with GeForce 2080 or newer |
| Vulkan desktop, SM5 | No | n/a | Unsupported |
| PlayStation 5, Xbox Series | Not in the public repository | Primitive or mesh shaders **[UNVERIFIED]** | Supported (VSM page) |
| Mac, `METAL_SM6` | Yes | No (both tiers false) | Beta, Apple Silicon M2 or later (macOS requirements page, 5.8) |
| Mac, `METAL_SM5` | No | n/a | Unsupported |
| iOS, `METAL_SM6_IOS` | Yes | No | SM6 on iOS is Experimental in 5.8 (release notes) |
| iOS and Mac mobile profiles, Android (all profiles) | No flag | n/a | No Nanite in the mobile renderer |

Sources: `Engine/Config/{Windows,VulkanPC,Mac,IOS,Android}/DataDrivenPlatformInfo.ini`; Epic
pages listed under Sources. Three runtime conditions sit on top of the platform flag
(`RenderUtils.cpp`): the project switch `r.Nanite.ProjectEnabled`, 64-bit atomics reported by the
RHI, and forward shading off. VSM is gated on the same Nanite platform flag
(`DoesPlatformSupportVirtualShadowMaps`), which is why its hardware floor equals Nanite's.
`ue5-main` no longer also checks a GPU-scene flag in the platform test; `5.8` does.

### Mac and Apple Silicon

The floor in source (`Engine/Source/Runtime/Apple/MetalRHI/Private/MetalRHI.cpp`, identical on
`5.8`): SM6 is offered only on macOS 15 or later with GPU family Apple8 on Mac (the source names
this "M2+") and on iOS 18 or later with family Apple9. Epic's macOS requirements page states
macOS 14.5 as the engine minimum; the SM6 gate, and so Nanite's, is the stricter one. The file
states that SM6 goes through Apple's Metal Shader Converter. When SM6 is active the RHI sets 64-bit atomics as supported,
noting they arrived with M2-class devices. The two family thresholds match Apple's own rule for
64-bit atomic minimum and maximum, which
[apple-metal-geometry-constraints.md](apple-metal-geometry-constraints.md) documents and probes.

- **The technical reason is the visibility write.** Both rasterizers write a 64-bit value with
  an atomic maximum on a storage texture, and no fallback remains
  (see [nanite-core-architecture.md](nanite-core-architecture.md)). On Metal the 64-bit pixel
  format is marked unsupported, so the visibility target is created as a two-channel 32-bit
  unsigned texture flagged as 64-bit-atomic compatible (`NaniteCullRaster.cpp`, `MetalRHI.cpp`).
  The Metal shader header declares a 64-bit integer type and maps the write to a 64-bit atomic
  maximum on a texture (`Engine/Shaders/Public/Platform/Metal/MetalCommon.ush`).
- **Atomics binding depends on bindless.** With bindless on, which is the default for
  `METAL_SM6` (`Engine/Config/Mac/BaseMacEngine.ini`), atomic-compatible textures are created
  with Metal's shader-atomic texture usage. Without bindless, the texture is a linear texture
  laid over a buffer and bound as a buffer for atomic access (`MetalTexture.cpp`, `MetalUAV.cpp`).
  The second route resembles what the archived community port did, as the earlier notebook
  describes it.
- **Hardware raster uses the vertex-shader path.** `GetRasterHardwarePath` (`NaniteShared.cpp`)
  requires tier-1 mesh shaders in the platform configuration; `METAL_SM6` and `METAL_SM6_IOS`
  set both tiers false, and no RHI in the public repository reports primitive shaders. The Metal
  RHI does contain mesh and amplification shader types, so this is a configuration choice. The
  reason is not stated in the files read.
- **Persistent-thread culling is forced off** on Apple Silicon by the RHI, with the reason given
  as the lack of a forward-progress guarantee. Its default is already 0 on `ue5-main`.
- **Ray tracing of Nanite** is limited to modes 0 and 1 as described above.

Status in 5.8 (macOS requirements page): Nanite and Virtual Shadow Maps are Beta on M2 and
later; Lumen hardware ray tracing and MegaLights are Experimental on M2 and later; Lumen software
ray tracing and TSR are supported from M1 and on Intel Macs. The Nanite page's platform section
does not mention Apple.

### Mobile and iOS

Nanite has no integration with Unreal's mobile renderer. The only route on a phone is the
desktop renderer under the iOS SM6 shader platform, experimental in 5.8. The release notes say
that support "targets A15 and newer Apple Silicon"; the RHI source on `5.8` and `ue5-main` gates
iOS SM6 on GPU family Apple9, which the Apple notebook maps to A17 Pro and later. The two
statements disagree; the source is the stricter one and is the one consistent with where 64-bit
atomics exist on iOS. Android has ray tracing and Lumen flags on its SM5 Vulkan profile but no
Nanite flag.

## Temporal and editor integration

**Velocity.** Nanite pixels get motion vectors in the depth-export pass, not at raster time. For
each pixel the pass reads the visibility payload, fetches the visible cluster and its instance,
and computes velocity from the pixel's depth and the instance's current and previous transforms
(`CalculateNaniteVelocity`, called from `NaniteExportGBuffer.usf` and `NaniteDepthExport.usf`).
Clusters whose material offsets vertices are skipped there and write velocity from the material
pass, where the offset can be evaluated for both frames. Nothing per vertex is stored for the
previous frame; the only requirement on the geometry system is that the instance record carries
its previous transform.

**Temporal upscaling and dynamic resolution.** LOD scale is computed from the view rectangle in
rendered pixels (`FPackedView::UpdateLODScales`), so detail is chosen for the internal
resolution, not the output resolution. `r.Nanite.MaxPixelsPerEdge` defaults to 1 and
`r.Nanite.MinPixelsPerEdgeHW` to 32. A separate time-budget controller can relax the edge target
when the primary or shadow raster runs over budget (`r.Nanite.PrimaryRaster.PixelsPerEdgeScaling`,
default 30 percent), parallel to resolution scaling. TSR consumes Nanite only through depth
and velocity: a code search found no Nanite reference in the TSR shader directory, and
`PostProcess/TemporalSuperResolution.cpp` mentions it once, to turn thin-geometry detection on by
default when Nanite Foliage is enabled.

**Hit proxies and picking.** Editor picking is a full-screen pass over the main visibility
target that maps pixel to cluster to material slot to a hit-proxy id through a per-primitive
table (`DrawHitProxies`, `NaniteEditor.cpp`). A debug picking mode runs a one-thread compute
shader on the pixel under the cursor and writes a record identifying triangle, cluster, instance
or primitive.

**Selection outline.** Selected instances are rasterized again, alone, into a private visibility
target through the explicit-instance-list entry, using the editor view's matrices and no
occlusion input. A pass then emits their depth for the outline post-process. The second raster
is independent of scene depth, so the outline pass sees the whole selection, hidden parts
included.

**Visualization modes** (`NaniteDefinitions.h`, 42 on `ue5-main`; `5.8` lacks only the last):
overview, triangles, patches, clusters, primitives, instances, groups, pages, overdraw, raster
mode (software or hardware), raster bins, shading bins, scene depth minimum, maximum, delta and
decoded, material count, material mode, material index, hit-proxy depth, Nanite mask, lightmap
UVs and indices, hierarchy offset, position bits, shadow casters, evaluate vertex offset, pixel
programmable raster, picking, shading write mask, no-derivative ops, fast-clear tiles,
tessellation, displacement scale, vertex color, mesh paint texture, voxels, assemblies, skinning,
curves, far shadow casters. VSM adds its own: page mask, mip, virtual page, cache state, Nanite
overdraw, ray count, clipmap level, and on `ue5-main` an overview, receiver mask and static mask.

Most useful as diagnostics for a first cluster renderer: clusters, groups, pages, raster mode,
overdraw, material index, and position bits.

## Coupling map

| Downstream system | What it needs from cluster geometry | How Unreal meets it | Shape the geometry milestone up front? |
|---|---|---|---|
| Sun shadows (cascades or clipmap) | Many orthographic views in one cull; depth-only output; LOD in shadow texels; per-view occlusion | View array, depth-only context, per-view LOD multiplier | **Yes.** View index in candidate and visible-cluster records; depth-only mode |
| Local-light shadows (atlas or page cache) | Views with sub-rectangles of one target; shader-side scissor; static or dynamic class per instance; for a page cache, a page rectangle per cluster and page-mask culling | Same service plus the virtual-target permutation | **Yes** for view rectangles and static class; reserve a field for a target rectangle; the page logic itself can wait |
| Ray queries | Geometry for a BLAS that matches raster closely enough; a map from hit triangle to cluster data for attributes; a per-mesh, view-independent LOD rule | Fallback mesh by default; stream-out of a fixed-error cut; CLAS on NVIDIA only | **Yes, in the baker.** Emit a fixed-error proxy per mesh and keep clusters decodable without view state. No runtime work yet |
| GI capture (cards or probes) | Many small orthographic views, an explicit (instance, view) list, material attributes without lighting | Explicit instance draws, a dedicated material pass | **Partly.** The explicit-list entry is cheap to add early; an attribute-only material output is a shading-path decision |
| Transparency | Sorted or order-independent draws that can run a forward-lit pixel shader | Hardware-only cluster draws with the normal translucent shader, off by default | **No.** Keep a hardware path that can bind an arbitrary pixel shader |
| Picking and selection | Pixel to instance and triangle; redraw of a chosen instance set | Visibility payload; explicit-list redraw | **Partly.** Explicit-list entry again; an id output if no visibility buffer exists |
| Motion vectors and TAA | Previous transform per instance; depth; a hook for deforming materials | Computed after raster from depth and instance data | **No change** if instances already carry previous transforms. LOD follows render resolution |
| Streaming | A hierarchy that is always resident; clusters grouped into fixed-size pages with dependency lists; a "leaf of resident cut" state per cluster and node; a request record keyed by resource and page range | Root pages, fixups, GPU request buffer | **Yes, in the file format only.** Page-structured output and page-range keys in hierarchy nodes, even while everything is loaded |

## Corrections to earlier research

- [`pipeline-state-of-the-art-m7-m11.md`](../2026-09-14-roadmap-review/pipeline-state-of-the-art-m7-m11.md)
  says Epic's VSM documentation certifies Apple M2 as the minimum and concludes VSM-style shadows
  are high feasibility "across the entire Metal 4 Apple-Silicon target". The VSM page does list
  Apple Silicon M2 or newer. Two qualifications. The M2 floor is Nanite's floor: VSM platform support is defined in source as Nanite platform
  support, which on Metal means SM6 with 64-bit atomics on macOS 15. It says nothing about what a
  page-cached shadow atlas needs on its own. And Epic rates Nanite and VSM on Mac as Beta in 5.8,
  with M1 excluded.
- The same file states that "RT shadows/MegaLights/SMRT/ReSTIR are all hard-gated to M3+ RT
  cores". Shadow Map Ray Tracing is not hardware ray tracing; Epic defines it as "a sampling
  algorithm used with virtual shadow maps". And Epic's macOS page lists Lumen hardware ray
  tracing and MegaLights as Experimental on M2 and later, so Unreal does not gate them on M3.
- [`studio-and-engine-disclosures-2023-2026.md`](../2026-09-14-roadmap-review/studio-and-engine-disclosures-2023-2026.md)
  gives Nanite's API reach as "Any RHI incl. Metal; compute rasterizer + HW raster fallback".
  Nanite runs only on SM6 shader platforms whose RHI reports 64-bit atomics: D3D12, desktop
  Vulkan, Metal SM6 on M2 or later with macOS 15, and consoles. It does not run with forward
  shading or in the mobile renderer.
- The same file dates RTX Mega Geometry as "Announced/updated GDC 2026". It was public by
  2025-04-24 (NVIDIA's OptiX 9 article).
  [`pipeline-state-of-the-art-m7-m11.md`](../2026-09-14-roadmap-review/pipeline-state-of-the-art-m7-m11.md)
  describes it as tech demos and NvRTX integrations; as of 5.8 it is also in Epic's own source as
  `r.RayTracing.Nanite.Mode 2`. The "no Metal analog" conclusion stands.
- [`open-source-references-2024-2026.md`](../2026-09-14-roadmap-review/open-source-references-2024-2026.md)
  describes the archived `ue5-nanite-macos` port and calls 64-bit atomics "a concrete Metal
  limitation to plan around". Epic now ships Nanite on Metal, Beta in 5.8, with native 64-bit
  texture atomics on M2 and later, and its RHI still contains the buffer-backed route for the
  non-bindless case. The limitation now applies to M1 and to pre-15 macOS only.

The SM5 error in the same notebooks is already recorded in
[nanite-core-architecture.md](nanite-core-architecture.md).

## Implications for Luminex

Facts first, then options. Luminex is Forward+ with no G-buffer, one Metal 4 backend, an M3 Max
as the recorded machine, and no ray tracing, mesh-shader or 64-bit-atomic surface in the RHI yet.

**Unreal offers no precedent for the Luminex shading path.** Nanite opaque shading is deferred
only. The nearest precedent is the translucency path: clusters decoded in the vertex stage and
shaded by the ordinary forward pixel shader. That is the "ordinary indirect cluster raster" the
current M9 text already describes, and it needs no visibility buffer, no atomics and no change to
the M7 lighting bindings. A visibility-buffer resolve that evaluates material and Forward+
lighting in one pass is the other option and is what
[visibility-buffer-and-surface-paths.md](visibility-buffer-and-surface-paths.md) weighs; nothing
in Unreal's integration argues for or against it beyond showing that every other consumer
(shadows, captures, selection) is indifferent to which one the main view uses.

**Shadows do not need cluster geometry, but cluster geometry should be born multi-view.** The
ordering question between M8 and M9 has an asymmetric answer in Unreal's design:

- Cascades and an atlas work with ordinary geometry. M8 can precede M9 without rework if its
  shadow passes go through the M7 visibility path and are later pointed at the cluster path.
- A page-cached atlas is markedly cheaper with cluster-granular culling and per-view LOD. Epic's
  own non-Nanite VSM path, which culls whole instances and depends on authored mesh LODs, is the
  cautionary example, though Epic publishes no number for the gap. This notebook's reading is
  that a VSM-style cache, if it stays "eligible" in M8, is cheaper to build after cluster culling
  exists, whichever milestone number that is; Luminex has no geometry LOD before M9, so an
  instance-granular cache would start in the case Epic warns about.
- If M9 ships first, the eight properties under "What to design in advance" are the insurance.
  The first five change record layouts and are the ones to do on day one.

A reasonable split, as this notebook's opinion: keep M8's cascades and atlas independent of
M9; put multi-view and depth-only into the first cluster-culling slice; add a later slice,
"shadow views through the cluster path", owned by whichever milestone lands second; treat the
page cache as a separate slice gated on both.

**Ray queries on Metal have two usable sources and one unavailable.** Cluster acceleration
structures are NVIDIA-only. The portable choices are a fixed-error proxy mesh per asset
(Unreal's default) or decoding a fixed-error cut into flat buffers (Unreal's mode 1). Both are
view-independent per mesh, because instances share a BLAS. For M10 this means the M9 baker should
be able to emit a proxy at a stated error and should keep a decode path that needs no view state;
a path-trace oracle can simply trace the source mesh. Unreal's reliance on screen traces to hide
proxy mismatch is a warning for ray-traced shadows and reflections: self-intersection against a
coarser proxy needs a bias policy or a matched cut.

**GI capture wants an explicit (instance, view) entry and an attribute-only material output.**
The first is cheap in the cull pass. The second cuts across Forward+: Luminex shaders compute
lighting inline, so a capture pass needs a material-only variant. That is an M11 concern but
worth knowing before material evaluation is restructured for clusters.

**Streaming constrains the file format, not the M9 runtime.** Unreal's runtime streaming is a
CPU manager around three data-format facts: a hierarchy that is always resident, fixed-size
pages with dependency lists, and patchable "resident leaf" state. If the M9 baker writes pages,
dependencies and page-range keys while the runtime loads everything, M11 adds a manager and a
request buffer without re-baking. Unreal's defaults (128 KiB pages, 512 MB pool) are sized for
content far larger than Luminex has; the structure matters, the numbers do not.

**Apple specifics.** What Epic needed on Mac was 64-bit texture atomics, which exist from the
M2 generation; it did not need mesh shaders and runs hardware raster through vertex pulling
with mesh shaders switched off. For Luminex on M3-class hardware this says: the vertex-pulling
cluster path is a first-class configuration that Epic ships, a software rasterizer is the only
part that needs 64-bit atomics in the RHI, and Epic avoids persistent-thread compute on Apple
GPUs. Whether Metal mesh shaders would beat vertex pulling on M3 is not answered by Unreal, since
Epic leaves them off.

**Diagnostics worth copying early:** cluster, group and page coloring, raster-path coloring,
overdraw, and a buffer high-water-mark report equivalent to Unreal's feedback channel. The last
one fits the existing measurement harness.

## Open questions and what could not be confirmed

- Why mesh shaders are disabled for Nanite on `METAL_SM6`. The configuration is clear; the
  reason is not in the files read. Candidates are converter support, tier-1 per-primitive
  attributes, or performance **[UNVERIFIED]**.
- Whether ray tracing mode 1 works on Metal. No platform exclusion was found; it was not run.
- Console configuration. PlayStation and Xbox platform files are outside the public repository,
  so their raster path and Nanite flags are taken from documentation only.
- The iOS floor. Release notes say A15 and newer; source requires GPU family Apple9. Which one
  describes shipped behavior was not tested. The family-to-chip mapping used here is taken from
  the Apple notebook and from Unreal's own source text, not re-fetched from Apple by this
  notebook (the reference page did not render).
- The macOS floor. Epic's requirements page gives macOS 14.5 as the engine minimum while the RHI
  offers SM6, and so Nanite, only from macOS 15. Whether 5.8 on macOS 14.5 falls back to SM5
  without Nanite was not tested.
- How the Metal Shader Converter lowers the 64-bit texture atomic. The Unreal side was read; the
  generated Metal code was not inspected. The Apple notebook probes the equivalent hand-written
  Metal shader.
- NvRTX details. The claim that Mega Geometry shipped experimentally in NvRTX 5.6 is from press
  coverage; NVIDIA's branch page names only the 5.4 experimental branch.
- Performance. No number in this notebook is a measurement. Epic publishes no shadow or
  streaming timings for Apple hardware, and none were taken here.
- Lumen mesh distance fields for Nanite meshes are commonly said to be built from the fallback
  mesh. The Lumen page fetched does not say so and the builder was not read **[UNVERIFIED]**.
- VSM page marking, allocation and filtering were read only as far as they touch Nanite.

## Sources

Unreal Engine source, `EpicGames/UnrealEngine`, read 2026-10-01, branches `ue5-main`
(`8b2f1e5201d0`) and `5.8` (`de7c5b3f3cff`); `5.7` and `release` consulted for dating:

- `Engine/Source/Runtime/Renderer/Private/Nanite/`: `NaniteCullRaster.h`, `NaniteCullRaster.cpp`,
  `NaniteShared.cpp`, `Nanite.cpp`, `NaniteComposition.cpp`, `NaniteShading.cpp`,
  `NaniteMaterialsSceneExtension.h`, `NaniteTranslucency.cpp`, `NaniteRayTracing.h`,
  `NaniteRayTracing.cpp`, `NaniteRayTracingASCache.cpp`, `NaniteStreamOut.h`,
  `NaniteStreamOut.cpp`, `NaniteFeedback.h`, `NaniteFeedback.cpp`, `NaniteEditor.cpp`,
  `NaniteVisualize.cpp`
- `Engine/Shaders/Private/Nanite/`: `NaniteEmitShadow.usf`, `NaniteCullingCommon.ush`,
  `NaniteClusterCulling.usf`, `NaniteRasterizer.usf`, `NaniteWritePixel.ush`,
  `NaniteExportGBuffer.usf`, `NaniteDepthExport.usf`, `NaniteShadeBinning.usf`,
  `NaniteStreaming.ush`, `NaniteStreaming.usf`, `NaniteStreamOut.usf`, `NaniteRayTrace.ush`,
  `NaniteRayTracing.usf`, `NaniteClusterRayTracing.usf`, `NaniteTranslucency.usf`
- `Engine/Shaders/Shared/NaniteDefinitions.h`, `Engine/Shaders/Shared/VirtualShadowMapDefinitions.h`
- `Engine/Source/Runtime/Renderer/Private/VirtualShadowMaps/`: `VirtualShadowMapArray.cpp`,
  `VirtualShadowMapArray.h`, `VirtualShadowMapCacheManager.cpp`; `Engine/Shaders/Private/VirtualShadowMaps/`:
  `VirtualShadowMapBuildPerPageDrawCommands.usf`, `VirtualShadowMapPageOverlap.ush`,
  `VirtualShadowMapPageAccessCommon.ush`, `VirtualShadowMapPerPageDispatch.ush`
- `Engine/Source/Runtime/Renderer/Private/ShadowDepthRendering.cpp`, `PrimitiveSceneInfo.cpp`,
  `MobileShadingRenderer.cpp`, `RayTracing/RayTracing.cpp`
- `Engine/Source/Runtime/Renderer/Private/Lumen/`: `LumenSceneRendering.cpp`,
  `LumenSceneCardCapture.cpp`, `LumenHardwareRayTracingCommon.h`
- `Engine/Shaders/Private/RayTracing/RayTracingHitGroupCommon.ush`
- `Engine/Source/Runtime/Engine/Private/Rendering/`: `NaniteStreamingManager.cpp`,
  `NaniteResources.cpp`, `NaniteCoarseMeshStreamingManager.cpp`;
  `Engine/Source/Runtime/Engine/Private/Nanite/NaniteReadbackManager.cpp`
- `Engine/Source/Developer/NaniteBuilder/Private/`: `NaniteRaytracingScene.cpp`,
  `NaniteRayTracingScene.h`, `Encode/NaniteEncodeRayTracing.cpp`
- `Engine/Source/Runtime/RenderCore/Private/RenderUtils.cpp`, `Engine/Source/Runtime/RHI/Public/RHIGlobals.h`
- `Engine/Source/Runtime/Apple/MetalRHI/Private/`: `MetalRHI.cpp`, `MetalTexture.cpp`,
  `MetalUAV.cpp`, `MetalRHIPrivate.h`; `Engine/Shaders/Public/Platform/Metal/MetalCommon.ush`;
  `Engine/Source/Runtime/Core/Public/Mac/MacPlatform.h`
- `Engine/Source/Runtime/D3D12RHI/Private/D3D12RayTracing.cpp`,
  `Engine/Source/Runtime/D3D12RHI/Private/Windows/WindowsD3D12Device.cpp`
- `Engine/Config/{Windows,VulkanPC,Mac,IOS,Android}/DataDrivenPlatformInfo.ini`,
  `Engine/Config/Mac/BaseMacEngine.ini`
- Commit history of `NaniteRayTracing.cpp` and `NaniteRayTracingASCache.cpp` on `ue5-main`;
  GitHub code search on the default branch for cluster operations, primitive shaders and the
  procedural ray tracing define

Epic documentation, fetched 2026-10-01, all for Unreal Engine 5.8:

- [Nanite Virtualized Geometry](https://dev.epicgames.com/documentation/en-us/unreal-engine/nanite-virtualized-geometry-in-unreal-engine)
- [Virtual Shadow Maps](https://dev.epicgames.com/documentation/en-us/unreal-engine/virtual-shadow-maps-in-unreal-engine)
- [Lumen Technical Details](https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-technical-details-in-unreal-engine)
- [Forward Shading Renderer](https://dev.epicgames.com/documentation/en-us/unreal-engine/forward-shading-renderer-in-unreal-engine)
- [macOS Development Requirements](https://dev.epicgames.com/documentation/unreal-engine/macos-development-requirements-for-unreal-engine?lang=en-US)
- [Unreal Engine 5.8 Release Notes](https://dev.epicgames.com/documentation/unreal-engine/unreal-engine-5-8-release-notes)

NVIDIA, fetched 2026-10-01:

- [NVIDIA RTX Branch of Unreal Engine](https://developer.nvidia.com/game-engines/unreal-engine/rtx-branch)
- [Fast Ray Tracing of Dynamic Scenes Using NVIDIA OptiX 9 and NVIDIA RTX Mega Geometry](https://developer.nvidia.com/blog/fast-ray-tracing-of-dynamic-scenes-using-nvidia-optix-9-and-nvidia-rtx-mega-geometry) (2025-04-24)

Reported only (search result, page not fetched):

- [Nvidia integrates RTX Mega Geometry into Unreal Engine 5.6](https://techbriefly.com/2025/09/08/nvidia-integrates-rtx-mega-geometry-into-unreal-engine-5-6/)

Not fetched successfully: Apple's `MTLGPUFamily` reference page (returned no content). Apple
facts in this notebook rely on [apple-metal-geometry-constraints.md](apple-metal-geometry-constraints.md).
