# Nanite geometry extensions and forward direction

**Status**: Frozen — non-normative  
**Research date:** 2026-10-01

Evidence notebook for the [geometry direction review](../2026-10-01-geometry-direction-review.md),
covering everything Nanite renders beyond static opaque rigid triangle meshes, and what Epic has
committed and said about where it goes next. Mechanisms were read from the `EpicGames/UnrealEngine`
source on 2026-10-01 (`ue5-main` first, then `5.8` at 5.8.3 and `ue6-main`) and are described in
original wording; version and status labels come from release branches, release notes and Epic
pages fetched the same day. Items marked **[UNVERIFIED]** were not confirmed against a primary
source; recheck them before a plan depends on them.

Source citations are a path plus a branch. Unless a branch is named, a statement is true for
`ue5-main`, `ue6-main` and `5.8` alike.

Credit: Nanite and the other Unreal Engine systems described here are the work of Epic Games.
Unreal Engine source is copyright Epic Games, Inc. and was read under the Unreal Engine EULA; this
notebook describes mechanisms in its own words and reproduces no engine code, shader text or
comments. Unreal, Unreal Engine and Nanite are trademarks of Epic Games, Inc.

## What "Nanite today" includes

Nanite today is one cluster hierarchy and one visibility buffer serving three cluster types
(triangles, voxel bricks, curves), three fixed deformers (skinning, spline deformation, assembly
transforms) and material-programmable rasterization (WPO, masking, displacement). In a default
5.8.3 project only triangles, skinning, splines, programmable raster and tessellation are active.
Assemblies, voxels, curves and translucency are compiled in behind startup switches that default
to off.

| Extension | What it adds | First in source | Enabled by default in 5.8.3 | Raster path |
|---|---|---|---|---|
| Runtime tessellation and displacement | Screen-space dicing of cluster triangles into micropolygons displaced by the material | 5.3 (behind a default-off startup switch through 5.4) | Compiled in (`r.Nanite.Tessellation` 1); active only for materials that enable displacement | Compute rasterizer only |
| Spline meshes | Fixed deformation of a mesh along a spline segment | 5.3 (default off); default on from 5.4 | Yes (`r.Nanite.AllowSplineMeshes` 1) | Compute or hardware |
| Skinned meshes | Linear-blend skinning inside the cluster vertex path, bone-aware culling bounds | 5.5 | Yes (`r.Nanite.AllowSkinnedMeshes` 1) | Compute or hardware |
| World position offset (WPO) and masked materials | Material-driven vertex offsets and pixel discard through per-material raster shaders | Present before 5.3 | Yes (`r.Nanite.ProgrammableRaster` 1) | Compute or hardware |
| Assemblies | One resource that instances shared part hierarchies and keeps simplifying across the instances | 5.6 code; startup switch in 5.7 | No (`r.Nanite.AllowAssemblies` 0, or the Foliage project setting) | Same as the part's clusters |
| Voxel clusters (Nanite Foliage) | Coarse hierarchy levels stored as 4×4×4 voxel bricks, ray-marched per pixel | 5.6 code; startup switch in 5.7 | No (`r.Nanite.AllowVoxels` 0, or the Foliage project setting) | Compute rasterizer only |
| Curves | Hair-strand polylines as a third cluster type | 5.8 | No (`r.Nanite.AllowCurves` 0) | Compute rasterizer only |
| Translucency | Cluster-culled forward translucency for translucent materials on Nanite meshes | 5.8 | No (`r.Nanite.AllowTranslucency` 0) | Hardware rasterizer only in 5.8 |
| Landscape | A Nanite static mesh generated from the heightfield | 5.3 per release notes; not traced in source | Per landscape actor | Ordinary triangle clusters |

Evidence for the table: the `Allow*` and `Foliage` startup switches and their defaults are defined
in `Engine/Source/Runtime/Engine/Private/Rendering/NaniteResources.cpp`; `r.Nanite.Tessellation`,
`r.Nanite.ProgrammableRaster` and `r.Nanite.ComputeRasterization` (all 1) live in
`Engine/Source/Runtime/Renderer/Private/Nanite/NaniteCullRaster.cpp`; the combinations are in
`Engine/Source/Runtime/RenderCore/Private/RenderUtils.cpp`. `NaniteResources.cpp` on each release
branch gives the history: `5.3` defines `r.Nanite.AllowTessellation` and `r.Nanite.AllowSplineMeshes`,
both 0 and both described as highly experimental; `5.4` keeps tessellation at 0 and turns spline
meshes to 1; `5.5` drops the tessellation switch and adds `r.Nanite.AllowSkinnedMeshes` at 1; `5.7`
adds `r.Nanite.Foliage`, `r.Nanite.AllowAssemblies` and `r.Nanite.AllowVoxels`, all 0; `5.8` adds
`r.Nanite.AllowTranslucency` (0, described there as heavy work in progress) and
`r.Nanite.AllowCurves` (0). The project setting that fronts `r.Nanite.Foliage` is labeled
"Nanite Foliage (Experimental)" in `Engine/Source/Runtime/Engine/Classes/Engine/RendererSettings.h`
on `5.7`, `5.8`, `ue5-main` and `ue6-main`. All of these switches are read once at startup because
they change which shader permutations exist.

### How the extensions attach to the core pipeline

Three facts from the core pipeline explain most of what follows (the static-mesh pipeline itself is
in [nanite-core-architecture.md](nanite-core-architecture.md)):

- **A cluster has a type.** A cluster holds triangles (at most 128 triangles and 256 vertices),
  voxel bricks or curve points, never a mix. The hierarchy, streaming pages, culling and LOD
  selection are shared; only decode, rasterization and attribute reconstruction branch on the type
  (`Engine/Shaders/Private/Nanite/NaniteDataDecode.ush`, `Engine/Shaders/Shared/NaniteDefinitions.h`).
- **Visible clusters are sorted into raster bins.** A bin is a shader permutation plus a draw or
  dispatch. Materials that do nothing programmable share a small set of *fixed-function* bins
  selected by a bit mask: two-sided, spline, skinned, shadow-casting, voxel, curve. Every material
  that needs material code at raster time (WPO, displacement, custom vertex UVs, pixel discard,
  pixel depth offset) gets its own bin (`NaniteDefinitions.h`, `NaniteRasterBinning.usf`,
  `NaniteShared.h`). Skinning and spline deformation are therefore "free" in the sense that they
  do not create bins; WPO, masking and displacement are not.
- **Everything opaque lands in the same visibility buffer.** A pixel stores depth plus a visible
  cluster index and a primitive index within the cluster (triangle, brick or curve segment). The
  shading pass re-derives the surface point from that pair, which is why each extension also has a
  reconstruction branch in `Engine/Shaders/Private/Nanite/NaniteVertexFactory.ush`.

## 1. Tessellation and displacement

**What it is.** A material can output a scalar displacement along the interpolated normal. Nanite
then subdivides each visible cluster triangle on the GPU until the pieces are a couple of pixels
across, displaces the new vertices, and rasterizes the micro-triangles in compute. A separate
offline path bakes displacement into the mesh before the cluster hierarchy is built.

**Runtime mechanism** (`NaniteTessellation.ush`, `NaniteDice.ush`, `NaniteSplit.usf`,
`NaniteRasterizer.usf` under `Engine/Shaders/Private/Nanite/`, and `TessellationTable.cpp`,
`NaniteCullRaster.cpp` under `Engine/Source/Runtime/Renderer/Private/Nanite/`):

1. *Per-edge factors.* For each triangle the cluster rasterizer computes one tessellation factor
   per edge: view-space edge length divided by the nearer endpoint depth, scaled by the view's LOD
   scale and by the inverse dicing rate. `r.Nanite.DicingRate` defaults to 2 pixels per
   micropolygon edge. Because a factor depends only on the two endpoints of its edge, neighboring
   triangles compute the same value and the subdivision is watertight by construction.
2. *A precomputed pattern table.* `TessellationTable.cpp` builds, once, a triangulation for every
   sorted triple of integer edge factors up to 14 (stored in a 16×16×16 index space). A pattern is
   a list of barycentric vertices followed by packed index triples. Vertices on a patch edge are
   snapped to exact uniform divisions of that edge, mirrored so both neighbors agree. At run time a
   triangle is rotated so its largest factor comes first and, if needed, flipped; the flip is
   recorded so winding can be restored.
