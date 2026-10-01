# Beyond Triangle Clusters: Adjacent Geometry Representations

**Status**: Frozen — non-normative  
**Research date:** 2026-10-01

Evidence notebook for the [geometry direction review](../2026-10-01-geometry-direction-review.md),
covering the representations and techniques that sit next to triangle-cluster LOD: tessellation and
displacement, far-field aggregates, Gaussian splats, cluster acceleration structures for ray
tracing, deforming geometry, learned geometry and procedural aggregates. It was collected on
2026-10-01 by fetching vendor pages, specifications, papers, slides, registries and the Unreal Engine
source; Nanite itself belongs to the other notebooks and appears here only as a cross-reference.
Items marked **[UNVERIFIED]** were not confirmed against a primary source; recheck them before a
plan depends on them.

Conventions: bracketed keys such as [DXR-spec] resolve in Sources, where every entry is labeled
Primary (fetched and read in this task) or Reported (press, blogs, search-result summaries,
pages that could not be rendered). "Read" for Unreal means the file was fetched from the named
branch on 2026-10-01 and described in my own words.

Credit: Nanite and the other Unreal Engine systems described here are the work of Epic Games.
Unreal Engine source is copyright Epic Games, Inc. and was read under the Unreal Engine EULA; this
notebook describes mechanisms in its own words and reproduces no engine code, shader text or
comments. Unreal, Unreal Engine and Nanite are trademarks of Epic Games, Inc.

## Findings that matter most

1. **NVIDIA's displaced micro-meshes are finished.** OptiX 9.0 (announced 2025-02-06) deprecated the
   DMM API, both NVIDIA micro-mesh repositories are archived (last pushed in early 2025) with a
   note that Mega Geometry supersedes them, and the Vulkan registry marks `VK_NV_displacement_micromap`
   deprecated by the cluster extension. Opacity micromaps went the other way and are now a ratified
   KHR extension.