3. *Immediate dicing.* If all three factors are at most 8, the triangle is diced inside the cluster
   rasterizer without leaving the thread group. A second copy of the table is reordered so that
   triangle *i* introduces exactly one new vertex and references two earlier ones no more than 32
   positions back. In a 32-lane wave each lane then evaluates the material's displacement for one
   vertex and reads its two partners from other lanes, which is one displacement evaluation per
   output triangle.
4. *Splitting.* Larger triangles become *patches*: a 16-byte record holding the visible cluster
   index, the triangle index, and three corners as fixed-point barycentrics of the source triangle
   (two 16-bit coordinates per corner, the third implied). Patches go to a GPU work queue. `PatchSplit` runs up to 6 levels with
   ping-ponged indirect arguments. Each level rebuilds the patch corners from the (skinned or
   spline-deformed) base triangle, computes a bounding box, inflates it by the primitive's
   displacement range using the cone of the three corner normals (the bound from Nießner and Loop's
   patch-based occlusion culling, cited in the file), then frustum- and HZB-culls it. A patch whose
   factors still exceed 14 is subdivided with the table at one fourteenth of its factors (capped
   at 8 per edge) and its children are enqueued; otherwise it is appended to the visible-patch
   list together with its pattern index. Patches found occluded in the main pass are kept for the
   post pass of two-pass occlusion.
5. *Patch rasterization.* A second compute permutation takes a few patches per thread group
   (`r.Nanite.MaxPatchesPerGroup`, default 5, clamped to a third of the minimum wave size), runs
   the material "domain" evaluation at every pattern vertex with UV derivatives derived from the
   cluster's UV density, and feeds the micro-triangles to the same software triangle rasterizer
   used for clusters.
6. *Shading.* The visibility buffer stores the *base* triangle, not the micro-triangle. The shading
   pass takes the pixel's position from depth, moves it into local space and computes barycentrics
   against the undisplaced triangle, so attributes, UVs and normals come from the base mesh and the
   material; only position and depth come from the displaced surface (`NaniteVertexFactory.ush`).

Wave-wide work balancing is done by `Engine/Shaders/Private/WorkDistribution.ush`: every lane
announces how many child items it has, a prefix sum lays them end to end, and the wave then loops,
each pass handing one item to every lane together with the lane index of its parent. Parent state
stays in registers and is read across lanes, so nothing is written to a queue; the price is that
work is balanced only within one wave ("Variable sized work"). Immediate dicing, the first split
step and `PatchSplit` all sit on this primitive, and the shaders are compiled with a fixed 32-lane
wave requirement.

**Distance handling.** A primitive carries a displacement fade-out size. When the largest possible
displacement projects smaller than that size, the instance (or, if the primitive opts in, the
individual cluster) is rerouted to a non-tessellated fallback bin, and the magnitude is faded over
a depth range first. Beyond a derived depth the dicing factors are quartered. Shadow passes that
use the fallback apply the minimum displacement to the base triangles
(`NaniteCullingCommon.ush`, `NaniteRasterizationCommon.ush`).

**Offline displacement** (`Engine/Source/Developer/NaniteBuilder/Private/NaniteDisplace.cpp`,
`NaniteBuilder.cpp`): when the mesh settings carry displacement maps and a nonzero trim error, the
builder adaptively tessellates the source mesh against the top mip of the maps before clustering.
It samples displacement along the interpolated normal, bounds the error of each candidate
sub-triangle with affine arithmetic over barycentric ranges and a min/max texture query over the
covered UV rectangle, over-tessellates by half and lets the ordinary simplifier trim back to the
target. The result is plain triangles, so it needs nothing at run time. An experimental plugin,
`NaniteDisplacedMesh`, provides a related asset-level workflow; it was not read.

**Design rationale, from the author.** Brian Karis began publishing a series on this system on his
personal blog in February 2026 (five posts read; three more announced). The points that matter
here:

- It is a real-time Reyes pipeline (bound, split, dice, rasterize) appended to Nanite's existing
  stages; the cluster data structure and its build are unchanged. Simplification and amplification
  run at once on one mesh: small base triangles are merged away by normal LOD selection while
  large ones are diced ("Nanite + Reyes").
- The original plan was to synthesize tessellated clusters into the hierarchy and cache them, so
  that run-time cost would equal an offline-displaced mesh. It was abandoned: running the build in
  reverse within streaming budgets was not practical, and uniformly sampled displacement can never
  match the triangle efficiency of content-adaptive simplification. Run-time tessellation is
  therefore expected to cost more than the same surface baked offline ("Possible approaches for
  tessellation").
- The reasons to want it anyway are data size (one scalar per texel instead of full vertices),
  authoring and reuse, animated displacement, and scaling up from assets authored for low-end
  platforms ("Nanite Tessellation").
- Patterns are produced by isotropic remeshing in barycentric space with only edge lengths as
  input. The post reports 69% of the diced triangles of a D3D-style uniform pattern and 68% of the
  patches of binary splitting, and says the table has been reused with permission in NVIDIA's
  open `vk_tessellated_clusters` sample ("How to tessellate").
- `PatchSplit` is deliberately one material-independent shader. Per-material splitting would mean
  one dispatch per material per level with unknown work, so everything that affects splitting,
  including the displacement range, must be data, and the range is authored by the user. The post
  calls that range a common source of error that bloats bounds and defeats culling.
- Patches have no hardware raster path. Micro-triangles that cross the near plane are culled, not
  clipped, and the software rasterizer clamps a triangle's screen rectangle (64 pixels in the post),
  so a sharp displacement discontinuity seen up close tears instead of stretching.

**Version and status.** Source: present but default-off and labeled highly experimental in `5.3`
and `5.4`; always compiled from `5.5`. Release notes introduce it in 5.4 as "Nanite - Tessellation
(Experimental)", and the 5.8 documentation still marks both the run-time and the offline feature
experimental (section 11).

**Limitations.**

- Documented for 5.8: there is no crack-free displacement, so UV seams, hard normals or any
  discontinuous attribute feeding the displacement open cracks; pixel depth offset is ignored when
  tessellation is on; opacity masks are evaluated per diced triangle, not per pixel; WPO applies to
  base vertices before tessellation.
- Displacement forces the compute rasterizer; a displaced material never takes the hardware path,
  and tessellation requires programmable raster and compute raster to be enabled together
  (`RenderUtils.cpp`, `NaniteRasterBinning.usf`).
- The patch queues are fixed-size (2,097,152 candidate and 2,097,152 visible patches by default,
  `NaniteShared.cpp`); overflow is dropped, not deferred.
- Six split levels bound the amplification; a commit that clamps factors at the last level to avoid
  holes at extreme amplification landed on `ue6-main` on 2026-08-11 and on `ue5-main` on 2026-09-22.
- Material range merging and bindless material merging are disabled for displaced bins because the
  rasterizer assumes one UV density per range.
- Displacement is scalar along the normal with a per-primitive magnitude and center; previous-frame
  displacement is not evaluated, so animated displacement produces no correct motion vectors
  (stated as a to-do in `NaniteVertexFactory.ush`).
- Curve radius is not supported on meshes that go through offline displacement.

## 2. Skinned and skeletal meshes

**What it is.** Skeletal meshes built as Nanite resources. Vertices are skinned with linear-blend
skinning inside the cluster vertex fetch, on the GPU, every time a cluster is rasterized or shaded;
there is no pre-skinned vertex cache.

**Data** (`Engine/Source/Developer/NaniteBuilder/Private/Encode/NaniteEncodeSkinning.cpp`,
`NaniteDefinitions.h`): each cluster stores per-vertex bone indices and quantized weights (weights
are dropped when every vertex has a single bone) and, separately, a short list of up to 16 bones
that dominate the cluster, used only for bounds. Hierarchy nodes store up to 16 bone indices for
the same purpose. If a cluster's bones do not fit, it stores none and culling falls back to the
instance bounds.

**Bone transforms** (`Engine/Source/Runtime/Renderer/Private/Skinning/`): a scene extension owns
two GPU buffers, a per-primitive header and a pool of current and previous bone matrices with
defragmentation. *Transform providers* fill the pool. `5.7` has one provider, for baked animation
banks used by instanced skinned meshes; `5.8` adds a run-time provider and an animation-sequence
provider that uploads sampled sequence frames progressively under a per-frame budget. The
providers' internals were not traced for this notebook.

**Deformation** (`Engine/Shaders/Private/Nanite/NaniteVertexDeformation.ush`): the vertex function
blends position, normal and tangent by the vertex's bone matrices, and blends the previous position
by the previous matrices so that velocity comes out of the same code. The result is the
"post-deform local vertex" that every later stage consumes, so WPO and displacement stack on top
of skinning.

**Culling bounds** (`NaniteClusterCulling.usf`): a cluster's or node's box is transformed by each
bone in its list and the boxes are unioned. This is conservative for linear-blend skinning because
a skinned vertex is a convex combination of its bone-transformed positions. A debug switch
(`r.Nanite.Culling.SkinnedNodeBounds`) drops back to instance bounds for comparison.

**LOD selection.** On `ue5-main` and `5.8` the sphere used to pick the hierarchy cut is *not*
skinned: LOD is chosen as if the mesh were in its reference pose under the instance transform.
`ue6-main` adds a commit (2026-07-13, "Improve culling for Nanite skeletal meshes") that applies
the root bone transform uniformly to the instance's LOD bounds and bases the hardware-versus-compute
raster choice on the culling bounds, to fix meshes animated far from the bind pose by root motion.
Error metrics remain those of the reference-pose simplification; nothing accounts for stretching.

**Animation LOD** (`NaniteSkinningUpdateViewData.usf`): a compute pass walks the instance-culling
chunks, and marks each skinned instance as "deforming" per view when its bounding radius over
distance exceeds a per-primitive or default minimum screen size. Instances that fail the test are
drawn unskinned and are culled with static bounds, so distant crowds cost the same as static
meshes.

**Raster path.** Skinned is a fixed-function bin flag, so a skinned mesh with a plain material
uses the shared compute and hardware rasterizers with a skinning permutation; no per-material
shader is needed.

**Version and status.** Source: `r.Nanite.AllowSkinnedMeshes` first appears on `5.5` with default
1. Epic called it experimental at its 5.5 introduction and in the 5.5 and 5.6 documentation; the
5.7 release notes group it under "Nanite Foliage and Skinning (Experimental)"; the 5.7 and 5.8
documentation pages carry no label, and no release note declares it production-ready (section 11).

**Limitations.** Morph targets are not supported on Nanite (5.8 documentation). Assembly parts on
skinned meshes move rigidly per part and their per-vertex skinning is still marked to-do in
source; voxel clusters skin with a single bone per brick; vertex colors with skinning were enabled
only on `ue6-main` (2026-07-29 builder commit).

## 3. World position offset and programmable raster

**What it is.** WPO is a material output that moves a vertex in world space. On Nanite it runs as
material code inside the rasterizer, after fixed deformation.

**Mechanism and cost.**

- *Bins.* A material with WPO, displacement, custom vertex UVs, pixel discard or pixel depth offset
  is "programmable" and receives its own raster bin with its own compute and hardware shaders
  (`NaniteShared.h`, `NaniteCullRaster.cpp`). The cost of programmable raster is therefore
  per-material fixed overhead (one indirect dispatch and one indirect draw per bin, plus shader
  permutations) on top of the per-vertex material evaluation.
- *Bounds.* Nanite cannot know where WPO will move a vertex, so the primitive declares a maximum
  WPO extent and every instance, node, cluster and patch box is inflated by it before culling
  (`NaniteSceneCommon.ush`, `NaniteClusterCulling.usf`). An undersized extent causes visible
  culling errors; an oversized one defeats occlusion culling.
- *Distance cutoffs.* Per instance, WPO can be disabled beyond a distance; if nothing else requires
  programmable raster the instance then drops to a fixed-function bin. A second per-primitive
  distance does the same for pixel-programmable features (`NaniteCullingCommon.ush`). These exist
  because programmable bins are the expensive case.
- *Compute versus hardware.* Vertex-programmable materials can use either rasterizer.
  Pixel-programmable materials in the compute rasterizer require a 32-lane wave; materials that
  need finite differences must take the hardware path (`NaniteRasterBinning.usf`).
- *Shadow caching.* Whether WPO is active is forwarded to the virtual shadow map invalidation flags
  for each emitted cluster; the coupling is covered in
  [nanite-system-integration.md](nanite-system-integration.md).

**Limitations.** LOD error is computed on the undeformed mesh; the mesh setting for maximum edge
length exists specifically to stop the simplifier from producing triangles too large to deform
well (`EngineTypes.h`). Bounds are an authored guess.

## 4. Spline meshes and landscape

**Spline meshes** are a fixed deformer. The instance payload carries the spline segment
parameters; the vertex function bends position and tangent frame along it
(`NaniteVertexDeformation.ush`). Culling deforms the cluster box approximately and scales the LOD
sphere by the deformation's stretch range (`NaniteClusterCulling.usf`). The source states that this
does not preserve the monotonic nesting of LOD bounds between hierarchy levels, so overlapping or
missing clusters are possible, and that moving splines get no velocity. Source history: default
off and highly experimental in `5.3`, default on from `5.4`.

**Landscape** has no Nanite-specific rasterization or culling path. `ULandscapeNaniteComponent`
(`Engine/Source/Runtime/Landscape/Classes/LandscapeNaniteComponent.h`) is a static-mesh component
whose mesh is generated from the landscape and built like any other Nanite mesh; the only trace in
the Nanite directories is a primitive filter flag. Displacement on landscape is the ordinary
tessellation path of section 1. The 5.3 release notes say Nanite Landscape reaches parity with
normal landscape rendering and does not add resolution. The 5.8 release notes introduce a separate
experimental system, Mesh Terrain, described as a true 3D mesh terrain with variable tessellation
and Nanite whose stated aim is to remove the constraints of heightfield-only landscape; it was not
read in source for this notebook.

## 5. Assemblies

**What it is.** A Nanite Assembly is one resource made of *parts* (complete cluster hierarchies,
each stored once) and *nodes* (a transform, a part reference and optionally bone weights). A tree
is the motivating case: a few branch and leaf-cluster parts instanced thousands of times.

**Build** (`Engine/Source/Developer/NaniteBuilder/Private/NaniteAssemblyBuild.cpp`, `ClusterDAG.cpp`):

1. The node hierarchy is flattened to a list of instance transforms per part. For skinned
   assemblies each node also gets a short bone-influence list, and bone-relative transforms are
   pre-multiplied by the weighted bind pose.
2. Each part's groups and clusters are copied once into the assembly's hierarchy and tagged with
   the part index.
3. For every instance, a reference to the part's *root cluster* plus the instance index is added
   to the input set of the next hierarchy level. From there the normal group, simplify and split
   loop continues over the instanced roots, transforming them into assembly space. Above the parts
   the assembly therefore has its own unique coarse levels (the source calls this the mip tail)
   that merge geometry across instances, which plain scene-level instancing cannot do.
4. Limits: 65,535 transforms per assembly in hierarchy nodes; assemblies of assemblies are
   rejected.

**Run time** (`NaniteClusterCulling.usf`, `NaniteVertexDeformation.ush`): a hierarchy node may
carry an assembly transform index. When traversal enters such a node it loads the transform, for a
skinned mesh blends the node's bone matrices into it and removes shear by re-orthogonalizing, and
writes the result to a per-frame transform buffer (current, previous, and the unskinned original
when skinning is active). Child nodes and visible clusters carry the index of that entry; bounds
are transformed by it for culling, and the unskinned version is used for LOD so that the cut is
stable under animation. The per-frame buffer is capped (262,144 visible parts by default,
`NaniteShared.cpp`).

**Status.** Source: assembly constants appear on `5.6`; the switch `r.Nanite.AllowAssemblies` (0)
and the Foliage project setting appear on `5.7`. Still off by default on all three current
branches. The 5.7 release notes introduce assemblies inside "Nanite Foliage and Skinning
(Experimental)", and the 5.8 Nanite Foliage page, which covers assemblies, is marked experimental.
Documented limits: one level of instancing, each assembly stores its own copy of its parts, and
skeletal parts render only in their bind pose, moved rigidly. Ray tracing of assembly parts was being built on the main branches through
September 2026 (one acceleration structure per part instance), which is outside this notebook.

## 6. Foliage and voxels

"Nanite Foliage" in source is a project switch that turns on three things at once: assemblies,
voxel clusters, and their shader permutations (`RenderUtils.cpp`). Skinning supplies the motion.

**What the voxel representation is.** A voxel cluster is a set of up to 128 *bricks*. A brick is a
4×4×4 block with a 64-bit occupancy mask, a signed 19-bit-per-axis start position on the cluster's
voxel grid, its occupied extent, an offset to its first attribute record and one bone index: 20
bytes (`NaniteDataDecode.ush`). Each occupied voxel owns one attribute record in the cluster's
normal vertex stream (normal, UVs, color, tangent). The voxel edge length is the cluster's LOD
error, so no separate scale is stored.

**When the renderer switches to it.** The choice is made per cluster group at build time, not at
run time (`ClusterDAG.cpp`). For each group the builder forms the parents either by triangle
simplification or by voxelization. Voxelization is tried when the mesh's shape-preservation mode
is "voxelize" or when any child is already a voxel cluster; the starting voxel size is derived
from the group's surface area and a voxel budget of three quarters of the group's vertex count,
is never smaller than the children's error, and grows 10% at a time until the voxel and brick
counts fit the parents. If the resulting voxel size is smaller than the error triangle simplification would
have incurred for the same budget, the parents are voxel clusters with error equal to the voxel
size; otherwise triangles win. An optional setting forces voxels from a given level. Once a level
is voxels, every level above it is voxels. Run-time LOD selection is unchanged: a voxel cluster is
drawn when its error, which is its voxel size, projects to about a pixel. Voxels therefore appear
only where they are roughly pixel-sized, as a replacement for the levels where triangle
simplification of disconnected leaves and twigs stops working.

**How it is built** (`Cluster.cpp`, `NaniteRayTracingScene.h`): candidate voxels are found by
conservatively voxelizing child triangles (or by re-covering child voxels). Each candidate is then
sampled by ray tracing the *original* full-detail geometry with Embree: 64 rays per voxel by
default, from precomputed low-discrepancy sets. The hit fraction is the voxel's coverage, the hit
normals accumulate into a normal distribution, and one hit supplies the attributes by barycentric
interpolation. By default coverage is not stored as opacity. Instead the builder keeps only as
many voxels as the summed coverage, removing the least-covered voxels first and pushing each
removed voxel's coverage and normal distribution to its neighbors, weighted by projected area
toward them, with a stochastic choice of whose attributes survive. Density is thus conserved as a
voxel count, which keeps every surviving voxel opaque and compatible with a depth-tested
visibility buffer. Removed voxels are remembered so that coarser levels still sample their volume.
The fitted normal distribution is stored as an average normal plus one anisotropy value in the
vertex color alpha channel.

**How it is drawn** (`NaniteRasterizer.usf`, voxel permutation): one compute thread per brick. The
thread projects the brick's occupied box to a conservative pixel rectangle. A wave-wide prefix sum
turns all rectangles in the wave into one list of candidate pixels, each of which is first tested
against the current depth; survivors are queued and processed 32 at a time so that all lanes stay
busy. For a surviving pixel the shader builds the view ray in the brick's local space, clips it to
the brick, and steps a DDA through at most ten cells, testing the occupancy mask with a shift. A
hit writes depth and the pair (visible cluster, brick index) with the same 64-bit atomic used for
triangles. Skinned bricks transform the ray by the inverse of their single bone matrix. Voxel
clusters are written in reverse order within their bin, and optionally bucketed by depth, to get
more early depth rejection.

**How it is shaded** (`NaniteVertexDeformation.ush`, `NaniteVertexFactory.ush`): the shading pass
reconstructs the hit point from depth, finds the voxel inside the brick, and counts set bits below
it in the mask to index the voxel's attribute record. The shading normal is drawn per pixel per
frame from the stored normal distribution (a visible-normal sample), which relies on temporal
accumulation. Texture derivatives come from pixel footprint and UV density.

**Limits in source.** Voxel clusters are skipped by ray tracing (no geometry is emitted for them);
skinning is one bone per brick; two-sided sign and world position for voxels are marked as
approximations; the build needs Embree and is much slower than triangle simplification.

**Two things that are not the production path.**
`Engine/Source/Runtime/Renderer/Private/Nanite/Voxel.cpp` and the `Voxel/` shader directory are a
separate experiment, present since `5.3` and excluded from shipping builds: with `r.Voxel` set, it
hashes the *depth buffer* into bricks at a distance-dependent voxel level and redraws them by one
of three methods (box rasterization, tile binning with ray casting, or scatter). It does not read
Nanite voxel clusters. `Engine/Shaders/Private/Nanite/NaniteSVO.ush` is a stack-based sparse voxel
octree traversal (8 levels) that no other file in the Nanite shader directory includes.

**Why Epic went this way.** The 5.8 documentation gives the reasoning directly (Nanite Foliage and
Working with Nanite-Enabled Content pages):

- *Aggregate geometry defeats the core algorithm.* Leaves, needles, grass and hair are many
  disjoint pieces that read as a volume at distance. Nanite depends on merging small triangles
  into larger ones and on occlusion; disjoint pieces cannot merge, and porous layers neither
  occlude nor cull, so they overdraw.
- *Each older foliage technique fails one way.* Alpha-masked cards overdraw and pay for the mask
  function; trees modeled entirely as unique triangles simplify and cull poorly at distance and
  are very large on disk; WPO wind forces authored worst-case bounds and puts every material in
  its own raster bin.
- *Area preservation was a mitigation.* The earlier mode enlarged surviving triangles to stop
  canopies thinning out. In source, `5.7` turns that boolean into an enumeration that adds
  "voxelize" and describes preserve-area as the legacy foliage technique (`EngineTypes.h`).
- *The three systems each remove one failure.* Assemblies address size (the documentation cites
  the largest tree of The Witcher 4 demo going from 3.5 GB to about 29 MB on disk, and one tree's
  streaming memory in one view from about 36 MB to about 2.7 MB). Voxels keep the silhouette and
  density once clusters approach pixel size, and are said to generally perform better in the
  distance than rasterized triangles. Skinning replaces WPO wind so that bounds come from bone
  matrices and everything stays in fixed-function bins.
- *Stated future work.* The normal distribution will be used directly in shading instead of
  stochastic selection, which the page says was already done in the Witcher 4 demo (the source has
  a compile-time analytic path that is switched off), and the distribution data may move out of
  vertex color.

The imposter atlas that Nanite used for very small distant instances was deleted outright in `5.8`.

**Status.** Introduced in the 5.7 release notes as part of "Nanite Foliage and Skinning
(Experimental)"; the 5.8 documentation page is marked experimental and says the feature is off by
default. 5.8.1 and 5.8.2 hotfix notes list a GPU crash in the voxel rasterizer and voxel foliage
not rendering on some platforms. The "Foliage Using Nanite" section of the 5.8 Working with
Nanite-Enabled Content page (the preserve-area path) is still labeled Beta.

## 7. Curves

**What they render.** Hair. The curve input comes from the groom system: the merge that introduced
curves (2026-05-13, both `ue5-main` and `5.8`) adds a Nanite groom asset and builder to the
`HairStrands` plugin and describes Nanite Curves as work in progress, experimental and disabled by
default. Nothing in source routes grass or cables through it.

**Data and LOD** (`ClusterDAG.cpp`, `Cluster.cpp`): a curve is a polyline of points with a radius
attribute. A curve cluster holds whole curves, at most 256 points, with a power-of-two number of
points per curve (at most 32, matched to the wave size). Curves are grouped spatially by their
root points. Coarser levels are made by removing whole curves, greedily by root distance, with the
largest root distance among removed curves as the error. Within a cluster, curves are ordered so
that a prefix is persistent and the rest fade: the rasterizer draws a fraction of the fading
curves that depends on how close the cluster is to its parent's switch point, so strand count
changes continuously.

**Rasterization** (`NaniteRasterizer.usf` curve permutation, `NaniteCurveRaster.inl`,
`NaniteCurve.usf`, `NaniteCurveCommon.ush`): compute only; curve clusters are never sent to the
hardware path. One thread handles one curve point, and with it the segment to the next point. In
the default path each point is pushed sideways by its radius, perpendicular to the strand tangent
and the view direction, and the resulting camera-facing quad per segment is fed as two triangles
to the software triangle rasterizer, writing depth and (cluster, segment) to the visibility
buffer; shadow views widen strands to at least one pixel. A compile-time alternative walks a
one-pixel line with a 2D DDA and thins sub-pixel strands by dithering on projected radius. A
second, optional *tiled* path (`r.Nanite.Curve.TiledRasterization`, default 0) bins segments into screen tiles of
capacity 128 with count, prefix-sum and write passes, then resolves each tile with per-pixel
coverage and deduplication before translating to the visibility buffer. The tiled path requires
64-bit image atomics and is explicitly disabled on Metal and Vulkan.

**Deformation** (`Engine/Source/Runtime/Renderer/Private/CurveSkinning/`, `ue5-main` and
`ue6-main` only): added in June 2026 after 5.8 branched. A scene extension keeps rest and
double-buffered deformed point buffers per groom; providers write deformed points in compute; the
vertex function reads the deformed point by curve index. The only solver in the directory is a
debug wave. Curves are not skinned by bones.

**Status.** New in `5.8`, off by default, labeled experimental in the merge message. No release
note or documentation page announces a feature called Nanite curves or Nanite hair; the only
public traces are two bullets under Chaos Hair in the 5.8 release notes ("Nanite compatible
rendering and simulation", "Prototype strand rendering") and a 5.8.2 hotfix line that names voxel
and curve clusters. Curves cannot combine with voxels or with triangles in one mesh, and ray
tracing skips them.

## 8. Translucency

**What kind of translucency it is.** In `5.8` this is forward translucency whose *geometry
submission* comes from Nanite. It is not a visibility-buffer technique and it does not make
translucent surfaces part of the opaque Nanite pass.

**Mechanism** (`NaniteTranslucency.cpp`, `NaniteTranslucency.usf`, `NaniteTranslucencyFactory.ush`,
`NaniteRasterBinning.usf`):

1. A material flagged translucent gets its own raster bins. The normal Nanite cull pass fills them
   with visible clusters at the selected LOD, forced to the hardware classification.
2. A small compute pass rewrites each bin's indirect arguments into the layout of the hardware
   path in use (instanced vertex-shader draw of up to 384 vertices per cluster, or mesh shader).
3. In the engine's translucency passes, each bin is drawn with the ordinary translucent base-pass
   pixel shader and blend state through a dedicated vertex factory that decodes cluster vertices,
   so lighting and material behavior match non-Nanite translucency. Depth is tested, not written.
4. Bins are drawn in bin order and clusters in the order the cull emitted them. No per-object or
   per-triangle sort of Nanite translucent geometry was found in the pass, which fits its pairing
   with order-independent transparency.

**Relation to OIT.** `5.8` has only the older sorted-pixels OIT
(`Engine/Source/Runtime/Renderer/Private/OIT/` holds three files there), and a 5.8 commit fixes its
composition with "experimental Nanite translucency". `AVBOIT`, an implementation of Drobot's
Adaptive Voxel-Based Order Independent Transparency (SIGGRAPH 2025 Advances), landed on `ue6-main`
on 2026-08-19 and on `ue5-main` on 2026-09-18, the day `ue6-main` also received its Nanite
extinction-splat additions: six new files in the same directory, one of them an in-tree design
note, plus a new `Engine/Shaders/Private/AVBOIT/` folder. From the commit message and note:

- It needs no raster-ordered views, only integer atomics on textures.
- A view-aligned froxel volume stores extinction. Depth slices are allocated adaptively: occupancy
  of fine virtual slices is marked from translucent bounds and compacted by prefix sum into a
  smaller number of physical slices through a 1D warp table.
- All translucent geometry is first drawn at reduced resolution (one eighth by default) with a slim
  shader that adds extinction into the volume atomically; a compute pass integrates each froxel
  column front to back into a transmittance table.
- The full-resolution translucent pass then draws in any order, looks up transmittance in front of
  each fragment, and accumulates weighted color, weight and extinction into additive targets; a
  resolve composites over the background.
- Nanite participates twice: the reduced-resolution splat replays the translucent raster bins
  (through the hardware path, or optionally through a compute "extinction splat" permutation for
  small clusters), and the full-resolution pass uses the AVBOIT pixel shader variant.
- Defaults, as of 2026-10-01 on both main branches: the shader permutations compile by default
  (`r.AVBOIT.Support` 1; the first `ue6-main` landing had it off), the run-time selector
  `r.OIT.Method` was set to AVBOIT at the 2026-09-18 landing and reset to 0 (none) on 2026-09-23 on
  both branches to sort out thin-translucency issues, and when selected it applies to Nanite
  translucency by default (`r.AVBOIT.Nanite` 1) and to non-Nanite translucency only on request
  (`r.AVBOIT.NonNanite` 0). The wave-level splat merge needs SM6 wave operations.

Follow-up commits in the same week let masked-shadow translucent Nanite materials cast shadows,
honor two-sided materials, and add a run-time toggle.

**Status.** New in `5.8`, off by default, described in the 5.8 switch text as heavy work in
progress. It is not announced anywhere: the 5.8 documentation still states that Nanite supports
only the Opaque and Masked blend modes, and the 5.8 release notes mention translucency on Nanite
once, as a log-spam fix. AVBOIT is post-5.8 and on the main branches only.

## 9. Other non-static, non-opaque paths found while listing files

| Item | What it is | Where |
|---|---|---|
| Imposters | Removed. The 2026-05-13 merge deletes the imposter atlas builder and shader and says the code was already fully disabled | `NaniteBuilder` and shader directory history; absent on `5.8` and later |
| Material cache | A rasterization mode that draws Nanite clusters unwrapped in UV space to fill a cached material texture; culling tests cluster UV ranges against the requested rectangle | `MATERIAL_CACHE` permutations in `NaniteCullRaster.cpp`, `NaniteClusterCulling.usf`; `Renderer/Private/MaterialCache/` (prototype dated 2025-03) |
| First-person rendering | Per-primitive first-person transform applied to bounds and vertices, with an interpolation alpha as a vertex-programmable feature | `NaniteClusterCulling.usf`, `NaniteDefinitions.h` |
| Owner-only visibility | A per-view bit mask that hides primitives flagged owner-no-see or only-owner-see | `NaniteOwnershipVisibilitySceneExtension.h` |
| Dynamic wind | Bone-driven wind for Nanite foliage through a skinning transform provider; the plugin describes itself as extremely experimental. Culling of batched animations was added 2026-07 | `Engine/Plugins/Experimental/DynamicWind`, commit subjects on the shader path |
| Stream-out and cluster ray tracing | Writing the selected cut to buffers for acceleration structures; heavy activity in 2026 | `NaniteStreamOut.cpp`, `NaniteRayTracing.cpp`; see [nanite-system-integration.md](nanite-system-integration.md) |
| Work-graph materials | A default-off switch for shading through work graphs | `r.Nanite.AllowWorkGraphMaterials` in `NaniteResources.cpp` |

## 10. `ue6-main` compared with `ue5-main`

**Branch relationship.** GitHub's compare endpoint reports the merge base at 2026-06-10, with
`ue6-main` 12,812 commits ahead and 2,476 behind `ue5-main` on 2026-10-01. `ue6-main` is the line
where work lands first; `ue5-main` receives selected changes later, often in batches (twenty-odd
Nanite ray tracing commits dated June to August on `ue6-main` arrive on `ue5-main` on 2026-09-25
and 2026-09-26).

**Directory listings.**

| Root | Entries `ue5-main` / `ue6-main` (subfolders counted) | Differences |
|---|---|---|
| `Renderer/Private/Nanite/` | 38 / 38 | Same names. Larger on `ue6-main`: `NaniteMaterialsSceneExtension.*` (27.8 KB to 50.8 KB), `NaniteRayTracing.*` (194.8 KB to 229.3 KB); small changes in a dozen others |
| `Shaders/Private/Nanite/` | 53 / 54 | `ue6-main` adds `NaniteRayTracingInstances.usf`; `Voxel/` identical |
| `NaniteBuilder/Private/` | 16 / 16 | Same names; small changes in `ClusterDAG.*` and `NaniteBuilder.cpp` |
| `Engine/.../Rendering/Nanite*` | 9 / 7 | `ue6-main` removes `NaniteCoarseMeshStreamingManager.*` |

Every file specific to an extension in this notebook is byte-size identical on the two branches:
`TessellationTable.cpp`, `NaniteDice.ush`, `NaniteSplit.usf`, `NaniteTessellation.ush`,
`NaniteDisplace.cpp`, `NaniteAssemblyBuild.cpp`, `Voxel.cpp`, `NaniteSVO.ush`, `Voxel/*`,
`NaniteCurveRaster.inl`, `NaniteCurve.usf`, `NaniteCurveCommon.ush`, `NaniteVertexDeformation.ush`,
`NaniteSkinningUpdateViewData.usf`. `NaniteDefinitions.h` has the same definitions on both.

**What `ue6-main` has that `ue5-main` does not, from commit subjects since the merge base** (100
most recent commits per path, read on 2026-10-01):

- A renderer-wide refactor that moves per-primitive state out of the scene object into "scene
  data" arrays and scene extensions. For Nanite this moves raster and shading bin lifetime into the
  materials scene extension and converts visibility and custom-depth tracking to persistent
  primitive indices. This is the reason for most file-size differences.
- Skinned-mesh LOD and raster-choice fixes for root motion (section 2), and vertex colors with
  Nanite skinning.
- Ray tracing work not yet merged back: assembly parts instanced in the top-level acceleration
  structure, per-part capacity tracking, and (2026-09-29) a skinning cache for instanced skinned
  meshes under `Renderer/Private/Skinning/`.
- Shader permutation cleanup for programmable raster, and a change of the pixel-programmable
  compute path to one large thread group.
- Optional sub-allocators for GPU scene and Nanite material data, with defragmentation.
- Builder instrumentation: a mesh-statistics commandlet and a fingerprint commandlet for chasing
  build non-determinism.

**What is the same.** AVBOIT and the September translucency fixes are on both. Voxel rasterizer
optimizations of 2026-06-10 are on both. Nothing in the Nanite paths of `ue6-main` introduces a new
primitive type, a new hierarchy format or a new rasterizer. Outside the Nanite directories,
`ue6-main`'s renderer has directories that `ue5-main` lacks (`LumenPT`, `DistanceField`,
`StaticMeshes`, `Velocity`); these are out of scope here. A code search of the repository for
"Nanite v2", "Nanite 2.0", "NaniteV2" and "Nanite2" returns nothing; the research stream that fed
curves and translucency is named `Dev-NaniteResearch` in the merge message.

**`5.8` compared with `ue5-main`.** The release branch has the same file lists. It lacks AVBOIT,
curve skinning, the 2026 cluster ray tracing rework and far-shadow visualization; its
`NaniteTranslucency.cpp` is 19.9 KB against 42.9 KB.

## 11. Version status and Epic's public statements

This section is public record only: release notes, documentation, the public roadmap and Epic's
posts. What the source shows is in sections 1 to 10. Pages under `dev.epicgames.com` were fetched
live; `unrealengine.com` news pages returned HTTP 403 and were read through Wayback Machine
captures, which are named in Sources.

### Status labels by release

| Feature | Introduced, with Epic's label | Label in 5.8 |
|---|---|---|
| Offline displacement ("static displacement mapping") | 5.2 roadmap card: precomputed displacement from a static texture map, Beta **[UNVERIFIED]** | Documentation: experimental |
| Run-time tessellation | 5.4 release notes: "Nanite - Tessellation (Experimental)". 5.5 adds an explicit per-material "Enable Tessellation" option | Documentation: experimental. 5.8 notes add switches to turn tessellation off in virtual shadow maps |
| Spline meshes | 5.3 release notes: experimental, opt-in, with a warning that enabling it costs culling performance for all Nanite | 5.4 release notes: an unlabeled "Nanite - Spline Mesh" entry that lists remaining work (performance, memory, cracks, level streaming, transform caching); the switch default becomes 1 in source. No release note read here calls it production-ready |
| Landscape | 5.3 release notes: "Nanite Landscape", no label | No label. 5.8 introduces Mesh Terrain as experimental |
| Skinned meshes | 5.5: "currently an Experimental feature" (Unreal Fest Seattle 2024 post; 5.5 and 5.6 documentation) | No label on the 5.7 or 5.8 documentation page; 5.7 notes group it under an experimental heading; never declared production-ready |
| Assemblies, voxels (Nanite Foliage) | 5.7 release notes: "Nanite Foliage and Skinning (Experimental)" | Documentation: experimental, off by default |
| Curves | Not announced | Not announced; indirect mentions only (section 7) |
| Translucency | Not announced | Not announced; documentation lists only Opaque and Masked |
| WPO and programmable raster | 5.1 roadmap card (programmable raster for masked materials, two-sided foliage, pixel depth offset, WPO) **[UNVERIFIED]**; 5.2 adds the maximum WPO displacement setting | Supported; documentation calls WPO support limited |

Eight releases after 5.0, no release note or documentation page read here labels any of these
extensions production-ready. Spline meshes (5.4 release notes) and skinned meshes (5.7
documentation) lost the experimental label without a replacement; tessellation has carried it for
five releases.

### Public roadmap

**[UNVERIFIED]** The Productboard roadmap has tabs for 5.0 through 5.8 and nothing later: no forward-looking tab
and no Unreal Engine 6 tab. The 5.8 tab has no card with Nanite in its title. Nanite-titled cards
are "Nanite Foliage & Skinning (Experimental)" and "Niagara Nanite Renderer" on 5.7; "Nanite -
Tessellation (Experimental)", "Nanite - Spline Mesh", "Nanite - Optimized Shading" and "Landscape
Nanite Automatic Async Build" on 5.4; "Nanite", "Nanite Landscape" and "Support for Nanite on
Apple M2 Devices (Beta)" on 5.3; and general Nanite cards on 5.1 and 5.2.

### Unreal Engine 6

What Epic has published (State of Unreal roundup dated 2026-06-17, and "The road to Unreal Engine
6" dated 2026-06-22):

- Unreal Engine 6 is in development and is the unification of UE5 and Unreal Editor for Fortnite
  into one product.
- Its three stated initiatives are a Verse-based gameplay programming model, portable and
  interoperable content through open standards, and model-assisted pipeline tooling.
- On rendering the commitment is one sentence in each post: rendering will keep getting better,
  and core functionality including rendering will continue to improve. Neither post contains the
  word Nanite.
- Early Access is targeted for the end of 2027, with full release 12 to 18 months after.
- A UE6 development stream is public on GitHub and is explicitly not an alpha. The post says
  remaining UE5 changes merge into UE6 and not the reverse. The commit history in section 10
  shows individual changes still being cherry-picked back to `ue5-main`.
- 5.8 is the last planned major UE5 release, with a 5.9 held in reserve.

**"Nanite v2".** No Epic page fetched for this notebook uses "Nanite v2", "Nanite 2" or a similar
name: not the release notes for 5.3 to 5.8, not the Nanite documentation, not the public roadmap,
not the State of Unreal 2026 roundup and not the UE6 post. A repository code search finds no such
identifier. The term appears in third-party articles: a vendor article dated 2026-07-14 says the
next step is "informally referred to across the industry as Nanite v2", lists expected features,
cites no Epic source and labels its own list an expectation. The State of Unreal 2026 keynote video was not
transcribed, so spoken use on stage is unchecked. Treat the name as community terminology with no
confirmed technical content.

**What Epic has said about Nanite's direction, in specifics.**

- Foliage documentation (5.8): normal distributions will feed shading directly; assembly parts
  will later support instanced animation; the voxel build settings are expected to change or be
  removed.
- Karis's blog (2026): higher-order surfaces may be supported by the tessellation path later; a
  hardware raster path for patches may become necessary because of the stretching limit.
- 5.8 release notes: Mesh Terrain as the intended successor to heightfield landscape; the
  Procedural Vegetation Editor growing Nanite-ready trees in the editor.

**Talks.** Epic's Nanite extension material since 2023 is on video, not in course notes. Titles
and dates were confirmed; contents were not transcribed.

| Talk | Venue, upload date | URL |
|---|---|---|
| An Artist's Guide to Using Nanite Tessellation | Unreal Fest 2024, 2024-07-31 | `youtube.com/watch?v=6igUsOp8FdA` |
| The Witcher 4 Unreal Engine 5 Tech Demo | State of Unreal 2025, 2025-06-03 | `youtube.com/watch?v=Nthv4xF_zHU` |
| The Road to 60 fps in The Witcher 4 UE5 Tech Demo | Unreal Fest Orlando 2025, 2025-10-31 | `youtube.com/watch?v=ji0Hfiswcjo` |
| Large Scale Animated Foliage in The Witcher 4 UE5 Tech Demo | Unreal Fest Stockholm 2025, 2025-11-26 | `youtube.com/watch?v=EdNkm0ezP0o` |
| The Future of Nanite Foliage | Unreal Fest Stockholm 2025, 2025-12-06 | `youtube.com/watch?v=aZr-mWAzoTg` |
| Introducing Mesh Terrain | Unreal Fest Chicago 2026, 2026-07-18 | `youtube.com/watch?v=QJwTTmNez3k` |

The SIGGRAPH "Advances in Real-Time Rendering" course pages for 2023 through 2026 list no Epic
talk about Nanite. Epic's 2025 entry was MegaLights; the 2025 course is where AVBOIT was
presented, by Activision. There is therefore no slide deck from Epic on voxels, assemblies,
skinning or curves; the documentation, the blog series and the source are the technical record.

## Corrections to earlier research

| Earlier statement | Where | Finding |
|---|---|---|
| Unreal Engine 6 was announced in June 2026 "with Nanite v2 and Lumen 2.0" | `2026-09-14-rendering-direction-review.md` section 1.1 table; `studio-and-engine-disclosures-2023-2026.md` UE6 row | The announcement, the Chicago venue, the 2026-06-17 date and the end-of-2027 Early Access target are confirmed by Epic's own pages. "Nanite v2" and "Lumen 2.0" are not: neither page cited by that row contains them. Epic's roundup does not mention Nanite at all, and the 80.lv article (dated 2026-08-14) is about a community member's observation of a new Lumen mode on `ue6-main` and says nothing about Nanite. The row's description of what "Nanite v2" does has no source |
| Nanite Foliage is "generally UE5.4-era" | `pipeline-state-of-the-art-m7-m11.md` section 6 | Wrong. Introduced in 5.7 as experimental (release notes); voxel and assembly code first appears on the `5.6` branch; still experimental and off by default in 5.8.3 |
| Nanite Tessellation is "generally UE5.5–5.6-era" | same | Wrong. Introduced in 5.4 as experimental (release notes); code present but default-off on `5.3`; still documented as experimental in 5.8 |
| Nanite skinned meshes: "Shipped (5.5+)", and they "GPU-skin vertices before Nanite cluster culling/raster" | `studio-and-engine-disclosures-2023-2026.md` | The version is right, the status and mechanism are not. Epic labeled it experimental in 5.5 and 5.6 and has never labeled it production-ready. There is no pre-skinning pass: vertices are skinned inside cluster vertex decode during rasterization and again during shading, and culling uses bone-transformed bounds |
| Nanite Tessellation "regressed in 5.6+ ... open bug UE-2347891" | same | Not confirmed. The only source is a third-party blog, and no release note or Epic page read here mentions such a regression. The "shipped" status should read experimental |
| AVBOIT is an Activision technique "named as a later study" | `studio-and-engine-disclosures-2023-2026.md`; roadmap M8.4 | Still correct, with an addition: Epic's own implementation landed on `ue6-main` on 2026-08-19 and on `ue5-main` on 2026-09-18, with an in-tree design note; it serves Nanite and non-Nanite translucency, with only the Nanite path on by default, and since 2026-09-23 neither branch selects the technique by default |

## Implications for Luminex

**Facts tied to the project's constraints.**

1. *Most extensions depend on machinery M9 defers.* Tessellation, voxels and curves exist only in
   the compute rasterizer and only because pixels are written to a visibility buffer with 64-bit
   atomics and reconstructed later. M9 as accepted defers software rasterization and treats the
   visibility buffer as a separate comparison, and the RHI exposes neither 64-bit atomics nor mesh
   shaders. None of the three is reachable from M9 without first changing that.
2. *The deformers are ordinary on a hardware path.* Skinning, spline deformation and WPO need
   three things from a cluster renderer: deformation in the cluster vertex function, conservative
   bounds, and a previous-frame position. With M9's indirect cluster raster the first is a vertex
   shader, the second is a union of bone-transformed boxes or an authored extent, and the third
   reuses the M6.1 motion contract. The obstacle is that Luminex has no skinning at all, and no
   milestone in the roadmap introduces it.
3. *M9's material scope equals Nanite's documented scope.* The 5.8 documentation supports Opaque
   and Masked on Nanite and nothing else. In the accepted order M9 lands before M8.4 introduces
   blended transparency, so M9 is opaque and masked by construction. That is the same boundary
   Epic ships by default in 5.8.
4. *Nanite translucency is forward transparency fed by the cluster cull.* It adds no
   visibility-buffer dependency. For Luminex it reduces to a rule: once both clusters and M8.4
   exist, translucent materials draw their visible clusters through the transparent pass.
   Ordering is then the same unsolved problem as for any transparent geometry, which is why Epic
   paired it with order-independent transparency.
5. *Cluster LOD is expected to struggle on the foliage Luminex already has.* Epic's documentation says
   disjoint leaves thin out under simplification and that porous layers defeat occlusion culling,
   and it took area preservation, then voxels, to address it. San Miguel's alpha-masked foliage is
   that case. A hierarchical-simplification M9 should measure it as a known-hard workload and not
   expect a win there.
6. *Offline displacement is a tool, not a renderer feature.* Karis's own argument is that
   simplification of an offline-displaced mesh is the general solution and run-time tessellation
   is a storage and authoring optimization that costs more per frame. Luminex has no storage
   pressure and bakes offline already.

**Closing table.** "Filing" is this notebook's suggestion to the coordinator.

| Extension | Prerequisites inside Nanite | Could a solo Metal 4 renderer build it? | What Luminex needs first | Filing |
|---|---|---|---|---|
| Offline displacement bake | An adaptive tessellator in the builder; nothing at run time | Yes. Pure tool work | The M9 geometry bake tool | Optional slice of the bake milestone; doubles as a generator of dense test content |
| Run-time tessellation | Compute rasterizer and 64-bit visibility buffer; per-material compute raster shaders; wave-level work balancing; shading that re-derives base-triangle barycentrics from depth; authored displacement range | Not in this form. Hardware tessellation or mesh shaders would be a different technique with a different cost model; see [beyond-triangle-clusters.md](beyond-triangle-clusters.md) | A software rasterizer and visibility-buffer shading; a material path that can evaluate displacement at raster time | Out of scope for M9. Gated research item behind a software-raster decision |
| Skinned clusters | Bone-matrix pool with previous frame; per-cluster and per-node bone lists; skinning in vertex decode; screen-size animation cutoff | Yes on the hardware path; the cluster-specific part is small | Skinning itself: glTF skin import, pose evaluation, bone buffer, previous-pose motion vectors, shadow invalidation for moving casters. Then bone lists in the bake | Future milestone, after a skinning slice that does not involve clusters. The roadmap has no owner for skinning today |
| WPO-style vertex deformation | Per-material raster shaders; authored maximum offset; distance cutoffs; shadow-cache invalidation | Yes on the hardware path, as a fixed set of deformers (Luminex has no material graph) | A deformation contract: declared extent that inflates bounds, and previous-frame evaluation | Gated research item; trigger is animated vegetation or procedural motion content |
| Spline meshes | Fixed deformer with approximate bounds | Yes, but no content needs it | Nothing | Out of scope |
| Landscape | A mesh generated from a heightfield | Yes; it is content generation | Terrain content | Out of scope for the geometry milestones |
| Assemblies | A cluster builder that keeps simplifying across instanced part roots; hierarchy nodes with a transform index; a per-frame transform buffer | Plausible only with a builder and hierarchy format Luminex controls. Scene-level instancing already gives the memory saving for rigid parts; what it cannot give is LOD across the instances | The M9 hierarchy at run time, and evidence that a maintained clustering library can or cannot express instanced children (see [cluster-lod-construction.md](cluster-lod-construction.md)) | Gated research item; trigger is one asset whose repeated parts dominate memory |
| Voxel clusters | Build-time ray tracing of the source mesh; compute rasterizer with atomic depth; visibility-buffer shading; stochastic normals with temporal accumulation; skinning for motion only (the voxel and assembly switches are independent) | No in the near term. Every prerequisite is absent | Software rasterizer, visibility buffer, an offline ray tracer in the baker | Out of scope. Record it as the known answer to aggregate-geometry LOD |
| Curves | Compute rasterizer; strand assets; strand deformation | No | Hair content and everything above | Out of scope |
| Translucency (5.8 form) | Translucent raster bins from the cluster cull; the engine's forward translucent pass | Yes | M8.4 blended transparency and M9 cluster culling | A requirement on whichever of M8.4 and M9 lands second, not a milestone |
| AVBOIT | 32-bit integer atomics on 3D textures (the extinction volumes are `Texture3D<uint>`); a reduced-resolution splat pass; a transmittance integration pass | Plausible. [apple-metal-geometry-constraints.md](apple-metal-geometry-constraints.md) records 32-bit `R32Uint`/`R32Sint` texture atomics from Apple6 without saying whether 3D textures are covered; the wave-level merge of splats is optional in Epic's version | The M8.4 sorted baseline to compare against | Already a named later study in [M8.4](../../roadmap/gpu-driven-hybrid-rendering.md); keep it there |

**Where the two gaps matter.** *No skinning* blocks skinned clusters directly and blocks the
animated half of assemblies, voxels and foliage wind indirectly, because all of them read the same
bone-matrix pool. Static assemblies and static voxels do not need it. *No blended transparency*
blocks only translucency and AVBOIT; it has no bearing on any opaque extension.

**What I would do.**

- Keep M9 to static rigid opaque and masked triangle clusters, and name the exclusions in its
  Defer line: run-time tessellation, deforming geometry, assemblies, voxel and curve primitives,
  translucent clusters.
- Give the baked cluster format three cheap provisions that these extensions would otherwise force
  a format break for: a cluster type tag, a per-mesh deformation extent added to every bound, and
  room for a per-cluster bone list. Whether that is prudent or premature is the coordinator's call;
  the cost is a few bytes per cluster and one add in the culling shader.
- If animated geometry is wanted at all, give skinning its own small milestone independent of
  geometry LOD. Nanite added skinned clusters five releases after static ones (5.5 against 5.0)
  and reused the hierarchy, culling passes and rasterizers, which suggests the order static
  clusters, then plain skinning, then skinned clusters loses nothing.
- Treat the software rasterizer as the real gate. If the direction review decides to build one,
  tessellation and voxels become gated research; if not, they stay out of scope and no roadmap
  text needs to mention them beyond the exclusion.

## Open questions and what could not be confirmed

- Whether anyone at Epic said "Nanite v2" on stage. The State of Unreal 2026 keynote and the
  Unreal Fest talks listed in section 11 were not transcribed; only titles, dates and written
  pages were checked.
- The production status of skinned Nanite meshes in 5.8. The documentation dropped the
  experimental label in 5.7 without a release note saying why.
- How these extensions behave on Metal in Unreal itself. Only one Metal-specific gate was seen in
  the files read (tiled curve rasterization is disabled on Metal). Platform support belongs to
  [nanite-system-integration.md](nanite-system-integration.md).
- Whether Metal Shading Language offers equivalents for the wave intrinsics that dicing,
  splitting, voxel tracing and curve rasterization rely on. **[UNVERIFIED]**; see
  [apple-metal-geometry-constraints.md](apple-metal-geometry-constraints.md).
- The internals of the three skinning transform providers and of the groom-to-curve builder in the
  hair plugin were not read. Whether curve LOD compensates strand radius when it removes strands
  was not determined.
- The documentation says the Witcher 4 demo shaded voxels directly from the normal distribution;
  the corresponding compile-time path is disabled on all three branches read. Which code the demo
  ran is unknown.
- The remaining posts of the tessellation blog series (vertex deduplication, visibility buffer and
  deferred materials, wrap-up) were announced but not all published when read.
- Mesh Terrain (5.8, experimental) was noted from release notes only.
- The Productboard roadmap is a scripted page; the tab and card listing in section 11 was not
  confirmed by a second fetch and is marked **[UNVERIFIED]**.
- Which shader platforms and feature levels compile the AVBOIT permutations was not determined;
  only the SM6 requirement of the optional wave merge was read.
- Commit subjects were read for the 100 most recent commits per path; earlier `ue6-main`-only
  changes on the builder path may exist beyond that window.

## Sources

Unreal Engine source, `EpicGames/UnrealEngine`, read 2026-10-01 on `ue5-main`, `ue6-main` and `5.8`
(5.8.3), with `5.3` to `5.7` consulted for history where stated:

- `Engine/Source/Runtime/Renderer/Private/Nanite/`: `NaniteCullRaster.cpp`, `NaniteShared.h`,
  `NaniteShared.cpp`, `TessellationTable.cpp`, `Voxel.cpp`, `Voxel.h`, `NaniteCurveRaster.inl`,
  `NaniteTranslucency.cpp`, `NaniteTranslucency.h`, `NaniteOwnershipVisibilitySceneExtension.h`
- `Engine/Shaders/Private/Nanite/`: `NaniteTessellation.ush`, `NaniteDice.ush`, `NaniteSplit.usf`,
  `NaniteRasterizer.usf`, `NaniteRasterBinning.usf`, `NaniteRasterizationCommon.ush`,
  `NaniteClusterCulling.usf`, `NaniteCullingCommon.ush`, `NaniteDataDecode.ush`,
  `NaniteVertexDeformation.ush`, `NaniteVertexFactory.ush`, `NaniteSkinningUpdateViewData.usf`,
  `NaniteSceneCommon.ush`, `NaniteSVO.ush`, `NaniteCurve.usf`, `NaniteCurveCommon.ush`,
  `NaniteTranslucency.usf`, `NaniteTranslucencyFactory.ush`, `NaniteClusterRayTracing.usf`,
  `Voxel/AutoVoxel.usf`, `Voxel/Brick.ush`
- `Engine/Shaders/Private/WorkDistribution.ush`, `Engine/Shaders/Shared/NaniteDefinitions.h`
- `Engine/Source/Developer/NaniteBuilder/Private/`: `NaniteBuilder.cpp`, `NaniteDisplace.cpp`,
  `NaniteAssemblyBuild.cpp`, `ClusterDAG.cpp`, `Cluster.cpp`, `Cluster.h`,
  `Encode/NaniteEncodeSkinning.cpp`
- `Engine/Source/Runtime/Renderer/Private/Skinning/`, `.../CurveSkinning/`, `.../OIT/` (including
  `AVBOIT.md`), `.../MaterialCache/` (listing only)
- `Engine/Source/Runtime/Engine/Private/Rendering/NaniteResources.cpp`,
  `Engine/Source/Runtime/Engine/Classes/Engine/EngineTypes.h`,
  `Engine/Source/Runtime/Engine/Classes/Engine/RendererSettings.h`,
  `Engine/Source/Runtime/RenderCore/Private/RenderUtils.cpp`,
  `Engine/Source/Runtime/Landscape/Classes/LandscapeNaniteComponent.h`,
  `Engine/Plugins/Experimental/DynamicWind/DynamicWind.uplugin`
- Commit subjects and selected commit messages on the three Nanite roots for `ue5-main`,
  `ue6-main` and `5.8`, the GitHub compare endpoint for branch relationships, the releases list for
  dates (5.8.0 on 2026-06-17, 5.8.3 on 2026-09-22), and repository code search

Epic documentation and release notes (fetched live):

- https://dev.epicgames.com/documentation/en-us/unreal-engine/nanite-virtualized-geometry-in-unreal-engine?application_version=5.8
  (also read with `application_version` 5.4, 5.5, 5.6 and 5.7)
- https://dev.epicgames.com/documentation/en-us/unreal-engine/working-with-naniteenabled-content?application_version=5.8
- https://dev.epicgames.com/documentation/en-us/unreal-engine/nanite-foliage?application_version=5.8
- https://dev.epicgames.com/documentation/en-us/unreal-engine/nanite-assemblies?application_version=5.8
- https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-5.3-release-notes?application_version=5.3
- https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-5.4-release-notes?application_version=5.4
- https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-5-5-release-notes
- https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-5-6-release-notes?application_version=5.6
- https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-5-7-release-notes?application_version=5.7
- https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-5-8-release-notes?application_version=5.8
- https://portal.productboard.com/epicgames/1-unreal-engine-public-roadmap
- https://forums.unrealengine.com/t/unreal-engine-5-8-released/2729274 (hotfix notes)

Epic posts (live URLs returned HTTP 403; read through Wayback Machine captures):

- https://www.unrealengine.com/news/state-of-unreal-2026-top-news-from-the-show (capture 2026-09-08)
- https://www.unrealengine.com/news/the-road-to-ue-6 (capture 2026-08-20)
- https://www.unrealengine.com/news/unreal-engine-5-8-is-now-available (capture 2026-09-27)
- https://www.unrealengine.com/en-US/blog/catch-up-on-the-big-news-from-unreal-fest-seattle-2024
  (capture 2026-01-11)

Author's blog (Brian Karis; personal site, not an Epic publication):

- https://graphicrants.blogspot.com/2026/02/nanite-tessellation.html
- https://graphicrants.blogspot.com/2026/02/possible-approaches-for-tessellation.html
- https://graphicrants.blogspot.com/2026/02/how-to-tessellate.html
- https://graphicrants.blogspot.com/2026/02/nanite-reyes.html
- https://graphicrants.blogspot.com/2026/03/variable-sized-work.html

Course pages and reported coverage:

- https://advances.realtimerendering.com/s2025/index.html (AVBOIT, Drobot, Activision; also the
  2023, 2024 and 2026 course pages)
- https://80.lv/articles/unreal-engine-6-will-introduce-new-lumen-mode (reported; checked for the
  correction above)
- https://gamestudio.n-ix.com/what-is-nanite-in-unreal-engine/ (reported; the one located written
  use of "Nanite v2")