2. **Tessellation on Metal 4 means compute.** Apple's feature tables list fixed-function
   tessellation under the Metal 3 programming model only, and neither the Metal 4 render pipeline
   descriptor nor the Metal 4 render encoder in the macOS 26.5 SDK headers has tessellation state
   or patch draws. The three 2026-era sources I read (Hable at SIGGRAPH 2026
   Advances, Karis's Nanite tessellation posts, RTXMG cluster tessellation) all tessellate in
   compute-class passes rather than a fixed-function stage; Hable's design also stores the welded
   result once and reuses it across depth, G-buffer and shadow passes.
3. **Cluster acceleration structures are not a portable API.** They ship on NVIDIA through NVAPI on
   D3D12 and two unratified `VK_NV_` extensions on Vulkan, are specified for cross-vendor DXR in
   the public DirectX-Specs repository (preview slipped from "late summer 2026" to "tentatively
   fall 2026", and no preview post is visible on 2026-10-01), and have no counterpart in the
   Metal 4 API surface I enumerated.
4. **Epic's own default still pairs raster Nanite with a separate ray-tracing fallback mesh.**
   `r.RayTracing.Nanite.Mode` defaults to the fallback mesh on every branch read; streamed-out LOD
   and cluster acceleration structures are opt-in, and the latter needs NVIDIA cluster-ops support.
5. **The raster/ray LOD mismatch is documented by the vendor that sells the fix.** NVIDIA's
   `vk_lod_clusters` states that a raster path walking a continuous cut while ray tracing uses a
   cached BLAS at a discrete level "most likely" produces self-shadow or self-reflection
   artifacts, and adds an optional raster "Discrete LoD" mode (off by default) so distant
   instances use the same resident level. The sample is not itself a hybrid renderer, so this is
   the vendor's expectation, not a measured result, and the mode relies on every level covering
   the whole mesh, which the meshoptimizer builder guarantees only when no group stops
   simplifying early (section 4.3).
6. **Splats are standardized as an asset, not as a composition.** `KHR_gaussian_splatting` is
   ratified, but it defines no mesh/depth composition, no LOD, and treats splat color as
   display-referred and effectively unlit. Splat LOD research (Spark 2.0, V3DG) reuses the
   cluster-DAG recipe; no source I found selects between a mesh and a splat representation at
   runtime. The evidence supports keeping hybrid selection as independent research.
7. **Deformation under cluster LOD is solved for culling, open for LOD error.** Per-cluster
   conservative bounds in bone space (Tencent 2024, Unterguggenberger 2021, Epic's current source)
   are the shared mechanism. No source I read bounds simplification error under deformation.
8. **Metal-specific traps for a cluster-then-ray-query order.** Ray tracing in render pipelines is
   documented as incompatible with mesh shading; acceleration-structure builds are per-structure
   encoder commands whose descriptors carry CPU-side counts; and the 16 KB to 1 KB acceleration
   structure alignment relief is described only for M5-class hardware.
9. **Nothing in learned geometry has shipped.** NVIDIA's RTX Kit lists neural shading, textures,
   materials and faces, and classical Mega Geometry, but no neural geometry or intersection
   component; N-BVH's reference code is CUDA-only research code.

## 1. Tessellation and displacement

### 1.1 Status

| Technique | Who, when | Maturity on 2026-10-01 | Hardware / API | Source |
|---|---|---|---|---|
| Displaced micro-meshes (DMM), `VK_NV_displacement_micromap` | NVIDIA; deprecated with OptiX 9.0, 2025-02-06 | Deprecated; toolkit and Vulkan sample both archived (last pushes 2025-02-13 and 2025-01-31; the GitHub API does not expose the archive date) | Ada RT-core feature, NVIDIA Vulkan extension (provisional) | [OptiX9] [MM-Toolkit] [VK-MM-Sample] [VK-registry] |
| Opacity micromaps | Khronos | `VK_EXT_opacity_micromap` promoted to ratified `VK_KHR_opacity_micromap` | Multi-vendor | [VK-registry] |
| Cluster tessellation (Catmull-Clark, per-frame cluster templates) | NVIDIA RTXMG, v0.9.2 2025-04-11 to v2.0.0 2026-08-28 | Shipping open-source SDK | NVIDIA RTX, 10 GB VRAM, driver 570+ | [RTXMG] [VK-TessClusters] |
| Compute adaptive tessellation and Catmull-Clark | Hable (Meta), SIGGRAPH 2026 Advances, 2026-07-21 | Published talk, MIT-licensed WebGPU code | Any compute-capable GPU | [Hable26] |
| Concurrent-binary-tree (CBT) bisection | Benyoub and Dupuy (Intel), HPG 2024; Advances talk 2024-07-30 | Published, open source | Any GPU; fp64 for planet scale | [CBT] [Adv24] |
| Nanite Tessellation | Epic, UE 5.4, 2024 | Shipped; cross-reference only | Nanite platforms | [Karis-Tess] |
| HDRP tessellation | Unity | Shipping; documented limits | D3D/Vulkan class | [Unity-Tess] |
| Fixed-function tessellation | Apple, Metal 1.2 onward | Legacy; Metal 3 programming model only | Apple3 and later | [Metal-FST] [Metal-Tess-Archive] |

### 1.2 NVIDIA micro-meshes: what they were and why they ended

A displaced micro-mesh is a coarse base triangle plus a power-of-two subdivision whose
micro-vertices carry scalar displacement along an interpolated direction. NVIDIA's own sample
rasterized them with mesh/task shaders or a compute path using fixed 64-vertex, 64-triangle
meshlet allocations and explicit load balancing between high- and low-subdivision base triangles
[VK-MM-Sample]. The hardware path was an RT-core feature on GeForce 40-series; NVIDIA's micro-mesh
landing page still advertises it with no deprecation notice, so that page is stale relative to
the repositories [MM-Page].

The end of the line is documented three ways: the OptiX 9.0 post says the DMM API "is now
deprecated and is being replaced by the OptiX Clusters API", was removed from the SDK, and that
existing DMM applications "will be disabled in a future driver version" [OptiX9]; the toolkit
README says the Vulkan extension "is no longer available" and points to Mega Geometry [MM-Toolkit];
and `vk.xml` carries `deprecatedby="VK_NV_cluster_acceleration_structure"` on the extension
[VK-registry]. Whether Blackwell lacks the DMM hardware engine was not confirmed
**[UNVERIFIED]**; NVIDIA's Blackwell architecture paper describes only the new triangle-cluster
engines and Opacity Micromap engine [Blackwell-WP].

Lesson: a fixed hardware displacement format lost to cluster templates plus programmable
tessellation, and the replacement is a cluster-level tessellation sample (`vk_tessellated_clusters`)
that implements an unofficial `KHR_materials_displacement` glTF extension with texture-based
displacement and tessellates only instances that are primary-visible [VK-TessClusters].

### 1.3 Compute adaptive tessellation (the portable pattern)

Hable's SIGGRAPH 2026 Advances talk is the most complete description of a compute-only
pipeline, and it is implemented in plain WebGPU, so its structure ports to Metal compute [Hable26].
The mechanism, in enough detail to reimplement:

- **Pattern.** Per-edge float tessellation factors, no shared interior factor. A "clamped
  parallelogram" pattern derives a triangle's interior from the two edges meeting at a corner
  (1D point placement in U and V, clamped into the triangle by `u = min(u, 1 - gap - v)`), with a
  precomputed gap-matching table for the third edge. It avoids DX11's minimum of six triangles,
  uses about one third fewer triangles at equal power-of-two factors, handles long thin
  triangles, and transitions from a single triangle. Tables are 25 by 25 over the 25 unique 1D
  point lines, 1.6 MB total; a full per-factor table costs 2.5 MB at maximum factor 64, 8.6 MB at
  128 and 30.2 MB at 256, and the table path measured faster than analytic evaluation.
- **Adaptive factors.** Factors come from a Phong-approximation midpoint error measured as an
  arc angle, not a screen projection. Seams between patches are closed by a welding post-pass
  (corners, then edges), because UV, material and round-off seams cannot be solved by identical
  factors alone; the talk calls this a fundamental limit of hardware tessellation too.
- **Tessellate once, render many.** Output goes to large flat buffers (indices, positions,
  normals, packed barycentrics, source triangle index; 704 MB at the 16M-vertex, 16M-triangle
  cap) and every pass (depth prepass, G-buffer, motion vectors, shadows) rasterizes the stored
  result with attributes interpolated in the vertex stage. DX11 tessellation re-runs per pass.
  Measured on an RTX 3070 per 1M triangles: a single lit pass 0.36 ms versus 0.50 ms for
  OpenSubdiv's fused path, and a lit pass plus four shadow passes 0.81 ms versus 2.51 ms.
- **Catmull-Clark.** Quads decompose into regular bicubic patches (analytic evaluation, semi-sharp
  creases as a change of basis); irregular neighborhoods are handled by recomputing a few
  subdivision levels per irregular vertex in workgroup memory and blending to the limit position.
  Measured compute cost lands within a small factor of published uniform evaluators (1.32 to
  1.68 times slower than Edge-Friend, 1.91 to 2.45 times faster than DV21; the uniform ones
  compute positions only). The same slide set reports that RTXMG would not run on the test
  machine (10 GB VRAM minimum).
- **Why displacement.** A scalar displacement field is the compression argument: the Suzanne
  example stores about 5.45M triangles of detail in a 1.19 MB lossless 2K height map. The limits
  stated are 8-bit quantization/resampling loss and no topology change (no overhangs or folds).

Karis's February 2026 posts make the same cost argument from the Nanite side (displacement is one
value where explicit detail needs five or more per vertex) and add a platform argument: low-poly
fallback meshes amplified by tessellation serve platforms that cannot run full Nanite [Karis-Tess].
Hable lists Nanite among recent compute tessellators and characterizes its pattern as integer
factors with nested splits whose pops are hidden only when triangle error is sub-pixel; his stated
goal is fractional factors with independent edges and no pops [Hable26].

### 1.4 Intel's concurrent binary trees

A CBT is a GPU memory-pool manager over a bit tree. The 2024 contribution maps one "bisector"
(a longest-edge-bisection triangle) to each halfedge of an arbitrary polygon mesh, so adaptive
triangulations of halfedge meshes (not just square terrain domains) come from halfedge operators,
and uses the CBT only to allocate and release bisectors concurrently rather than as an implicit
encoding, which lifted the depth limit (subdivision depth up to 64 with a 128K-element pool, CBT
depth 17, about 7 MiB) [CBT]. Measured on an AMD 6800 XT chosen to mimic a console: incremental
update 0.1 ms (planet), 0.084 ms (moon) and 0.085 ms (21,399-halfedge asset); the planet frame
used 64K triangles at about 49 pixels per triangle and rendered through a visibility buffer
in 1.7 to 1.9 ms including shading. The paper positions CBT for terrain, water and planets because
Nanite-style approaches "are not applicable as the amounts of geometry involved are simply too
large". Planet scale needed double precision for vertex positions; Metal Shading Language does
not support `double` [MSL-4.1], and the authors say quantization might avoid it but they did not try.
It is a second lineage for large smooth domains, not a replacement for cluster LOD on authored
assets. Both a paper and a talk exist: HPG 2024 (PACMCGIT 7(3)) and the SIGGRAPH 2024 Advances talk
"Achieving scalable performances for large scale components with CBTs" [HPG24] [Adv24].

### 1.5 Hardware tessellation on Metal

Metal's tessellation is a three-stage model: a compute kernel writes half-precision per-patch
factors, a fixed-function tessellator generates samples for triangle or quad patches, and a
post-tessellation vertex function evaluates each sample [Metal-Tess-Archive]. The current feature
tables (dated 2026-05-21) list Tessellation, Indirect tessellation arguments and Tessellation in
indirect command buffers under the column "Metal 3" only, whereas mesh shading, ICBs and ray
tracing read "Metal 3 & 4"; the table's own footnote defines the column as the programming models
in which a feature is available [Metal-FST]. Apple's `MTL4RenderPipelineDescriptor` lists no
tessellation properties (it lists `maxVertexAmplificationCount`, topology, vertex and fragment
function descriptors, and binary-linking options) [Metal-Docs]. The macOS 26.5 SDK headers agree:
no `MTL4*.h` header mentions tessellation, `MTL4RenderPipeline.h` has no tessellation property and
`MTL4RenderCommandEncoder.h` has no patch-draw or tessellation-factor command; the word appears
only in the Metal 3 headers (`MTLRenderPipeline.h`, `MTLRenderCommandEncoder.h`,
`MTLIndirectCommandEncoder.h`) [Metal-SDK]. Reading these together, a Metal 4-first renderer has
no fixed-function tessellator **[inference from three Primary sources; mixing Metal 3 pipeline
objects into a Metal 4 command stream was not tested]**. Mesh shading is listed from Apple7
and indirect mesh draws from Apple9 [Metal-FST].

### 1.6 Displacement in shipped engines: integration costs

| Cost | Evidence |
|---|---|
| Ray tracing sees a different surface | Unity HDRP: "tessellated meshes fall back to a non-tessellated version" in ray tracing [Unity-Tess] |
| Motion vectors | Unity HDRP: motion vectors fail when tessellation factors differ between frames [Unity-Tess] |
| Bounds | Unity HDRP: world-space displacement can expand objects beyond their bounds and needs manual adjustment [Unity-Tess] |
| Per-pass recompute | Hable: DX11-style tessellation re-evaluates each pass; storing the welded result once is about 3 times cheaper with four shadow passes [Hable26] |
| Memory cap | Hable: fixed 16M-vertex buffers, with tessellation factors dropped first when the cap would be exceeded [Hable26] |
| Pattern pops | Nanite's integer-factor pattern pops when factors change, hidden only by sub-pixel error [Hable26] |
| Terrain tessellation in UE 5.8 | Mesh Terrain (Experimental) combines Nanite with variable tessellation; forum reports of breakup at distance are Reported, not verified [UE58-Notes] [UE58-Press] |

Call of Duty: Ghosts shipped tessellation with Catmull-Clark subdivision in 2014 (Brainerd,
SIGGRAPH 2014 Advances, as cited by [Hable26]); I did not fetch that talk.

## 2. Far-field and aggregate geometry

### 2.1 Octahedral impostors

Mechanism (from open reimplementations, Reported [Impostor-Demo]): bake the mesh from a grid of
directions laid out on an octahedral map (full sphere or upper hemisphere) into an atlas of
albedo, normal and depth frames. At runtime draw one camera-facing quad per instance covering the
bounding-box silhouette; convert the view direction to an octahedral UV, pick the grid cell,
sample the three nearest frames, blend by barycentric weights, and use stored depth for a step of
parallax. Epic's lineage: Ryan Brucks's UE4 Impostor Baker (article dated 2018-04-09, Reported)
was used for Fortnite tree impostors, offered full-sphere, upper-hemisphere and traditional
billboard modes, and the octahedral layout improved texture use [Impostor-80lv]. The current Unreal
source still ships an `ImpostorBaker` plugin under `Engine/Plugins/Experimental`, whose descriptor
calls it an impostor generator for distant mesh LODs (branches 5.8 and ue5-main) [UE-Src].

Limits (my reasoning from the mechanism, not a measured claim): no true silhouette or parallax
beyond the stored depth, shadows and ray queries need a separate representation, and the
mesh-to-impostor switch has to be blended. Maturity: the oldest technique in this notebook, still
shipped. Cost to Luminex: a bake tool and a per-instance state; it needs no cluster geometry.

### 2.2 Hierarchical LOD (merged proxies)

- **Unreal.** The `ue5-main` World Partition HLOD plugin has five builders: instancing, mesh
  merge, mesh simplify, mesh approximate and custom actor [UE-Src]. It is an authored, offline,
  spatial-grid proxy system that predates and coexists with Nanite.
- **Roblox SLIM (SIGGRAPH 2026 Advances, 2026-07-21).** A cloud "world transcoder" optimizes
  a region as one aggregate instead of per asset: bake all parts into a shared model space, merge
  geometry with compatible material state (new batch at the 16-bit index limit), remove interior
  triangles by sampling rays from points on every triangle and keeping a triangle if any ray
  escapes, simplify the remainder with a custom edge-collapse simplifier, and keep one virtual bone
  per static mesh so doors and wheels still move. The client treats the produced discrete LOD
  levels as a budget problem: a triangle budget (4.7M desktop, 2.4M tablet, 1.1M phone) sets the
  target per instance from visibility and screen coverage, and a fixed byte budget keeps levels
  resident by loading the missing level with the best "visible error removed per added byte". The
  talk reports up to 97 percent fewer client instances, 98 percent fewer triangles, 74 percent
  fewer draw calls and 33 percent less GPU memory in its demo. Its future-work list names point
  clouds, Gaussian splats and impostors for distant representations and says plainly that these
  "are research directions, not shipping features" [Roblox26].
- **Ray-tracing proxies.** Ubisoft's Assassin's Creed Shadows ray-traced GI (SIGGRAPH 2025
  Advances) uses a deliberately coarse copy of the raster world: low LOD, no vertex animation or
  skinning, small geometry culled; a typical urban scene holds about 2,000 BLAS, 30,000 instances,
  300 MB of BLAS and 20 MB of TLAS on Xbox Series X [AC-Shadows].

### 2.3 Voxel and aggregate far fields

Unreal 5.7 (released 2025-11-12 per press coverage, Reported) introduced Nanite Foliage as an
**Experimental** system built from Nanite Assemblies, Nanite Skinning and Nanite Voxels
[UE57-80lv]. Epic's wording as surfaced
by search snippets (the documentation page itself would not render for me, so Reported) describes
voxels as near-pixel-sized aggregates that retain triangle detail, animation and material
characteristics by camera distance [UE-Foliage-Snippets]. The source-based notebook
`nanite-geometry-extensions.md` confirms the status and owns the mechanism: assemblies and voxels
sit behind startup switches that are still off by default on 5.8.3, voxel clusters are opaque
bricks drawn only by the compute rasterizer into the visibility buffer, and they are built by
ray tracing the source mesh offline. For the industry picture:

- I found **no other shipped voxel far-field**. The nearest relative is NVIDIA's Mega Geometry
  foliage system (GDC 2026, with CD PROJEKT RED for The Witcher 4), which has "a new
  level-of-detail system for foliage" and "uses partitioned top-level acceleration structures"
  [NV-GDC26] [NV-Blog-Foliage]; press describes a multi-stage LOD that progressively merges
  sub-meshes into fewer instances until each tree is one instance (Reported [NV-Foliage-Press]).
  It is instance merging, not voxelization. The RTXMG 2.0.0
  changelog lists Cluster LOD and not foliage, and I found no dedicated foliage repository in the
  `NVIDIA-RTX` organization; NVIDIA said it would open-source its newest Mega Geometry work
  "later this year" [RTXMG] [NV-GDC26].
- Roblox lists splats, point clouds and impostors as unshipped distant representations
  [Roblox26]; Spark's splat LOD tree is the closest shipped analogue (section 3).
- A negative datum for generic cluster LOD: Bevy's meshlet path requires opaque materials
  ("Transparent, alpha masked, and transmissive materials are not supported") and gives materials
  no vertex-stage control [Bevy-Src]. Alpha-card foliage is where cluster simplification stops
  working, and Epic answered with a new, experimental, three-part system rather than the generic
  path **[inference]**. San Miguel's alpha-masked foliage sits exactly there.

## 3. Gaussian splats next to meshes

### 3.1 The standard

`KHR_gaussian_splatting` was announced as a release candidate on 2026-02-03 with ratification
expected in Q2 2026 [Khronos-PR]. In the Khronos glTF repository its README status read
"Release Candidate" until a registry-update commit on 2026-09-03 changed it to "Complete, Ratified
by the Khronos Group"; the actual ratification date is not visible in the files [KHR-GS]. The
extension defines:

- a mesh primitive with mode `POINTS` carrying the extension (a loader that ignores it draws a
  point cloud with the glTF material);
- the one kernel `ellipse`, with position, rotation, scale, opacity and spherical harmonics up
  to degree 3, `perspective` projection and a `cameraDistance` sort;
- composition as depth-sorted alpha blending, back to front, with the standard transmittance sum,
  preferring floating-point accumulation buffers, premultiplied inputs otherwise;
- a **display-referred image state** "similar to `KHR_materials_unlit`": "glTF scene lighting,
  exposure settings, and tonemapping generally do not affect rendered splats", though an
  implementation may relight;
- extension points for new kernels, color spaces, projections, sort methods and compression.
  SPZ (Niantic Spatial) and L-GSC (Qualcomm) are named as compression proposals in the press
  release; a `KHR_gaussian_splatting_compression_spz` candidate is reported but its folder is not
  in the Khronos extension directory today [Khronos-PR] [KHR-GS].

It says nothing about depth composition with meshes, mixed transparency, LOD, streaming or
temporal behavior. Contributors include Cesium, Niantic Spatial, Esri, NVIDIA, Huawei, Autodesk,
Khronos and independents [KHR-GS].

### 3.2 Cost on Apple hardware and open Metal code

The Unity Gaussian-splat renderer's benchmark (6.1M splats, 1200 by 797): RTX 3080 Ti 6.8 ms
(4.5 render, 1.1 sorting, 0.8 view calculation) and Apple M1 Max through Metal 21.5 ms (46 fps),
with about 48 bytes per splat of extra GPU memory for sorting and cached view data [UnityGS].
`MetalSplatter` is a Swift/Metal renderer for iOS, macOS and visionOS, pushed 2026-09-02, with
PLY, SPZ and `.splat` loading [MetalSplatter]. Apple's open SHARP model regresses a Gaussian
representation from a single photograph (arXiv 2512.10685; repository pushed 2026-09-11, license
field not machine-identified) [SHARP]. M3 Max timings are not in any source I read.

### 3.3 Splat LOD is the cluster-DAG recipe

| Work | Date | Mechanism |
|---|---|---|
| Hierarchical 3D Gaussians (Kerbl et al.) | SIGGRAPH 2024, ACM TOG 43(4) | Chunked training merged into an optimizable hierarchy; LOD selection with smooth transitions for kilometer-scale captures [H3DGS] |
| Virtualized 3D Gaussians (V3DG) | arXiv 2025-05-10 | Offline hierarchical clusters, online footprint-based cluster selection, inspired by Nanite, aimed at crowd-scale composed scenes; the abstract says its test assets (objects, trees, people, buildings) each need on the order of 0.1 billion Gaussians [V3DG] |
| LODGE | NeurIPS 2025 | Per-level depth-aware smoothing, importance pruning and fine-tuning; spatial chunks loaded on demand with opacity blending at chunk borders for memory-limited devices [LODGE] |
| Spark 2.0 (World Labs) | 2026-04-14; v2.3.0 on 2026-09-30 | LoD splat tree (interior nodes merge children); priority-queue traversal to a fixed budget of 500K to 2.5M splats in O(B log N); a 16M-splat GPU pool of 64K-splat pages mapped to chunks of a streamable `.RAD` file with LRU eviction; CPU radix sort back to front, instanced WebGL2 draws; fuses splat and mesh objects inside three.js [Spark2] [Spark-Repo] |
| HiGS | arXiv 2026-05-29 | Separates coarse macro-tiles (bin and depth-sort) from fine render tiles; up to 15.8 times faster than original 3DGS with exact front-to-back compositing [HiGS] |

The common architecture (hierarchy, screen-size metric, budgeted cut, paged residency) is the
one a cluster-geometry milestone would build for triangles, so splat LOD is a possible later
consumer of that infrastructure, not a prerequisite for it **[my reading]**.

### 3.4 Hybrid mesh and splat work (2024 to 2026)

- **Mesh-bound Gaussians for editing and animation.** SuGaR (Guédon and Lepetit, arXiv
  2023-11-21; CVPR 2024 per the earlier notebook): surface-aligned Gaussians, Poisson mesh
  extraction, optional binding of Gaussians to the mesh so rigging and relighting use ordinary
  tools [SuGaR]. Gaussian Frosting (arXiv 2024-03-21; ECCV 2024 per the earlier notebook): a base
  mesh plus an adaptive-thickness shell of Gaussians for fuzzy materials such as hair and grass,
  with parameters that follow mesh deformations [Frosting].
- **Reconstruction-time hybrids.** Hybrid Mesh-Gaussian Representation (IJCAI 2025): textured
  meshes for flat texture-rich regions, Gaussians for intricate geometry, joint optimization with
  transmittance-aware supervision, higher fps with fewer Gaussians [HybridMG].
- **Unified rasterization.** UniMGS (arXiv 2026-01-27): anti-aliased alpha blending of triangle and
  Gaussian fragments in a single pass, with a mesh proxy driving Gaussian deformation [UniMGS].
- **Engine composition.** Spark fuses splats with three.js meshes; MetalSplatter and the Unity
  renderer draw splats as a separate pass.

None of these chooses, per view, between a mesh and a splat LOD with an error oracle. They
decide the split at training time, or composite fixed layers.

### 3.5 What composition requires

| Concern | Evidence and consequence |
|---|---|
| Depth | Splats have no single depth. The standard sorts by center distance; StochasticSplats approximates each Gaussian as a view-dependent plane so a fragment can be depth-tested against opaque scene depth with an ordinary z-buffer [StochasticSplats] |
| Transparency | Sorted blending needs a global or per-tile sort (1.1 of 6.8 ms on a 3080 Ti [UnityGS]). Sort-free alternatives: weighted-sum OIT (ICLR 2025, Reported from a search listing [SortFree]) and stochastic transparency (ICCV 2025, more than 4 times faster than sorted rasterization) [StochasticSplats] |
| Temporal | Stochastic variants are noisy at one sample per pixel and rely on accumulation; Gaussian Stippling (arXiv 2026-09-29) adds a learned spatiotemporal reconstruction [Stippling]. No source defines splat motion vectors, so TAA/TAAU/MetalFX inputs would need a convention |
| Color and exposure | Splat colors are display-referred and unlit by specification [KHR-GS]; Luminex's scene-linear, pre-exposed pipeline would need an explicit decode and a decision to bypass or invert exposure and tonemapping for that layer **[my reading]** |
| Shadows, lights, ray queries | Not addressed by the standard beyond permitting relighting; a splat layer would not cast or receive shadows without a separate design |
| Asset path | The primitive is `POINTS` mode, so a glTF loader must opt in explicitly or reject the file |

### 3.6 Does the evidence support the roadmap's placement?

The roadmap lists hybrid mesh/splat selection as independent research after M8 and M9, requiring
paired representations, error oracles and depth/transparency/temporal contracts
([roadmap Part II](../../roadmap/gpu-driven-hybrid-rendering.md)). The evidence supports keeping it
out of the milestone chain, with one refinement: the study splits in two.

- **A. A splat layer.** Load a ratified splat primitive, draw it as a depth-tested, sorted or
  sort-free pass over the opaque result, and decide the color/exposure rule. It needs the
  transparency contract from M8.4 and the temporal conventions from M6, but not cluster geometry.
- **B. Hybrid LOD selection.** Needs paired representations, a perceptual or image error oracle,
  and ideally the cluster hierarchy/paging machinery from M9. Nothing in 2024 to 2026 research
  provides a recipe, so it stays research.

Both are optional, and neither is a prerequisite of any M-slice.

## 4. Ray tracing against cluster geometry

### 4.1 What a cluster acceleration structure is

The DXR specification, the Vulkan proposal and NVIDIA's blog describe the same mechanism
[DXR-spec] [VK-NV-CAS] [NV-VK-Blog]:

- A **CLAS** is a small acceleration structure built from one cluster of at most 256 triangles and
  256 unique vertex indices (the Vulkan extension exposes device limits instead of fixed values).
  It carries a 32-bit user ClusterID readable in hit shaders; triangle indices inside a hit are
  cluster-local, so shaders combine ClusterID with primitive index.
- A **cluster BLAS** is built from a list of CLAS references, not copies, so one cluster can be
  shared across LOD cuts and instances. It can sit in a TLAS or a **partitioned TLAS** (PTLAS),
  where instances are grouped into partitions of roughly 100 to 1,000 so only changed partitions
  are rebuilt.
- A **cluster template** is a CLAS without positions: topology is fixed once and instantiated
  per frame with new vertex data, which is the animation and tessellation path.
- Everything is GPU-driven and batched: counts and addresses come from device memory through
  indirect acceleration-structure operations; compaction and the normal copy/postbuild modes do
  not apply (only serialization and tools modes remain). A CLAS cannot be updated, only rebuilt
  or instantiated, and a cluster BLAS cannot be the source of a conventional build or update
  [DXR-spec] [VK-AnimClusters]. In DXR a PTLAS instance may carry an explicit bounding box so its
  BLAS pointer can be swapped without a rebuild, which the spec names as the LOD-switching use;
  a "global partition" is meant for instances that move every frame [DXR-spec].
- Cost: build work drops by about two orders of magnitude because each CLAS stands for roughly
  100 triangles [Blackwell-WP]. NVIDIA's own animated-clusters sample (8.43M animated triangles,
  RTX 6000 Ada, 2560 by 1440): acceleration-structure time 5.22 ms for triangle BLAS with 10
  percent rebuilds, 2.78 ms refit-only, and 0.80 ms with cluster templates; render time 1.39,
  1.45 and 1.52 ms. The README says plainly that for static content a fast-trace triangle BLAS
  remains the best option and recommends clusters when geometry changes frequently (LOD
  streaming, skinning, adaptive tessellation) [VK-AnimClusters].

### 4.2 Who supports what

| Vendor / API | State on 2026-10-01 | Source |
|---|---|---|
| NVIDIA, D3D12 | Cluster and PTLAS support through NVAPI; Unreal queries it per device | [Blackwell-WP] [UE-Src] |
| NVIDIA, Vulkan | `VK_NV_cluster_acceleration_structure` (570, revision 4 dated 2025-07-16) and `VK_NV_partitioned_acceleration_structure` (571), both vendor-authored and not ratified; driver 572.16 (2025-01-30) onward | [VK-registry] [VK-NV-CAS] |
| NVIDIA, OptiX | OptiX 9.0 Clusters API | [OptiX9] |
| NVIDIA hardware | Supported on all RTX GPUs from Turing as a driver feature; Blackwell RT cores add a Triangle Cluster Intersection Engine and a cluster compression engine (up to 2 times ray-triangle rate over third generation) | [Blackwell-WP] |
| NVIDIA products | Alan Wake 2 first game with Mega Geometry (page dated 2025-01-30; Remedy figures of 5 to 20 percent fps and 300 MB less VRAM are Reported); SDK v2.0.0 adds Cluster LOD (2026-08-28) | [NV-AW2] [AW2-Press] [RTXMG] |
| Microsoft DXR | Clustered Geometry, Partitioned TLAS and Indirect AS Operations are in the public DirectX-Specs repository (last edited 2026-09-08), gated by `ClustersAndPTLASSupported`, `RAYTRACING_TIER_2_0` and Shader Model 6.10 (`RAYQUERY_FLAG_ALLOW_CLUSTERED_GEOMETRY`, `ClusterID()`); a `COMPRESSED1` vertex format is defined. The spec says the features are "aligned across hardware vendors" and can work on existing RT hardware "given a driver update". Preview: spec text says "~late summer 2026"; the 2026-04-27 DirectX blog says "tentatively starting with a preview fall 2026"; none of the ten most recent blog posts (through 2026-10-01) mentions it | [DXR-spec] [DX-Blog] |
| Vulkan cross-vendor | No `EXT`/`KHR` cluster extension in `vk.xml` at the 2026-09-24 repository head | [VK-registry] |
| AMD | Dense Geometry Format (128-byte blocks of up to 64 triangles and 64 vertices, HPG 2024 paper); SDK v1.2.0 on 2026-05-07 added SuperCompression (the animation-aware encoder is described in a post dated 2025-09-23, the date of v1.1.0); `VK_AMDX_dense_geometry_format` (479) is provisional; AMD says DGF "will be directly supported by future AMD GPU Architectures"; the Vulkan proposal expects native support to "ultimately evolve" and allows a driver fallback that decodes blocks into a conventional acceleration structure, and no AMD page read here names a shipping GPU that decodes DGF in hardware; a multivendor extension with Samsung is announced. "RDNA 5" attributions come from press only | [DGF-Page] [DGF-README] [DGF-Vulkan] [DGF-Paper] [VK-AMDX] |
| Intel | The DXR spec's "aligned across vendors" is the only primary statement; Intel Labs' trillion-triangle path-tracing work (Arc B580, partitioned TLAS "linkage of fragments", more than 9 million dynamic instances) is Reported from search summaries because the blog returned HTTP 403. Product support for cluster operations is not confirmed | [DXR-spec] [Intel-Blog] |
| Apple Metal | See 4.4 | [Metal-Docs] |
| Unreal | Cluster path gated on NVIDIA device capability; default is a fallback mesh | [UE-Src] |

### 4.3 Keeping raster LOD and ray-traced geometry consistent

Four strategies appear in sources I read, from cheapest to most faithful:

1. **Separate coarse proxy (the default almost everywhere).** Unreal's `r.RayTracing.Nanite.Mode`
   has three values: fallback mesh (the default on ue5-main, 5.8, release and ue6-main),
   streamed-out mesh, and cluster acceleration structures; requesting the third on a device
   without cluster ops falls back to the first with a one-time warning. Related knobs include a
   global proxy LOD bias, whether WPO and skinned proxies enter the ray-tracing scene, and whether
   proxies are in the scene at all [UE-Src: `NaniteResources.cpp`]. Unity HDRP and Ubisoft's Shadows use the same idea
   [Unity-Tess] [AC-Shadows]. NVIDIA's Blackwell paper describes the practice it wants to retire:
   "Falling back to low-resolution proxies for ray-traced effects is no longer needed"
   [Blackwell-WP].
2. **Stream out the current cut into an ordinary triangle BLAS.** Unreal's streamed-out mode is
   controlled by a LOD bias (0 is full detail), a global minimum cut error, a separate larger
   bias and minimum error for off-screen instances (defaults 1.0 LOD bias and 4.0 error), a
   per-resource "reference instance" that sets streaming requests and BLAS LOD, a BLAS cache
   (64 MB default) with a relative-error tolerance for hits (0.5 default), an option to let ray
   tracing drive Nanite streaming, and per-frame budgets (16M vertices, 64M indices, 8M built
   primitives, 1 GB staging) [UE-Src: `NaniteRayTracing.cpp`, ue5-main].
3. **Cluster-level reuse.** Unreal's CLAS mode keeps a CLAS buffer (512 MB default), allocation
   chunks, and automatic "quality scaling" that lowers ray-tracing LOD when the CLAS pool is
   under pressure (thresholds 70 and 85 percent, floor 0.3) [UE-Src]. RTXMG's Cluster LOD does the
   same with three BLAS reuse tricks: sharing one canonical BLAS among distant instances with
   identical coarse cuts, a 64 MB cache of coarse-level BLAS across frames, and merging all
   high-detail instances into one BLAS; the sample is ray tracing only, so it has no raster
   consistency problem to solve [RTXMG-ClusterLOD].
4. **Make the rasterizer converge on the ray tracer's level.** `vk_lod_clusters` supports raster
   and ray tracing as separate render modes over the same LOD traversal and streaming system. Its
   BLAS caching pins
   one fully resident discrete level per geometry; if raster meanwhile walks the continuous DAG
   cut it pulls in a different, finer set, "most likely causing self-shadow or self-reflection
   artifacts". Its optional **Discrete LoD** for raster renders distant instances from the same
   resident level as the cached BLAS. This is safe because "each LoD level is a complete
   decimation of the whole mesh", so rendering all clusters of a level is watertight and at
   least as detailed as the continuous cut, at the price of more triangles
   [VK-LODClusters-Docs]. Three limits apply. The mode is off by default "as this sample doesn't
   yet implement a hybrid renderer", so the artifact is predicted, not demonstrated. Only
   instances whose continuous cut already fits a small range of the last few levels qualify;
   nearer instances still walk the continuous cut. And the quoted property is conditional in the
   builder itself: the sample builds with a copy of meshoptimizer's `clusterlod.h` whose one local
   change is to add a single-cluster root when the last iteration is entirely stuck, and maps a
   level to the groups of one depth. In that builder, upstream and copy alike, a group whose
   simplification keeps more than 85 percent of its triangles is emitted as a terminal group at
   its depth and its clusters leave the queue, so every deeper level lacks that region. A depth
   level therefore tiles the whole mesh only when no group terminated at a shallower depth;
   otherwise the closed surface for level L is the depth-L groups plus all terminal groups from
   shallower depths [Meshopt-ClusterLOD]. `cluster-lod-construction.md` measured 0.44 percent
   (Sponza) and 0.22 percent (San Miguel) of triangles in terminal groups with default settings,
   without separating root clusters from early stops, and a quarter of San Miguel without a
   coarser level when the simplifier fallbacks were disabled.

Known failure modes with a source: shadow draw-in when the BVH lags the raster LOD (NVIDIA's claim
for Alan Wake 2 is that Mega Geometry "eliminates" it [NV-AW2]); self-shadow and
self-reflection from level mismatch [VK-LODClusters-Docs]; and a research result that
ReSTIR's spatiotemporal reuse breaks when LOD changes the mesh topology between frames, which
the SIGGRAPH 2026 paper "Real-Time Level-of-Detail Rendering with ReSTIR" (Wang, Kettunen,
Lin, Wyman, Zhao) addresses with a surface-point mapping across topologies [ReSTIR-LoD]. Mitigations
beyond these (ray-origin bias, matching shading normals) are common practice but I did not find
them in a fetched source **[UNVERIFIED]**.

### 4.4 Apple Metal's position

- The Metal 4 acceleration-structure API enumerated from Apple's documentation (2026-10-01) has
  primitive descriptors for triangles, curves and bounding boxes, motion variants, instance and
  **indirect instance** descriptors (instance and motion-transform counts from GPU buffers), and
  intersection-function tables [Metal-Docs]. I found no cluster-level or partitioned structure,
  no opacity-micromap type, and no indirect primitive-structure build. The macOS 26.5 SDK headers
  agree: `MTL4AccelerationStructure.h` declares only those descriptor classes, and no Metal header
  mentions clusters or micromaps [Metal-SDK]. That settles the SDK on the development machine,
  not later OS releases; recheck before relying on it.
- Builds, refits, copy-and-compact and compacted-size queries are encoder commands taking a
  descriptor; the triangle geometry descriptor holds `triangleCount`, `vertexBuffer`,
  `indexBuffer` as buffer ranges [Metal-Docs]; in the SDK header the primitive counts are plain
  integer properties, and only the indirect instance descriptor reads its counts from a buffer
  [Metal-SDK]. The feature tables list "Address-driven
  acceleration structure builds" under Metal 4 from Apple9 (M3 and A17 Pro) [Metal-FST]. A GPU-selected LOD
  therefore needs either a CPU-visible count (a frame of latency through readback), a
  conservative maximum with degenerate padding, or a CPU-chosen discrete level **[inference]**.
- Acceleration-structure alignment "drops from 16kB to just one kilobyte" with M5-class third
  generation ray tracing, together with hardware instance transforms and hardware intersection
  function buffer indexing [Metal-M5]. By implication earlier generations, including the M3 Max
  development machine, pay 16 KB alignment per structure, which punishes many small BLAS
  **[inference]**.
- "Support for function pointers and ray tracing in render pipelines isn't compatible with mesh
  shading" [Metal-FST]. Read literally, a render pipeline with object or mesh stages cannot
  trace from its fragment stage; ray queries from a compute pass, or from a separate render
  pipeline without mesh stages (a full-screen deferred or visibility-buffer resolve), remain
  available **[my reading of a one-line footnote; not probed]**.
- Hardware ray tracing begins with M3/A17 Pro; third generation arrives with M5/A19 [Metal-M5].
  The WWDC26 session list I could read (neural rendering with Metal, tensors, game profiling
  tools, a Cyberpunk 2077 port) shows no acceleration-structure or geometry session, and the
  Cyberpunk talk's ray-tracing mentions are high-level [WWDC26]. The feature tables predate
  WWDC26 (2026-05-21).

## 5. Deforming geometry with cluster LOD outside Epic

| Approach | Bounds and error handling | Status | Source |
|---|---|---|---|
| Tencent adaptive-LOD mobile pipeline | Offline per cluster: a "main bone" (highest weight), a normal cone, and a bounding box that covers the cluster's maximum extent in that bone's space across all animations. At cull time the CPU sends bone transforms, the box is transformed to mesh space and culled normally. Skinned objects are a distinct visibility-buffer class whose vertex stage re-skins positions. No software raster (no atomic64 on mobile). Only LOD0 is authored; An 80M-triangle scene on mobile; about 3 ms GPU on a high-end phone and 20 ms on a five-year-old low-end one | Presented at SIGGRAPH 2024 Advances; shipping status not stated | [Tencent24] |
| Conservative meshlet bounds for linear-blend skinning | Computes spatial bounds and a normal cone valid across all animation states so frustum and backface culling per meshlet are safe; a search summary quotes up to 35.4 percent lower render time (Reported) | Peer-reviewed (Pacific Graphics 2021, in Computer Graphics Forum) | [Meshlet-Skin] |
| Epic Nanite skinning (cross-reference) | Source on ue5-main skins each cluster's bounding box by every bone that influences the cluster and unions the results; clusters, voxels and assembly transforms carry bone influences | Current source | [UE-Src: `NaniteVertexDeformation.ush`] |
| AMD DGF animation | Bake DGF from one keyframe; each frame a compute pass interpolates positions, re-quantizes and scatters them into the blocks through a vertex table; BVH is rebuilt each frame. Quantization step under 1 percent of frame time; about 20 microseconds for a 252K-triangle scene | SDK sample, 2025-09-23 | [DGF-Anim] |
| NVIDIA animated clusters | Topology fixed in cluster templates, vertices instantiated per frame; templates accept a bounding-box bloat percentage so later animation does not escape the box ("too low will result in artifacts") and a position-truncation setting | Sample | [VK-AnimClusters] |
| Roblox SLIM aggregates | Skeletons merged into one shared skeleton, a virtual bone per static mesh; aggregation does not make content static | Deployed at scale per the talk | [Roblox26] |
| Bevy meshlets | Preprocessed static assets; "not suitable for dynamically generated geometry"; no skinning is documented in the module | Open source | [Bevy-Src] |

What none of them gives: a bound on simplification error under deformation. Rest-pose error
metrics are what the hierarchy stores; the conservative boxes above only make culling safe.
Whether a pose that compresses a limb makes a rest-pose-derived LOD cut visibly wrong is not
addressed in any fetched source. The earlier notebook's claim that Nanite skeletal meshes use
animation LODs instead of geometry LODs does not match the source read for
`nanite-geometry-extensions.md`: skinned Nanite meshes keep the cluster hierarchy, the LOD cut on
ue5-main and 5.8 is chosen from reference-pose bounds, and a separate animation LOD draws
instances below a screen-size threshold unskinned. It is an addition to geometry LOD, not a
replacement.

Luminex today animates node transforms and clips, not skinned meshes **[from the repository's own
architecture summary]**, so deformation is a format-reservation question, not a milestone.

## 6. Learned geometry

| Item | Status | Evidence |
|---|---|---|
| N-BVH (neural ray queries with BVH, SIGGRAPH 2024) | Research code, MIT; CUDA compute capability 7.5+, tiny-cuda-nn FullyFusedMLP, Windows/Linux; no production use mentioned | [N-BVH] |
| LSNIF and a voxel deformation-aware successor (AMD) | Research lineage, not shipped | Earlier notebook rows; not re-fetched |
| Neural prefiltering for LOD (Weier et al., SIGGRAPH 2023); Neural geometric LOD (CVPR 2021) | Research | Earlier notebook rows; not re-fetched |
| Hierarchical Neural Surfaces for mesh compression (arXiv 2025-12-17) | Research; zero-genus meshes only; a search summary reports a 327K-vertex mesh decoding in under 100 ms on an RTX 2070 (Reported) | [HNS] |
| NVIDIA RTX Kit | Neural Shaders, Neural Texture Compression, Neural Materials, Neural Faces, RTXTF, RTXGI/RTXDI, Mega Geometry, Opacity Micro-Map, Character Rendering; no neural geometry or intersection component | [RTXKit] |
| Real-Time LoD Rendering with ReSTIR (SIGGRAPH 2026) | Not neural; a transport result about LOD changes | [ReSTIR-LoD] |

Nothing in this area has shipped as a geometry feature. The only learned items with a product
home (textures, materials, faces) are not geometry. N-BVH's reference code is CUDA-only, so the
earlier notebook's objection (it presumes a ray-tracing pipeline) is joined by a platform one.

## 7. Procedural and instanced aggregates

- **Assemblies (Epic).** Nanite Assemblies in 5.7 are the "parts" that become detailed instances
  across a foliage asset (Reported [UE57-80lv]); the builder has `NaniteAssemblyBuild.cpp`, and the
  deformation shader carries an assembly-transform skinning path (ue5-main) [UE-Src]. Detail
  belongs to `nanite-geometry-extensions.md`. UE 5.8 release coverage says Nanite's hierarchical
  instance culling now works on chunks of 64 instances and supports GPU-updated instances,
  aimed at scenes with many GPU-generated PCG instances (Reported [UE58-Press]).
- **Aggregate LOD as a bake (Roblox SLIM).** Section 2.2: merge, remove hidden surfaces,
  simplify, keep virtual bones, select discrete levels under a rate-distortion budget [Roblox26].
  This is the closest shipped answer to "LOD for a prefab built from many cubes".
- **GPU scattering.** Guerrilla's Horizon Zero Dawn (GDC 2017, Jaap van Muijden) used compute
  shaders to place environment content around the player from artist-authored rule graphs
  [HZD-GDC]. Unreal's PCG framework has GPU processing nodes dispatched as compute shaders and,
  per 5.8 coverage, GPU runtime scatter "within the same performance range as the landscape GPU
  grass system" (Reported [UE58-Press]); I could not read the primary release-notes text for this
  item.
- **Ray tracing instances.** NVIDIA's PTLAS exists to update huge instance sets per frame
  (100K physics objects in its sample; foliage in the Witcher 4 demo) [NV-VK-Blog] [NV-Blog-Foliage].
  Apple's M5-class hardware adds hardware instance transforms and the 1 KB alignment, which
  Apple frames as keeping many small objects separate [Metal-M5].

## Corrections to earlier research

| Earlier claim | Finding |
|---|---|
| CBT work cited only as "SIGGRAPH 2024 Advances" and as terrain-oriented ([studio and engine disclosures](../2026-09-14-roadmap-review/studio-and-engine-disclosures-2023-2026.md)) | There is a talk (Advances, 2024-07-30) and a peer-reviewed paper (HPG 2024, PACMCGIT 7(3), arXiv 2407.02215). Targets are terrain, water and planets; the method extends to arbitrary halfedge meshes; planet scale used fp64 |
| RTX Mega Geometry "v2.0.0 (Sep 2026)" and "implementation is vendor-specific" ([PC vendor landscape](../2026-09-14-roadmap-review/pc-vendor-and-api-landscape.md)) | v2.0.0 was published 2026-08-28 (v1.0.0 2025-07-01, v0.9.2 2025-04-11). The API is vendor-specific on Vulkan but specified cross-vendor in the DXR spec repository, and NVIDIA states support on all RTX GPUs from Turing |
| Animated clusters: "BLAS build 5.22 ms to 0.80 ms, total 6.61 ms to 2.32 ms" ([open-source references](../2026-09-14-roadmap-review/open-source-references-2024-2026.md)) | Those compare against a triangle BLAS with 10 percent rebuilds. The refit-only baseline is 2.78 ms build and 4.23 ms total (about 3.5 times and 1.8 times), clusters trace slightly slower (1.52 versus 1.39 to 1.45 ms), and the README recommends triangle BLAS for static content |
| "No formal `KHR_gaussian_splatting` extension located ... [UNVERIFIED]" ([PC vendor landscape](../2026-09-14-roadmap-review/pc-vendor-and-api-landscape.md)) | The extension exists; release candidate 2026-02-03; registry status "Complete, Ratified" as of the 2026-09-03 commit. The later direction review was right |
| Nanite tessellation "regressed in 5.6+, unresolved as of 5.7.2 (open bug UE-2347891)" from a blog-mill source | Not re-verified; I could not find a primary source. Karis's 2026 posts describe tessellation as shipped in 5.4. Forum reports about 5.8 Mesh Terrain and tessellation breakup are Reported only |
| "Snowdrop ... represents distant trees with depth impostors" ([production engines](../production-engines.md)) | Not checked here **[UNVERIFIED]**; the verified shipped ray-tracing proxy example in this notebook is Assassin's Creed Shadows |

## Implications for Luminex

Facts and options tied to the constraints (Metal 4 only, M3 Max development machine, solo
developer, cluster geometry planned before ray queries). The plan decisions are the coordinator's.

1. **Cluster-before-ray-queries is workable, but the cluster builder should emit complete
   per-level decimations.** Metal has no CLAS, so Luminex's M10 will build ordinary triangle BLAS.
   The strategies that transfer are the ones that do not need cluster-level AS: a coarse proxy,
   a CPU-chosen discrete LOD level per mesh shared across instances (BLAS sharing/caching), and
   making the rasterizer converge on that level where mismatch artifacts show. All three need the
   hierarchy to expose each level as a closed, watertight triangle mesh. `vk_lod_clusters` states
   this as a property of its levels, but the meshoptimizer `clusterlod.h` builder underneath does
   not guarantee it: a group that stops simplifying early is terminal at its depth and absent
   from every deeper one (section 4.3). The bake therefore has to construct and check each
   level itself, as the groups at that depth plus all terminal groups from shallower depths, and
   report or bound the terminal share. Whether that composite is crack-free where a terminal
   group meets coarser neighbors is not established: the builder recomputes boundary locks each
   iteration from the clusters still in play, so that seam is treated like an open mesh border
   in later iterations **[inference from the builder source; not tested]**. M10's existing "proxy classes and
   mismatch views" ([roadmap](../../roadmap/gpu-driven-hybrid-rendering.md)) are the right gate;
   the new fact is that a raster-versus-ray LOD divergence view is a named failure mode with a
   proposed fix, described by one vendor sample that does not itself render hybrid.
2. **Do not plan on mesh shaders and fragment-stage ray queries together.** Metal's footnote makes
   mesh-shader render pipelines incompatible with ray tracing in the same pipeline. A forward
   pass drawn through M9's optional mesh-shader path could not trace from its own fragment
   stage; ray-query consumers would live in a compute pass or in a separate render pipeline
   without mesh stages, which also
   interacts with the visibility-buffer decision in `visibility-buffer-and-surface-paths.md`.
3. **BLAS granularity matters more on the M3 Max than on M5.** With 16 KB alignment implied on
   earlier generations, thousands of per-instance, per-frame BLAS are the wrong shape. Shared
   BLAS per discrete level, merged far-field BLAS, and CPU-visible counts fit the Metal 4 API
   **[inference]**. NVIDIA's reuse tricks (sharing, caching, merging) are the transferable part
   of RTXMG.
4. **Tessellation is a compute feature that needs a cluster-friendly output path.** The portable
   recipe is Hable's: per-edge fractional factors from tables, tessellate once into flat buffers,
   weld, reuse across shadow and depth passes. It does not need cluster raster, but it needs the
   M6 motion-vector contract (Unity shows what breaks when factors vary), displaced bounds in
   the M7 instance table, and a decision about whether RT sees the displaced surface. There is no
   Luminex content that needs it today.
5. **Impostors and aggregate LODs are alternatives for the content cluster LOD handles worst.**
   Alpha-masked foliage (San Miguel) is where generic cluster paths stop (Bevy excludes masked
   materials; Epic built Nanite Foliage). If M9 scopes cluster LOD to opaque geometry, an
   octahedral-impostor or merged-aggregate far field is the cheaper complement, with no
   dependence on cluster geometry. Roblox's SLIM is a recent, large-scale existence proof for
   baked aggregate LODs with budgeted selection.
6. **Splats: keep as independent research and split it** (section 3.6). The dependency that
   matters is M8.4's transparency contract plus the display-referred decision, not M9.
7. **Deformation is a reservation, not a deliverable.** No skinned content exists. If the
   cluster format reserves an optional per-cluster bone-influence list (what Tencent and Epic
   store), skinned culling can follow later without a format break **[my suggestion]**.
8. **Neural geometry and hardware cluster formats are watch items.** DGF hardware, DXR cluster
   previews and any Apple acceleration-structure change are the events that would re-open the
   cluster-AS question for Metal; none is dated.

### Placement table

| Technique | Maturity | Who ships or publishes it | Prerequisites in Luminex terms | Placement | Reasoning |
|---|---|---|---|---|---|
| Compute adaptive tessellation and displacement | Published 2026 talk with code; Nanite tessellation shipped 5.4 | Hable, Epic, NVIDIA RTXMG | Instance table bounds, M6 motion vectors, indirect draws; cluster raster not required | Gated research; no dependency on M9, gated on content that needs it | Only portable route on Metal 4; costly integration surface and no content needs it |
| Fixed-function (Metal 3) tessellation | Legacy | Apple | Not in the Metal 4 model | Out of scope | Absent from the target programming model |
| Micro-meshes / `VK_NV_displacement_micromap` | Deprecated 2025 | NVIDIA | None | Out of scope | Deprecated, vendor-only, superseded |
| CBT bisection (terrain, water, planets) | Published HPG 2024, open source | Intel | A terrain scene; fp64 workaround on Metal | Out of scope until large-terrain content exists | Different problem from authored-asset LOD |
| Octahedral impostors | Mature, shipped (Fortnite lineage) | Epic plugin, many | Bake tool, alpha-masked path (exists), per-instance LOD state | Gated research (small slice) | Cheap far-field for masked foliage; independent of cluster geometry |
| HLOD / merged aggregate proxies | Mature (UE HLOD); SLIM 2026 | Epic, Roblox | Offline tool, scene hierarchy (UX3), instance table | Gated research | Complements M11 residency as coarse data; offline-heavy |
| Voxel far-field for foliage | Experimental and off by default through 5.8.3, single vendor | Epic (5.7) | Cluster geometry, assemblies, skinning, a compute rasterizer with a visibility buffer, build-time ray tracing of the source mesh (per `nanite-geometry-extensions.md`; voxels are opaque, so transparency is not one) | Out of scope for now; revisit after M9 | Every prerequisite is unbuilt |
| Splat layer (ratified glTF primitive) | Standard ratified; open renderers exist, including on Metal | Khronos, World Labs, MetalSplatter | M8.4 transparency contract, M6 temporal convention, color/exposure rule | Independent research (keep, study A) | Standard covers the asset only |
| Hybrid mesh/splat LOD selection | Research | Academia | Study A plus M9 hierarchy/paging, error oracle | Independent research (keep, study B) | No recipe exists in 2024 to 2026 work |
| Cluster AS for ray tracing (CLAS, PTLAS, DGF) | NVIDIA shipping; DXR spec pending; Vulkan NV-only; AMD future | NVIDIA, Microsoft spec, AMD | No Metal API | Out of scope as implementation; design influence only | Metal exposes nothing comparable today |
| Triangle-BLAS proxy plus discrete-level convergence | Proxy: standard practice. Convergence: one vendor sample option, off by default | Epic default, Ubisoft (proxy); NVIDIA sample (convergence) | M9 levels that the bake has made and verified complete (terminal groups carried into deeper levels), M10 AS and proxy classes | Milestone requirement inside M10, builder constraint in M9 | Cheapest consistent answer on Metal; the builder does not provide complete levels unconditionally |
| Skinned cluster LOD | Published and shipped | Epic, Tencent | Skinned content (none), M9 clusters | Out of scope; reserve format space | No skinned assets in Luminex |
| Neural LOD, N-BVH, neural intersection and geometry compression | Research | NVIDIA, AMD, universities | CUDA stacks, ray queries | Out of scope (observe only) | Nothing shipped; CUDA-bound |
| Assemblies / prefab instancing with LOD | Experimental (Epic), shipped as bake (Roblox) | Epic, Roblox | Scene documents/hierarchy, M9 | Gated research after M9 | Needs hierarchy semantics that UX3 scene documents may provide |
| GPU scattering / procedural placement | Shipped (HZD 2017, UE PCG) | Guerrilla, Epic | GPU-written instance rows, which the paced table design does not allow today | Gated research after M9 and M11 | Needs a new ownership model for instance rows |

## Open questions and what could not be confirmed

- **DXR cluster preview.** The date has slipped from "late summer" to "tentatively fall 2026"; no
  preview announcement was visible in the DirectX blog's ten newest posts on 2026-10-01. Intel and
  AMD driver support for cluster operations is unconfirmed.
- **Complete LOD levels.** Whether a depth level of the cluster hierarchy is a closed mesh
  depends on no group having terminated earlier; `vk_lod_clusters` asserts the property without
  that condition, and how its Discrete LoD and cached BLAS behave on a mesh with early terminal
  groups was not determined. Whether a level completed with shallower terminal groups is
  crack-free at their seams is an untested inference from the builder source. Both need a bake-time
  check on real content before a plan relies on per-level meshes.
- **Apple.** I found no cluster-level acceleration structure, opacity micromap type or indirect
  primitive build in Apple's documentation or the macOS 26.5 SDK headers, and no related WWDC26
  session in the list I could read.
  Recheck the next OS release's documentation and the Metal feature tables for changes after
  2026-05-21. The mesh-shading footnote was read, not probed: which pipeline combinations it
  forbids in practice is unconfirmed, and so is the 16 KB alignment on pre-M5 hardware, which
  Apple states only as the value M5 improves on.
- **DGF hardware.** No AMD source read here says which GPU, if any, decodes DGF natively today;
  the wording is "future" architectures plus a driver fallback.
  M3 Max numbers for splat rendering, compute tessellation and many-BLAS builds do not exist in any
  source I read; they would need local probes (see `apple-metal-geometry-constraints.md`).
- **Blackwell and DMM hardware.** Not confirmed whether the DMM engine was removed.
- **NVIDIA foliage.** Whether the Mega Geometry foliage system is open-sourced, and its actual LOD
  algorithm, is unknown; only the GDC 2026 announcement and press descriptions were available.
- **Epic documentation.** The Nanite Foliage and 5.7/5.8 release-note pages did not render;
  voxel wording, assembly details, the 5.7 release date and 5.8 PCG claims rest on search
  snippets and secondary coverage. The source tree was read for ray tracing, deformation, HLOD
  and the impostor plugin; where this notebook and `nanite-geometry-extensions.md` differ on
  foliage, voxels, assemblies or skinning, that notebook read the source and takes precedence.
- **Deformed LOD error.** No fetched source bounds simplification error under deformation.
- **Intel's trillion-triangle work.** The venue and technical details are Reported from search
  summaries; the blog could not be fetched.
- **Splat color pipeline.** Whether exposure bypass or inverse tonemapping gives acceptable
  results next to PBR content is untested.
- **Sort-free splat OIT (ICLR 2025)** was seen only in a search listing, not read.
- **Neural geometry rows** other than N-BVH, Hierarchical Neural Surfaces and the RTX Kit page
  were carried over from the earlier notebook and not re-fetched.
- **Call of Duty: Ghosts tessellation and Snowdrop impostors** were not independently checked.

## Sources

Primary means fetched and read in this task; Reported means press, blogs, search-result summaries
or pages that could not be rendered. Unreal Engine source was read through the authenticated
GitHub API on 2026-10-01 from `EpicGames/UnrealEngine`; it is licensed under the Unreal Engine EULA
and is described here, never reproduced.

**Tessellation, displacement, micro-meshes**

- [OptiX9] Primary. OptiX 9.0 release post, NVIDIA developer forums (2025-02-06). https://forums.developer.nvidia.com/t/optix-9-0-release/322842
- [MM-Toolkit] Primary. Displacement-MicroMap-Toolkit README and repository state (archived; last push 2025-02-13). https://github.com/NVIDIAGameWorks/Displacement-MicroMap-Toolkit
- [VK-MM-Sample] Primary. `vk_displacement_micromaps` README (DEPRECATED notice; archived, last push 2025-01-31). https://github.com/nvpro-samples/vk_displacement_micromaps
- [MM-Page] Primary. NVIDIA Micro-Mesh landing page. https://developer.nvidia.com/rtx/ray-tracing/micro-mesh
- [VK-registry] Primary. `xml/vk.xml` and `proposals/` in KhronosGroup/Vulkan-Docs, repository head dated 2026-09-24. https://github.com/KhronosGroup/Vulkan-Docs
- [RTXMG] Primary. NVIDIA-RTX/RTXMG README, `CHANGELOG.md`, releases (v0.9.2 2025-04-11, v1.0.0 2025-07-01, v1.0.1 2025-09-16, v2.0.0 2026-08-28). https://github.com/NVIDIA-RTX/RTXMG
- [VK-TessClusters] Primary. `vk_tessellated_clusters` README. https://github.com/nvpro-samples/vk_tessellated_clusters
- [Hable26] Primary. John Hable, "Adaptive Tessellation and Subdivision", SIGGRAPH 2026 Advances in Real-Time Rendering in Games (2026-07-21): slides https://advances.realtimerendering.com/s2026/content/Adaptive%20Tessellation%20and%20Subdivision_7.pdf, course page https://advances.realtimerendering.com/s2026/index.html
- [CBT] Primary. Benyoub and Dupuy, "Concurrent Binary Trees for Large-Scale Game Components", arXiv 2407.02215 (2024-07-02), PACMCGIT 7(3), doi 10.1145/3675371. https://arxiv.org/abs/2407.02215
- [HPG24] Primary. HPG 2024 paper listing. https://www.realtimerendering.com/kesen/hpg2024Papers.htm
- [Adv24] Primary. SIGGRAPH 2024 Advances course page (CBT talk, Tencent talk). https://advances.realtimerendering.com/s2024/index.html
- [Karis-Tess] Primary (engineer's blog). "Nanite Tessellation", Graphic Rants (February 2026). http://graphicrants.blogspot.com/2026/02/nanite-tessellation.html
- [Unity-Tess] Primary. HDRP 17.0 manual, Tessellation. https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@17.0/manual/Tessellation.html
- [Metal-FST] Primary. Metal Feature Set Tables (dated 2026-05-21). https://developer.apple.com/metal/Metal-Feature-Set-Tables.pdf
- [Metal-Tess-Archive] Primary. Metal Programming Guide, Tessellation (archived, 2016-12-12). https://developer.apple.com/library/archive/documentation/Miscellaneous/Conceptual/MetalProgrammingGuide/Tessellation/Tessellation.html
- [MSL-4.1] Primary. Metal Shading Language Specification 4.1. https://developer.apple.com/metal/Metal-Shading-Language-Specification.pdf
- [Metal-Docs] Primary. Apple developer documentation (read through its documentation data endpoints on 2026-10-01): `MTL4RenderPipelineDescriptor`, `MTL4AccelerationStructure*` descriptors, `MTL4ComputeCommandEncoder`, "Ray tracing with acceleration structures". https://developer.apple.com/documentation/metal/mtl4renderpipelinedescriptor
- [Metal-SDK] Primary. Metal framework headers in the macOS 26.5 SDK installed with Xcode on the development machine, searched on 2026-10-01: `MTL4RenderPipeline.h`, `MTL4RenderCommandEncoder.h`, `MTL4MeshRenderPipeline.h`, `MTL4AccelerationStructure.h`, and a search of all Metal headers for tessellation, cluster and micromap.
- [UE58-Notes] Primary, partial. Unreal Engine 5.8 release notes (page only partly rendered). https://dev.epicgames.com/documentation/unreal-engine/unreal-engine-5-8-release-notes
- [UE58-Press] Reported. UE 5.8 coverage (Mesh Terrain, PCG GPU scatter, instance culling): search listings including https://80.lv/articles/unreal-engine-5-8-is-out-today-with-big-optimization-improvements-and-mesh-terrain and https://tomlooman.com/unreal-engine-5-8-performance-highlights/

**Far field**

- [UE-Src] Primary (source). `EpicGames/UnrealEngine`, read 2026-10-01 on branches ue5-main, 5.8, release and ue6-main where noted: `Engine/Source/Runtime/Engine/Private/Rendering/NaniteResources.cpp`; `Engine/Source/Runtime/Renderer/Private/Nanite/NaniteRayTracing.cpp`; `Engine/Source/Runtime/D3D12RHI/Private/Windows/WindowsD3D12Device.cpp` (5.8); `Engine/Shaders/Private/Nanite/NaniteVertexDeformation.ush`; `Engine/Source/Developer/NaniteBuilder/Private/NaniteAssemblyBuild.cpp`; `Engine/Plugins/Editor/WorldPartitionHLODUtilities/` builders; `Engine/Plugins/Experimental/ImpostorBaker/ImpostorBaker.uplugin`.
- [Impostor-80lv] Reported. "Impostor Baker for UE4", 80.lv (2018-04-09). https://80.lv/articles/impostor-baker-for-ue4
- [Impostor-Demo] Reported. Open WebGPU octahedral-impostor demo describing the bake and blend. https://github.com/ektogamat/octahedral-impostor-component
- [Roblox26] Primary. Sergey Makeev, "SLIM: Scaling User-Generated 3D Worlds on Roblox", SIGGRAPH 2026 Advances (slides with notes). https://advances.realtimerendering.com/s2026/content/SMAK_SIG26_SLIM-Scaling_User-Generated_3D_Worlds_on_Roblox_8_Aug_2026.pdf
- [AC-Shadows] Primary. "Ray Tracing the World of Assassin's Creed Shadows", SIGGRAPH 2025 Advances (slides). https://advances.realtimerendering.com/s2025/index.html
- [UE57-80lv] Reported. Unreal Engine 5.7 release article, 80.lv (dated 2025-11-13; other coverage found by search gives the release as 2025-11-12). https://80.lv/articles/unreal-engine-5-7-is-now-availabe
- [UE-Foliage-Snippets] Reported. Epic documentation wording surfaced through search snippets; page did not render. https://dev.epicgames.com/documentation/unreal-engine/nanite-foliage
- [NV-GDC26] Primary. NVIDIA GeForce at GDC 2026. https://www.nvidia.com/en-us/geforce/news/gdc-2026-nvidia-geforce-rtx-announcements/
- [NV-Blog-Foliage] Primary, partial. NVIDIA developer blog post on the foliage system (only the partitioned-TLAS sentence was readable). https://developer.nvidia.com/blog/?p=113506
- [NV-Foliage-Press] Reported. Witcher 4 Mega Geometry demo coverage (multi-stage LOD, frame rates). https://www.tweaktown.com/news/110865/witcher-4-rtx-mega-geometry-foliage-demo-runs-at-almost-60-fps-on-an-rtx-4070-and-80-fps-at-4k-on-a-rtx-5090/index.html
- [Bevy-Src] Primary (source). `bevyengine/bevy` `crates/bevy_pbr/src/meshlet/mod.rs` and `asset.rs` on main (v0.20.0-rc.2 published 2026-09-28). https://github.com/bevyengine/bevy

**Splats**

- [Khronos-PR] Primary. "Khronos Announces glTF Gaussian Splatting Extension" (2026-02-03). https://www.khronos.org/news/press/gltf-gaussian-splatting-press-release
- [KHR-GS] Primary. `KHR_gaussian_splatting` README and commit history in KhronosGroup/glTF (status change in #2642, 2026-09-03). https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_gaussian_splatting
- [Spark2] Primary. "Streaming 3DGS worlds on the web" (Spark 2.0, 2026-04-14). https://www.worldlabs.ai/blog/spark-2.0
- [Spark-Repo] Primary. https://github.com/sparkjsdev/spark
- [UnityGS] Primary. UnityGaussianSplatting README (benchmark; last commit 2025-10-17). https://github.com/aras-p/UnityGaussianSplatting
- [MetalSplatter] Primary. https://github.com/scier/MetalSplatter
- [SHARP] Primary. https://github.com/apple/ml-sharp (arXiv 2512.10685)
- [H3DGS] Primary. https://arxiv.org/abs/2406.12080
- [V3DG] Primary. https://arxiv.org/abs/2505.06523
- [LODGE] Primary. https://arxiv.org/abs/2505.23158
- [HybridMG] Primary. https://arxiv.org/abs/2506.06988
- [UniMGS] Primary. https://arxiv.org/abs/2601.19233
- [SuGaR] Primary. https://arxiv.org/abs/2311.12775
- [Frosting] Primary. https://arxiv.org/abs/2403.14554
- [StochasticSplats] Primary. https://ubc-vision.github.io/stochasticsplats/
- [Stippling] Primary. https://arxiv.org/abs/2609.38488
- [HiGS] Primary. https://arxiv.org/abs/2606.00352
- [SortFree] Reported (search listing only). Sort-Free Gaussian Splatting, ICLR 2025. https://arxiv.org/pdf/2410.18931

**Ray tracing and cluster acceleration structures**

- [DXR-spec] Primary. DirectX Raytracing Functional Spec, Part 2 (`d3d/Raytracing2.md`, last edited 2026-09-08). https://microsoft.github.io/DirectX-Specs/d3d/Raytracing2.html
- [DX-Blog] Primary. DirectX Developer Blog: "Announcing Shader Model 6.10 Preview and AgilitySDK 720 Preview" (2026-04-27) and the blog category listing read on 2026-10-01. https://devblogs.microsoft.com/directx/shader-model-6-10-agilitysdk-720-preview/
- [VK-NV-CAS] Primary. `VK_NV_cluster_acceleration_structure` proposal and refpage. https://docs.vulkan.org/features/latest/features/proposals/VK_NV_cluster_acceleration_structure.html
- [NV-VK-Blog] Primary. "NVIDIA RTX Mega Geometry Now Available with New Vulkan Samples" (2025-02-06). https://developer.nvidia.com/blog/nvidia-rtx-mega-geometry-now-available-with-new-vulkan-samples/
- [Blackwell-WP] Primary. NVIDIA RTX Blackwell PRO GPU architecture whitepaper v1.0. https://www.nvidia.com/content/dam/en-zz/Solutions/design-visualization/quadro-product-literature/NVIDIA-RTX-Blackwell-PRO-GPU-Architecture-v1.0.pdf
- [NV-AW2] Primary. NVIDIA GeForce news, Alan Wake 2 Mega Geometry (page dated 2025-01-30). https://www.nvidia.com/en-us/geforce/news/dlss-4-multi-frame-generation-out-now/
- [AW2-Press] Reported. 5 to 20 percent fps and 300 MB VRAM figures. https://www.tweaktown.com/news/102660/alan-wake-2-is-the-first-game-to-use-nvidias-groundbreaking-new-rtx-mega-geometry-tech/index.html
- [VK-LODClusters-Docs] Primary. `vk_lod_clusters` README, `CHANGELOG.md` and `docs/` (BLAS sharing, caching, merging, `raster_discrete_lod.md`). https://github.com/nvpro-samples/vk_lod_clusters
- [Meshopt-ClusterLOD] Primary (source, MIT). `demo/clusterlod.h` in zeux/meshoptimizer on its default branch, and the sample's copy `src/meshopt_clusterlod.h` (taken from upstream commit 9443e0c; the two differ only by the single-cluster root), both read 2026-10-01. https://github.com/zeux/meshoptimizer/blob/master/demo/clusterlod.h
- [VK-AnimClusters] Primary. `vk_animated_clusters` README. https://github.com/nvpro-samples/vk_animated_clusters
- [RTXMG-ClusterLOD] Primary. RTXMG `docs/ClusterLOD.md`. https://github.com/NVIDIA-RTX/RTXMG/blob/main/docs/ClusterLOD.md
- [DGF-Page] Primary. AMD GPUOpen, Dense Geometry Compression Format SDK (v1.2, May 2026). https://gpuopen.com/dgf/
- [DGF-README] Primary. DGF-SDK README and releases (v1.0.0 2025-02-06, v1.1.0 2025-09-23, v1.2.0 2026-05-07). https://github.com/GPUOpen-LibrariesAndSDKs/DGF-SDK
- [DGF-Vulkan] Primary. "AMD releases Vulkan support for Dense Geometry Format" (2025-08-05). https://gpuopen.com/learn/dense-geometry-format-amd-vulkan-extension/
- [DGF-Anim] Primary. "Animating geometry with AMD DGF" (2025-09-23). https://gpuopen.com/learn/animating-geometry-with-amd-dgf/
- [DGF-Paper] Primary. Barczak, Benthin, McAllister, "DGF", PACMCGIT 7(3), 2024, doi 10.1145/3675383. https://gpuopen.com/download/publications/DGF.pdf
- [VK-AMDX] Primary. `VK_AMDX_dense_geometry_format` refpage. https://docs.vulkan.org/refpages/latest/refpages/source/VK_AMDX_dense_geometry_format.html
- [Intel-Blog] Reported (HTTP 403; search summaries). Intel Community, "Path Tracing a Trillion Triangles" and "Path Tracing Massive Dynamic Geometry in Jungle Ruins". https://community.intel.com/t5/Blogs/Tech-Innovation/Client/Path-Tracing-a-Trillion-Triangles/post/1687563
- [Metal-M5] Primary. Apple Tech Talk, "Boost your graphics performance with the M5 and A19 GPUs". https://developer.apple.com/videos/play/tech-talks/111431/
- [WWDC26] Primary. WWDC26 video list and sessions 356 and 388. https://developer.apple.com/videos/wwdc2026/
- [ReSTIR-LoD] Primary. Wang et al., "Real-Time Level-of-Detail Rendering with ReSTIR", SIGGRAPH 2026. https://research.nvidia.com/labs/rtr/publication/wang2026levelofdetail/

**Deformation, learned geometry, aggregates**

- [Tencent24] Primary. Shun Cao, "Seamless Rendering on Mobile: The Magic of Adaptive LOD Pipeline", SIGGRAPH 2024 Advances (slides with notes). https://advances.realtimerendering.com/s2024/content/Cao-NanoMesh/AdavanceRealtimeRendering_NanoMesh0810.pdf
- [Meshlet-Skin] Primary (author page); figure Reported. Unterguggenberger et al., "Conservative Meshlet Bounds for Robust Culling of Skinned Meshes", CGF 2021. https://johannesugb.github.io/gpu-programming/conservative-meshlet-bounds-for-robust-culling-of-skinned-meshes/
- [N-BVH] Primary. https://github.com/WeiPhil/nbvh
- [HNS] Primary (abstract). https://arxiv.org/abs/2512.15985
- [RTXKit] Primary. NVIDIA RTX Kit components. https://developer.nvidia.com/rtx-kit
- [HZD-GDC] Primary. GDC Vault, "GPU-Based Run-Time Procedural Placement in Horizon Zero Dawn" (GDC 2017). https://gdcvault.com/play/1024700/GPU-Based-Run-Time-Procedural
