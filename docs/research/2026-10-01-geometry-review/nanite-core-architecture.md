# Nanite core architecture: static opaque meshes in current Unreal source

**Status**: Frozen — non-normative  
**Research date:** 2026-10-01

Evidence notebook for the [geometry direction review](../2026-10-01-geometry-direction-review.md),
describing how Nanite builds, encodes, culls, rasterizes and shades static opaque meshes. It was
collected by reading the `EpicGames/UnrealEngine` source on branch `ue5-main` on 2026-10-01, with
spot comparisons against `5.8`, `ue6-main` and the release branches `5.0` to `5.7`, plus the public
2021 and 2024 Epic talks. Mechanisms are described in this notebook's own words; no engine code,
shader text or comments are reproduced. Items marked **[UNVERIFIED]** were not confirmed against a
primary source; recheck them before a plan depends on them.

Citation convention: a bare path such as `NaniteBuilder/Private/ClusterDAG.cpp` means that file
under the roots listed in [Sources](#sources), on `ue5-main`, read on 2026-10-01, unless another
branch is named. "The 2021 talk" is Karis, Stubbe and Wihlidal's SIGGRAPH 2021 course talk; "the
2024 talk" is Wihlidal's GDC 2024 talk on GPU-driven materials.

Credit: Nanite and the other Unreal Engine systems described here are the work of Epic Games.
Unreal Engine source is copyright Epic Games, Inc. and was read under the Unreal Engine EULA; this
notebook describes mechanisms in its own words and reproduces no engine code, shader text or
comments. Unreal, Unreal Engine and Nanite are trademarks of Epic Games, Inc.

## Branches and what differs between them

The static opaque path is the same design on all three current branches. The numeric limits that
define it (128 triangles and 256 vertices per cluster, groups of 8 to 32 clusters, hierarchy
fan-out 4, 32 KB root pages, 128 KB streaming pages, 8 sub-pixel bits, 2^24 instances) have the
same values in `5.0`, `5.4`, `5.8` and `ue5-main` (`Engine/Shaders/Shared/NaniteDefinitions.h` on
each branch), except that the `5.0` header does not yet define the sub-pixel constant, which was
checked from `5.4` on.

| Compared | Finding | Evidence |
|---|---|---|
| `ue6-main` vs `ue5-main` | `NaniteDefinitions.h`, `GraphPartitioner.cpp`, `Cluster.cpp`, `NaniteHierarchyTraversal.ush`, `NaniteWritePixel.ush`, `NaniteRasterizer.ush` and `NaniteShadeBinning.usf` are byte-identical. The differences found are scene-extension refactors, skinned LOD bounds, and the hardware/software decision using a sphere derived from the culling box instead of the LOD sphere. | File diffs of the fetched copies, 2026-10-01 |
| `5.8` vs `ue5-main` | Core files identical or near-identical. `ue5-main` adds a software-raster permutation for translucent extinction splatting, ray-tracing flags on streaming requests and clusters, and curve skinning. | Same |
| `5.7` vs `5.8` | Impostor atlas code (`r.Nanite.ImposterMaxPixels`) exists through `5.7` and is absent from `5.8` and `ue5-main`. | `NaniteCullRaster.cpp` on each branch |
| `5.3` vs `5.4` | Persistent-thread traversal stops being the default; compute shading becomes the default on most platforms (see below). | `NaniteCullRaster.cpp`, `NaniteResources.cpp`, `RenderUtils.cpp` on each branch |

## Pipeline at a glance

| Stage | Where it runs | Output |
|---|---|---|
| Cluster, group, simplify, repeat | Offline, CPU, parallel | A DAG of clusters with error and bounds |
| Constrain, quantize, page, build hierarchy | Offline, CPU | Root and streaming pages, a 4-ary hierarchy per mesh |
| Page install and transcode | Runtime, GPU compute | Bit-packed clusters in one GPU page buffer |
| Instance culling (chunk, then instance) | GPU compute | Candidate hierarchy roots per view |
| Hierarchy traversal with LOD and visibility tests | GPU compute, one dispatch per level | Candidate clusters |
| Cluster culling and rasterizer choice | GPU compute | Visible clusters, split into software and hardware lists |
| Raster binning | GPU compute | Per-material lists of cluster triangle ranges |
| Software and hardware rasterization | Compute and graphics | 64-bit visibility buffer |
| HZB build, then the post pass repeats culling and raster for items occluded in the main pass | GPU | Completed visibility buffer |
| Depth export | Compute or full-screen draw | Scene depth, stencil, shading mask, velocity |
| Shade binning and compute shading | GPU compute, one dispatch per material | G-buffer |

## Offline build

### Stage order

The builder turns each mesh into clusters, reduces the cluster set level by level until one root
cluster remains, optionally trims the finest levels, derives the fallback mesh from the same DAG,
then encodes pages. Evidence: `NaniteBuilder/Private/NaniteBuilder.cpp` (the intermediate-resource
function, the fallback function and the call into `Encode`), and `Encode/NaniteEncode.cpp`.

### Triangles to leaf clusters

A leaf cluster holds 124 to 128 triangles. The leaf partitioner is constructed with a minimum of
`ClusterSize - 4` and a maximum of `ClusterSize`, and `ClusterSize` is 128
(`ClusterDAG.cpp`, `Cluster.h`). The 256-vertex limit is enforced later, during encoding: a
cluster that still exceeds 256 vertices after index constraining is cut in two by triangle range
(`Encode/NaniteEncodeConstrain.cpp`).

Steps, from `ClusterDAG.cpp` (`AddMesh`):

1. Hash every triangle edge by the positions of its two end points and match opposite edges.
   Matching is by position, not by index, so UV and normal seams do not break adjacency. Edges
   shared by more than two triangles are linked in a sorted order to keep the build deterministic.
2. Union-find over the adjacency gives connected islands.
3. Build a graph with one node per triangle. Adjacent triangles get a heavy edge (weight 260).
   "Locality links" add light edges (weight 1) between triangles that are close in space but not
   connected: triangles are sorted by a 30-bit Morton code of their centers, and each triangle in
   a short island looks up to 16 steps each way along that order for the five nearest triangles in
   other islands that have the same material (`GraphPartitioner.h`).
4. Partition the graph, then build each cluster in parallel, recording for every edge how many
   neighbors lie outside the cluster ("external edges").

### The graph partitioner

The partitioner is still METIS. `NaniteBuilder.Build.cs` links the `metis` third-party module, the
derived-data version string includes the METIS version, and the strict partition entry that the
DAG builder uses calls `METIS_PartGraphRecursive` (`GraphPartitioner.cpp` also keeps a k-way entry
that the DAG does not use). Epic wraps it in its own recursive bisection: each call splits a graph
in two with target weights chosen so both halves can later be divided into parts inside the
allowed size range, the element array is partitioned in place, child graphs are extracted, and
recursion stops when a part fits the maximum. Balance tolerance is loose near the top and strict
near the leaves. Bisection of large graphs runs on a task queue; the resulting ranges are sorted so
that the threaded result is deterministic.

Two other partitioners exist for content outside this notebook's scope: a top-down
surface-area-style splitter (`BVHCluster.h`) used for voxel clusters, and k-means for curves.

### Cluster groups

Clusters of one level are grouped 8 to 32 at a time (`MinGroupSize`, `MaxGroupSize` in
`ClusterDAG.cpp`), and a level with 32 or fewer clusters becomes one group. The grouping graph has
one node per cluster. Two clusters are connected when they share boundary edges, found by hashing
each cluster's external edges by position and matching reversed edges. The edge weight grows with
the number of shared edges, so the partitioner prefers to keep clusters with long shared boundaries
together, which leaves the fewest locked edges. Clusters that came out of the same group one level
down ("siblings") get a lower weight per shared edge (12 instead of 16, plus a small constant), which nudges the next
level's group boundaries away from the previous level's. The same Morton locality links connect
spatially close but disconnected clusters.

The 2021 talk gives the reason: an edge that stays on a group boundary across many levels can never
be simplified, so boundaries must move from level to level.

### Simplification and boundary locking

Each group is reduced independently and in parallel (`ReduceGroup`):

1. Merge the triangles of all clusters in the group into one mesh, welding identical vertices
   through a hash table. Edges shared with clusters outside the group keep their external mark.
2. Compute the number of parent clusters as the group's triangle count divided by 256, rounded up,
   and simplify to that many clusters of 126 triangles. This roughly halves the triangle count.
3. Split the simplified mesh back into clusters of 124 to 128 triangles with the same graph
   partitioner. If the split needs more clusters than planned, the merge and simplification are
   redone from scratch with a target two triangles smaller per cluster, down to a floor of 64.

The simplifier is Epic's own quadric edge-collapse simplifier
(`MeshSimplifier/Private/MeshSimplify.cpp`, module `QuadricMeshReduction`), not a third-party
library. Boundary locking works on positions:
both end points of every external edge are marked locked. When a candidate collapse has one locked
end point, the surviving vertex is placed exactly on the locked one; when both are locked the
collapse receives a penalty of 1e8, which keeps it at the bottom of the queue. After simplification
the surviving locked edges are looked up again by position and re-marked as external, with their
neighbor counts, so the next level can match them.

Because locked vertices never move and both sides of a group boundary contain the same locked
vertices, the simplified group is watertight against every neighbor at this level and the level
below.

### How attributes enter the error metric

The metric is a quadric with attributes, evaluated per "wedge" (a set of triangles around the
collapsing edge that share attribute values), plus a separate edge quadric that resists moving open
or discontinuous edges (edge weight 2.0 in the Nanite call). Evidence: `Cluster.cpp`
(`SimplifyTriangles`) and `MeshSimplify.cpp` (`EvaluateMerge`, `Simplify`).

- Scale normalization: positions are multiplied by a power of two so that the average triangle
  size (square root of area per triangle) is about 0.25 before simplification, and the error is
  scaled back afterwards. This makes the attribute weights independent of mesh units.
- Attribute weights: normal 1 per component, tangent 1/16 per component, tangent sign 0.5, vertex
  color 1/16 per channel. Each UV channel is weighted by the reciprocal of 128 times that channel's
  average triangle size in UV space, so UV error is measured relative to texel density.
- Triangles on either side of a UV mirror are given different material keys during simplification,
  which forces a discontinuity there.
- The new vertex position is found by minimizing the quadric with a volume-preservation
  constraint first, then without it, then restricted to the edge, then the midpoint. Positions that
  flip a triangle or leave the neighborhood bounds are rejected or penalized.
- Each wedge's error is clamped to its surface area, and removing an isolated sliver counts as
  losing its whole area.
- The value returned is the square root of the largest error of any collapse performed, in object
  units. If edge collapses cannot reach the target, triangles are removed outright.

The 2021 talk calls the attribute mixing a heuristic and lists a perceptual metric as open work.

### DAG assembly and the data that makes cuts crack-free

A cluster stores two things that drive selection: a bounding sphere called the LOD bounds and a
scalar LOD error. A group stores a LOD bounds sphere and a "parent LOD error". The assignments in
`ReduceGroup` are:

- Group LOD bounds = a sphere enclosing the LOD bounds of all child clusters.
- Group parent error = the maximum of every child cluster's error and the error returned by
  simplifying the group.
- Every parent cluster produced from the group gets the group's LOD bounds and the group's parent
  error as its own. Leaf clusters have error zero and their own bounding sphere.
- The root cluster is wrapped in a root group with a parent error of 1e10.

Three properties follow, and together they are the whole crack-free argument:

1. **Shared decision inside a group.** All parents made from one group carry identical error and
   sphere, so the test "is this parent good enough" gives one answer for all of them. All children
   in a group are judged by the group's parent error and sphere, so "is our parent not good enough"
   also gives one answer.
2. **Exact complement.** A parent's own test and its children's parent test use the same two
   numbers. For any view, exactly one side passes. The union of clusters where "own error is
   acceptable and the parent's error is not" is therefore a cut of the DAG with no overlap and no
   hole.
3. **Monotonicity.** Error never decreases and spheres nest going up, so the projected error is
   monotonic along every path and the cut is unique. Each cluster can evaluate its two comparisons
   alone, in parallel, without knowing the DAG.

Boundaries match across the cut because the only edges shared between a drawn cluster and a
neighbor from a different group are locked edges, identical on both sides.

What is serialized: each packed cluster carries the LOD sphere, the box bounds as center and
extent, and the LOD error and maximum edge length as two 16-bit floats (`NaniteEncode.cpp`,
`PackCluster`). Each hierarchy slot carries a LOD sphere, box bounds, a minimum child error rounded
down and a maximum parent error, both 16-bit floats (`NaniteEncodeHierarchy.cpp`,
`PackHierarchyNode`).

### The hierarchy over clusters

The runtime does not walk the DAG. It walks a separate bounding hierarchy whose leaves are "group
parts": the clusters of one group that landed in one page (`NaniteEncodeHierarchy.cpp`).

- Fan-out is 4. A packed node is four child slots stored as structure-of-arrays, 60 dwords.
- A separate tree is built for each DAG level. Leaves of a level are split top-down into a
  complete 4-ary tree with at most one partially filled node; at each split the children are
  ordered by two rounds of binary splitting, each choosing the axis that gives the smallest summed
  surface area. The source states the aim is to bound tree depth, which bounds traversal latency.
- The per-level roots are joined under a small top tree, with the coarsest levels closest to the
  root. Mixing levels in one tree was tried and rejected because coarse clusters inflate both the
  bounds and the error ranges of their neighbors.
- An inner slot's LOD sphere encloses its children's spheres, its box encloses their boxes, its
  maximum parent error is the maximum below it and its minimum error is the minimum below it. The
  traversal test is therefore conservative.
- Maximum depth is 32 (`NANITE_MAX_CLUSTER_HIERARCHY_DEPTH`); it was 14 in `5.4` and `5.6`. A mesh
  may have at most 2^16 nodes.

### The fallback mesh

The fallback mesh is cut from the same DAG, so it costs no second simplification of the source
(`NaniteBuilder.cpp`, `ClusterDAG.cpp` `FindCut`). A priority queue starts at the root and
repeatedly replaces the cluster with the largest error by the children of its generating group,
until the triangle count passes the target or the error falls below it, then continues for about
4096 more triangles. The selected clusters are merged and run through the same simplifier down to
the exact target, with a floor of 64 triangles. The targets are a triangle percentage and a
"relative error" expressed as a percentage of the square root of the mesh's surface area (capped by
the bounding box area). With the default "Auto" target the relative error is 1.0
(`r.Nanite.Builder.FallbackTargetAutoRelativeError`); a separate, coarser default of 2.0 exists
for a ray-tracing proxy. If no reduction is requested the source mesh is used unchanged.

### Build parallelism and time

The source gives parallelism facts but no build-time figures. Edge hashing, leaf cluster creation,
group reduction (one task per group), external-edge matching, quantization and page writing all use
parallel loops; METIS bisection runs on a work queue above 5000 triangles; each stage logs its wall
time. Determinism is enforced by sorting parent clusters by a hash-derived identifier after each
parallel level, and the cache key separates ARM64 from x64 builds because the two do not yet
produce identical data (`NaniteBuilder.cpp`, version string). The 2021 talk says only that build
time matters and the builder is heavily optimized. No seconds-per-million-triangles number was
found in any source read. **[UNVERIFIED]** any specific build-time figure.

## Encoding

### Vertex and attribute quantization

| Data | Encoding | Evidence |
|---|---|---|
| Position | One global grid per mesh with step 2^-precision centimeters. Automatic precision is derived from the geometric mean of leaf cluster extents and clamped to at least 4 (1/16 cm). Each cluster stores an integer origin and a bit count per axis sized to its own extent, at most 21 bits per axis. Because the grid is global, shared boundary vertices quantize identically in every cluster. | `NaniteEncode.cpp` |
| Normal | Octahedral, default 8 bits per axis, at most 15 | `NaniteEncode.cpp`, `NaniteEncodeGeometryData.cpp` |
| Tangent | Optional. An angle around the normal, default 7 bits, plus a sign bit. Without explicit tangents the tangent frame is derived at shading time and costs no storage. | Same; 2021 talk |
| UV | A custom float with 5 exponent bits and 14 mantissa bits, then per-cluster minimum and bit count per component; up to 4 channels | `NaniteDefinitions.h`, `NaniteEncodeGeometryData.cpp` |
| Color | Per-cluster minimum and bit count per channel; a constant-color mode stores nothing per vertex | Same |

Attributes of one vertex are concatenated into a bit string with no byte alignment; shaders read
them with a bit-stream reader.

### Indices

The in-memory format stores each triangle as a base vertex index plus two 5-bit offsets, about 17
bits per triangle. This is possible because the builder reorders triangles and duplicates vertices
where needed so that every triangle's vertices fall within a trailing window of 32 vertices
(`NANITE_CONSTRAINED_CLUSTER_CACHE_SIZE`, `NaniteEncodeConstrain.cpp`). Triangles are also rotated
so the lowest index comes first, which the hardware path later uses to recover vertex order.

The disk format is a generalized triangle strip (`NaniteEncodeTriStrip.cpp`): three 128-bit masks
per cluster say whether each triangle starts a strip, turns left or right, and reuses an old
vertex; reused vertices are 5-bit back-references and new vertices are implicit. The 2021 talk
quotes about 5 bits per triangle on disk against 17 in memory (slide 142). Any triangle can be decoded
independently with bit counts over the masks, which is what makes GPU decoding parallel.

Each cluster also stores material ranges (a fast form for up to three materials packed in 32 bits,
otherwise a table, at most 64 materials) and "vertex reuse batches": runs of at most 32 triangles
touching at most 32 distinct vertices, precomputed for the mesh-shader path
(`NaniteEncodeVertReuseBatch.cpp`).

### Pages

- Sizes are measured in decoded GPU bytes: 32 KB for a root page, 128 KB for a streaming page. A
  root page holds at most 64 clusters and a streaming page at most 256.
- Groups are ordered coarsest level first, then by Morton code of the group center with the
  direction alternating per level, and clusters are appended to the current page until it is full
  (`NaniteEncodePageAssignment.cpp`). A group that straddles pages is split into parts (at most 31).
- The first pages are root pages and are always resident. Their count comes from a per-mesh
  minimum-residency setting that defaults to zero, which yields one root page.
- A group becomes drawable only when all its parts are resident. Page "fixups" patch the hierarchy
  child references and the leaf flags of the parent clusters when pages are installed or removed.
  Clusters carry a flag meaning "my children are not loaded, draw me even if my error is too high",
  which is how a partially streamed mesh stays hole-free.
- On disk, vertices that already exist in the same page or in a parent page are stored as
  references instead of data (the 2021 talk reports about 30% of vertices, slide 140). The remaining values
  are stored as zigzag deltas from the previous vertex, split into low, middle and high byte
  streams. Reference chains across pages are limited in depth.

Streaming policy, the request path and residency belong to `geometry-compression-and-streaming.md`.

### GPU transcode

Pages are uploaded in disk form and converted to the memory form by a compute shader, one thread
group per cluster (`Engine/Shaders/Private/Nanite/NaniteTranscode.usf`). The first pass copies the
fixed-size headers, decodes strip indices into the base-plus-offsets form, and turns byte-stream
deltas into bit-packed vertices using wave prefix sums. A second pass fills referenced vertices by
reading already-installed pages, rebasing positions when the two clusters have different origins.
The split exists because the second pass depends on other pages being present.

## Runtime culling

### Frame flow

One function drives everything for a set of views (`NaniteCullRaster.cpp`, `DrawGeometry`):
initialize queue counters; filter primitives per view; main pass (instance culling, hierarchy
traversal, cluster culling, raster binning, rasterization); build a furthest-depth HZB from the
scene depth and the just-written Nanite depth; post pass (the same steps over the items that the
main pass found occluded); read back peak counts.

### Instance culling and the instance hierarchy

Instances are culled in two stages.

- A scene-wide spatial structure groups instances into cells and each cell into chunks of at most
  64 instances, with a compact box per chunk. A compute pass tests every chunk against every view
  in a view group and emits one work item per surviving chunk with a mask of the views that can see
  it; chunks occluded in the main pass are appended to an occluded list for the post pass
  (`NaniteInstanceHierarchyCulling.usf`, `Engine/Shaders/Shared/SceneCullingDefinitions.h`). Static
  chunks are kept separate from dynamic ones so a cheaper permutation can skip previous-frame
  transforms.
- The instance pass runs 64 threads per chunk, one per instance. Each instance is tested per view
  for visibility flags, draw distance, the global clip plane, frustum and HZB. A surviving
  (instance, view) pair appends one candidate node that points at hierarchy node 0 of that mesh
  (`NaniteInstanceCulling.usf`).

How cells are selected on the CPU side (`Renderer/Private/SceneCulling/SceneCulling.cpp`) was not
read. **[UNVERIFIED]** cell-level details. The 2021 talk predates this hierarchy and culled the
flat instance list.

### How traversal is scheduled

The default is one indirect compute dispatch per hierarchy level followed by one cluster-culling
dispatch. `r.Nanite.PersistentThreadsCulling` is 0 by default on `ue5-main`, `5.8` and every
release back to `5.4`; it was 1 in `5.1` to `5.3`, and `5.0` had only the persistent-thread shader.
The variable's own description discourages the persistent mode on anything but fixed hardware,
because its thread count does not follow GPU size and the kernel depends on how the scheduler
behaves.

Level-by-level mode (`NaniteHierarchyTraversal.ush`, `NaniteCullRaster.cpp`):

- A thread group has 64 threads and processes 16 nodes, one thread per child slot.
- Each thread tests its slot. Surviving inner slots are counted with a wave-level sum, the group
  reserves a range in the shared candidate-node buffer with one atomic add, and each survivor
  writes its child node there.
- The same atomics update the indirect arguments of the next level (group count, node count and
  start offset). Two argument buffers are used alternately because a buffer cannot be read as
  indirect arguments and written in the same pass. The number of level passes comes from the
  deepest hierarchy currently registered with the streaming manager.
- Surviving leaf slots append their clusters (page index and cluster index, one entry per cluster
  in the part) to the candidate-cluster buffer.
- After the last level, cluster culling runs with 64 clusters per thread group.

Persistent mode keeps a fixed number of thread groups alive in one dispatch. Each group repeatedly
claims a batch of 16 nodes from an atomic read cursor, waits until the entries have been written
(unwritten entries hold a sentinel), processes them and appends children to the same queue; when no
nodes are ready it claims a batch of 64 clusters instead, so cluster culling fills otherwise idle
time. A group exits when the outstanding-node counter reaches zero and no cluster batches remain.
The 2021 talk measured this as 25% faster than the level-by-level approach on its content.

### Buffers, capacities and overflow

| Buffer | Default capacity | Overflow behavior | Evidence |
|---|---|---|---|
| Candidate nodes | 2,097,152 (`r.Nanite.MaxNodes`), with separate regions for the main and post passes | Writes past the end are skipped | `NaniteShared.cpp`, `NaniteHierarchyTraversal.ush` |
| Candidate clusters | 16,777,216 (`r.Nanite.MaxCandidateClusters`), 8 or 12 bytes each | Main-pass entries grow from the front and post-pass entries from the back of one buffer; a running total trims additions so the two cannot collide | `NaniteClusterCulling.usf` |
| Visible clusters | 4,194,304 (`r.Nanite.MaxVisibleClusters`); the 24-bit payload field implies a ceiling just under 16,777,216, but no explicit clamp to it was found | Software clusters fill from the bottom and hardware clusters from the top; a one-thread pass clamps both counts so the ranges cannot overlap | Same |
| Streaming requests | Sized by the streaming manager | Requests past the end are dropped | `NaniteStreaming.ush` |

Dropped work means missing geometry for that frame; there is no fallback draw. Peak counts are sent
back to the CPU through a GPU message so the engine can report that a limit should be raised. These
three defaults are unchanged since `5.0`.

### The LOD criterion

The test compares a world-space error against a distance-like quantity, with no screen-space
projection per cluster (`NaniteCullingCommon.ush`, `NaniteClusterCulling.usf`).

- For a LOD sphere, compute the smallest and largest value over the sphere of "view depth times the
  cosine of the angle off the view axis", clamped at the near plane. Call the smallest one the
  projected scale. For an orthographic view it is 1.
- A per-view constant, the LOD scale, equals half the projection's vertical scale times the
  viewport height in pixels, divided by the target error in pixels
  (`r.Nanite.MaxPixelsPerEdge`, default 1).
- An error E is acceptable when projected scale is greater than LOD scale times E times the
  instance's uniform scale. In words: E projects to less than one pixel at the nearest point of the
  sphere.
- Hierarchy slot: descend when the slot's maximum parent error is not acceptable. For a leaf slot
  there is a second test, added in 2025: skip the part when even its minimum child error is not
  acceptable at the farthest point of the sphere, since nothing in it can be drawn.
- Cluster: draw when its own error is acceptable, or when it is flagged as a leaf in the current
  streaming state.

A time budget can lower quality automatically: `r.Nanite.PrimaryRaster.TimeBudgetMs` with a floor
of 30% scales the target edge length up when the pass is over budget (disabled by default).

### Frustum and HZB tests

Both nodes and clusters use the same box test (`NaniteCullingCommon.ush`, `NaniteHZBCull.ush`). The
box is transformed to clip space and reduced to a screen rectangle and a depth range, with flags
for crossing the near or far plane. The rectangle is converted to pixels using pixel centers, so a
box that covers no pixel center is culled outright. For occlusion, the HZB mip is chosen so the
rectangle spans at most 4 by 4 texels, the 16 texels are gathered, and the box is visible when its
nearest depth is at least as near as the farthest occluder depth (reversed Z). Boxes that cross the
near plane skip the occlusion test and force the hardware rasterizer, which can clip.

### Two-pass occlusion

Occlusion is two-pass by default (`r.Nanite.Culling.TwoPass`).

- **Main pass.** Every chunk, instance, node and cluster is tested against the previous frame's HZB
  using its previous-frame transform and the previous view. Items that pass are processed and
  drawn. Items that fail are not discarded; they are recorded: chunks and instances in occluded
  lists, nodes as an entry holding a bit mask of the occluded children, and clusters at the far end
  of the candidate buffer with their rasterizer choice.
- **HZB.** A furthest-depth pyramid is built from the scene depth and the Nanite depth just
  rasterized (`BuildHZBFurthest` called from `DrawGeometry`).
- **Post pass.** Only the recorded items are tested again, with current transforms against the new
  HZB, and whatever is now visible is traversed and drawn. Recorded nodes do not repeat the LOD
  test.

The result is conservative: an object hidden last frame and visible now is drawn in the same frame,
at the cost of a second culling and raster pass over a short list. No list of previously visible
objects is kept; visibility in the main pass is decided by the old depth pyramid alone.

### Multiple views

All passes take an array of packed views and every candidate record carries a view index, up to
4096 views per call (`NANITE_MAX_VIEWS_PER_CULL_RASTERIZE_PASS_BITS`). Virtual shadow map mip
levels, cube faces and stereo eyes go through the same culling as extra views. Detail is in
`nanite-system-integration.md`.

## Rasterization

### Software or hardware

The choice is made per cluster at the end of cluster culling. A cluster goes to the hardware
rasterizer when its longest triangle edge, projected with the same distance measure as the LOD
test, exceeds `r.Nanite.MinPixelsPerEdgeHW` (32 pixels; 18 in `5.0`), or when its box crosses the
near plane or the global clip plane. Everything else goes to the software rasterizer. Since the LOD
target is one-pixel error, most clusters of a dense mesh have edges of a few pixels and take the
software path; large triangles on simple meshes take the hardware path.

Raster binning then overrides the choice per material range (`NaniteRasterBinning.usf`): a material
that needs finite-difference derivatives, a pixel-programmable material on a GPU whose wave size
may be below 32, and translucent materials are forced to hardware; displacement, voxel and curve
clusters are forced to software. `r.Nanite.ComputeRasterization=0` sends everything to hardware.

### Raster bins

A raster bin is one unique combination of raster shader and state. Fixed-function content uses a
handful of bins selected by a bit mask (two-sided, spline, skinned, shadow-casting, voxel, curve);
each material with programmable raster gets its own. Binning is a count, reserve, scatter sequence
over the visible clusters: each cluster's material ranges are looked up, adjacent ranges with the
same bin are merged, and an entry (visible cluster index, bin, first triangle, triangle count) is
written into that bin's slice of one indirection buffer. Each active bin then gets one indirect
compute dispatch for its software clusters and one indirect draw for its hardware clusters. The
software dispatches can run on an async compute queue overlapping the hardware draws.

### Hardware path variants

`GetRasterHardwarePath` in `NaniteShared.cpp` picks, in order: a mesh-shader path when the
platform has tier-1 mesh shaders (three flavors that differ only in how indirect arguments are laid
out), a primitive-shader path when the RHI reports primitive shaders, and otherwise a plain
vertex-shader path.

- **Mesh shader.** One thread group of 32 per vertex-reuse batch. Each thread decodes one triangle;
  the batch's distinct vertices are deduplicated with wave operations so each is transformed once.
  The payload and view index travel as per-primitive attributes.
- **Vertex shader, used when mesh and primitive shaders are absent.** One instanced, non-indexed
  draw per bin: the instance count is the number of cluster ranges and the vertex count per
  instance is fixed at 384 (128 triangles times 3). Each vertex invocation derives its triangle and
  corner from the vertex ID, fetches the cluster from the instance ID, decodes the index and
  transforms the vertex. Triangles beyond the range collapse to a degenerate position. Every vertex
  is therefore transformed once per triangle that uses it.
- **All variants.** The pixel shader has no color output for the visibility buffer. It performs
  the same 64-bit atomic-max write to the same texture as the software path. That is what lets
  software and hardware output be merged in any order, including concurrently.

### The software rasterizer

It is a compute shader, one thread group of 64 threads per cluster range (`NaniteRasterizer.usf`,
`NaniteRasterizer.ush`). There is no tile binning and no coverage mask: the design assumes a
triangle covers a few pixels.

1. **Vertices.** Threads transform the cluster's vertices, 64 at a time, to screen positions
   snapped to a grid with 8 sub-pixel bits (256 positions per pixel), and store them in
   group-shared memory.
2. **Triangle setup.** One thread per triangle (two rounds for 128). It reads three cached
   vertices, computes the three edge vectors, rejects back faces by the sign of the 2D determinant
   (or flips winding for two-sided), computes the bounding rectangle rounded to pixel centers,
   clips it to the scissor, and limits it to 64 pixels per side. Edge-function constants are
   evaluated at the first pixel center and adjusted for a top-left fill rule. Depth is a plane
   equation in the edge-function values.
3. **Traversal.** If no triangle in the wave is wider than 4 pixels, every pixel of the rectangle
   is tested against the three edge functions. Otherwise each row solves the edge equations for the
   first and last covered pixel and iterates only that span.
4. **Write.** For each covered pixel the thread packs depth and payload into 64 bits and performs
   an atomic maximum on the visibility texture. Depth is the floating-point bit pattern in the high
   word, so with reversed Z the nearest fragment wins and the payload travels with it. There is no
   separate depth read, test and write, and no ordering requirement between triangles, clusters,
   or the two rasterizers.

Depth-only rendering (shadows) uses a 32-bit atomic maximum on a 32-bit texture instead.

The 2021 talk reported the software path as about 3 times faster than its fastest hardware
(primitive shader) path on small triangles, and used a 128-thread group at that time.

### Platforms without 64-bit atomics

On `ue5-main`, `5.8` and `ue6-main` there is no alternative: the write function has only the
depth-only branch and the 64-bit atomic branch, and compilation fails otherwise
(`NaniteWritePixel.ush`). `5.0` had vendor-extension branches and a last-resort "lock buffer"
variant, which did a 32-bit atomic maximum on a separate depth texture and then wrote the 64-bit
value non-atomically if it had won. Two threads can both win and write in the wrong order, and the
`5.0` source describes the scheme only as appearing to work in practice for the compute rasterizer,
not the pixel shader. A commit dated 2022-04-20 removed the vendor extensions, the lock buffer and
DX11 support together. Unreal's Metal platform header, for its Shader Model 6 profile, declares
64-bit image atomics supported and maps the write to a native 64-bit atomic maximum on a texture
(`Engine/Shaders/Public/Platform/Metal/MetalCommon.ush`). The same profile sets the SM6
wave-operations flag, which the shared platform header (`Engine/Shaders/Public/Platform.ush`) turns
into the wave once/vote/min-max/bit-op capabilities; an older block that disabled those
capabilities under a note about outstanding bugs is commented out and no longer applies.

### Programmable raster

Masked materials, pixel depth offset and world position offset (WPO) run material code inside the
rasterizers, in bins of their own. The 2024 talk dates this to UE 5.1.

- **Vertex side.** The material's WPO is evaluated when vertices are transformed. Culling boxes are
  enlarged by the primitive's declared maximum WPO extent.
- **Pixel side, software.** Before any material code runs, the pixel's stored depth is read and the
  fragment is dropped if it is already behind. Then barycentrics and their screen derivatives are
  computed from the edge functions, the three vertices' attributes are interpolated, the material's
  mask or depth offset is evaluated, and the atomic write is skipped if the pixel is clipped.
  Vertices for this path are kept in a rolling 64-entry cache, which relies on the 32-vertex window
  guaranteed by the builder and needs a wave size of at least 32.
- **Pixel side, hardware.** Same early depth test (kept if any pixel of the 2 by 2 quad passes, so
  derivatives stay valid), then the material code, then the atomic write.

Cost containment, all visible in source:

- Per-primitive distances switch an instance or cluster to the fixed-function "fallback" bin: a
  pixel-programmable distance, a WPO disable distance, and a displacement fade size.
- Because the early test only helps when near geometry is drawn first, clusters in
  pixel-programmable bins are sorted into 256 logarithmic depth buckets between
  `r.Nanite.DepthBucketsMinZ` (1000) and `MaxZ` (100000), and software clusters are otherwise
  written in reverse order for the same reason.
- Only clusters whose materials need it enter programmable bins; fixed-function content keeps the
  cheapest shader.
- Materials whose code needs finite differences cannot run in the software rasterizer, where
  neighboring lanes are unrelated triangles, so they are forced to hardware.

### The visibility buffer

One 64-bit unsigned texel per pixel (`PF_R64_UINT`, or a two-channel 32-bit format where needed).
The high 32 bits are depth. The low 32 bits are the visible-cluster index plus one in the top 24
bits and the triangle index in the low 8 bits; zero means empty (`NaniteDataDecode.ush`, same on
`5.8` and `ue6-main`). The instance is not stored: the visible-cluster record holds the instance,
view, page and cluster index. The 2021 talk described a 30/27/7-bit split (slide 84), so the layout
has changed since. The 24-bit cluster field bounds how many visible clusters a frame can address to
just under 16,777,216; no separate clamp of `r.Nanite.MaxVisibleClusters` to that value was found
**[UNVERIFIED]**.

## Shading

### Today: compute shading with shade binning

On `ue5-main` and `5.8` all Nanite G-buffer shading is compute. The base pass adds a "shade
binning" stage and then one indirect compute dispatch per material (`NaniteShading.cpp`,
`NaniteShadeBinning.usf`). No pixel-shader material path remains.

1. **Shading mask.** Depth export writes a 32-bit mask per pixel: a Nanite bit, a 14-bit shading
   bin (the material), lighting channels, flags, and a 4-bit shading rate.
2. **Count.** A pass over 8 by 8 pixel blocks counts pixels (or 2 by 2 quads) per shading bin. It
   also performs a clear optimization: G-buffer sub-tiles that will be fully overwritten are marked
   so they need no clear.
3. **Reserve.** One thread per bin allocates a contiguous range in a single buffer and writes the
   bin's indirect dispatch arguments.
4. **Scatter.** Pixel or quad coordinates are written into each bin's range. Fully covered blocks
   are placed at the front of the range and loose elements at the back, so dispatch threads read
   coherent blocks first.
5. **Shade.** Each bin's dispatch reads its coordinates, loads the visibility texel, reconstructs
   attributes, evaluates the material and writes the G-buffer targets through unordered-access
   views.

Bins come in two modes. If the compiled material shader contains any derivative operation
(detected from the shader bytecode), the bin is shaded in quads of four threads so finite
differences work, with helper lanes for pixels of other materials. Otherwise it is shaded per
pixel. The 2024 talk reports 4% to 24% helper lanes depending on content and mode, and a software
variable-rate-shading mode built on the same lists.

### How it changed across releases

| Release | Material path | Evidence |
|---|---|---|
| 5.0, 5.1 | One full-screen draw per material. A compute pass writes a "material depth" value per pixel; each material draws a grid of screen tiles at its depth with a depth-equal test; a classify pass builds a per-material tile mask so empty tiles are discarded in the vertex shader. | `5.0` and `5.1` `NaniteMaterials.cpp` (passes named Classify Materials, Emit Material Depth, Emit GBuffer); 2021 and 2024 talks |
| 5.2, 5.3 | Compute materials present but off: `r.Nanite.ComputeMaterials` and `r.Nanite.AllowComputeMaterials` default 0 and are marked experimental. | `5.2` and `5.3` `NaniteMaterials.cpp`, `NaniteResources.cpp` |
| 5.4 | Compute shading on by default (both variables default 1), with the old path still compiled. Vulkan and Metal are excluded in code and stay on the old path, with a note that compute derivatives are missing there. | `5.4` `NaniteResources.cpp`, `RenderUtils.cpp` |
| 5.5 onward | The pixel-shader path is deleted: `NaniteMaterials.cpp` shrinks from 1163 lines in `5.4` to 90 in `5.5`. | `5.5` source; commits of 2024-04-20 and 2024-04-24 on `ue5-main` titled as removing the legacy path |

First compute-shading commit on `ue5-main`: 2023-01-19; first shade-binning commit: 2023-02-01
(commit history of `NaniteMaterials.cpp` and `NaniteShadeBinning.usf`). The 2024 talk gives the
reasons for the change: cheaper empty dispatches, no pipeline state changes, freedom from mandatory
2 by 2 quads, and removal of the material-depth machinery.

### Attribute and derivative reconstruction

For each shaded pixel (`NaniteVertexFactory.ush`, `NaniteAttributeDecode.ush`):

1. Unpack the visible-cluster index and triangle index, then load the visible cluster, instance and
   cluster header.
2. Decode the triangle's three vertex indices and fetch and decode the three vertices from the page
   buffer. Transform them to clip space, re-running WPO if the cluster has it enabled.
3. Compute perspective-correct barycentric coordinates of the pixel center from the three clip
   positions, together with their derivatives with respect to screen x and y.
4. Interpolate every attribute as a value plus two screen derivatives (dual numbers). UV
   derivatives come out analytically, and texture sampling uses explicit gradients.

Analytic derivatives avoid the artifacts of finite differences across triangle, depth and object
boundaries that a full-screen quad would produce. The 2024 talk puts their cost at about 2% of
shading time and notes that derivatives of arbitrary expressions fall back to finite differences,
which is why quad mode still exists.

### Depth, mask and velocity export

A compute pass (on platforms that expose the depth buffer's tile metadata) or a full-screen pixel
shader (elsewhere) converts the visibility buffer into engine targets (`NaniteDepthExport.usf`,
`NaniteExportGBuffer.usf`). For each Nanite pixel that is nearer than existing scene depth it writes
scene depth, stencil (decal-receiver or custom stencil), the shading mask and velocity. Velocity is
recomputed from the visibility data: the triangle's vertices are fetched and transformed with the
previous instance transform and previous view. Pixels whose material uses WPO are excluded here and
get their velocity from the shading pass instead.

## Budgets and limits

| Limit | Value | Source |
|---|---|---|
| Triangles per cluster | 128 (7 bits) | `NaniteDefinitions.h` |
| Vertices per cluster | 256 (8 bits) | Same |
| Materials per cluster | 64 | Same |
| UV channels | 4 | Same |
| Clusters per group | 8 to 32 built; format allows 511 | `ClusterDAG.cpp`, `NaniteDefinitions.h` |
| Hierarchy fan-out, depth, nodes per mesh | 4, 32, 65,536 | `NaniteDefinitions.h` |
| Root page, streaming page | 32 KB, 128 KB | Same |
| Pages per mesh, GPU pages total | 65,536, 131,072 | Same |
| Instances | 16,777,216 (24 bits); Epic's documentation states the same hard limit | Same; Epic documentation |
| Views per culling call | 4096 | Same |
| Candidate nodes, candidate clusters, visible clusters | 2 M, 16 M, 4 M (payload-implied ceiling just under 16 M) | `NaniteShared.cpp`, `NaniteDataDecode.ush` |
| Streaming pool | 512 MB, excluding root pages (`r.Nanite.Streaming.StreamingPoolSize`) | `NaniteStreamingManager.cpp` |
| Initial root pages | 2048, growable | Same |
| Page installs per frame, pending pages | 128, 128 | Same |
| Target error, hardware threshold | 1 pixel, 32 pixels | `NaniteCullRaster.cpp` |
| Sub-pixel bits | 8 | `NaniteDefinitions.h` |
| Shading bins | 14 bits in the mask | `NaniteDataDecode.ush` |

Design decisions these reveal: capacities are fixed and preallocated rather than grown, overflow
drops work rather than stalling, cluster and triangle indices are sized to fit a 32-bit payload,
and the whole scene's geometry lives in one GPU buffer addressed by page index.

## What changed since the 2021 talk

| Topic | 2021 talk | Current source |
|---|---|---|
| Traversal | Persistent threads | Per-level dispatches by default; persistent mode optional |
| Instance culling | Flat list | Chunked spatial hierarchy first |
| Material shading | Full-screen tile draws per material with material depth | Compute dispatch per material with shade binning |
| Visibility payload | 30 depth, 27 cluster, 7 triangle bits | 32 depth, 24 cluster, 8 triangle bits |
| Non-64-bit-atomic fallback | Present in 5.0 | Removed |
| Programmable raster | Absent | Masked, depth offset and WPO in both rasterizers |
| Hardware threshold | 18 pixels in 5.0 | 32 pixels |
| Software raster group size | 128 threads | 64 threads |
| Small-instance impostors | Present | Removed in 5.8 |
| Core DAG, cluster and page constants | 128, 8 to 32, 128 KB | Unchanged |

## Essential versus incidental

The smallest coherent subset that still delivers crack-free continuous cluster LOD with GPU culling
is short. Everything in it is a consequence of the three DAG properties above.

| Part | Why it is essential |
|---|---|
| Clusters grouped by shared boundary, simplified per group with the group boundary locked, split again, repeated to a root | This is the only place cracks are prevented |
| Per cluster: own error and LOD sphere. Per group: parent error and LOD sphere, with monotonic error and nested spheres | Makes the cut a local, order-independent test |
| A GPU pass that evaluates the two comparisons per cluster per view and emits visible clusters | The runtime half of the same idea |
| Per-cluster frustum and occlusion culling against a depth pyramid | Without it the cost follows scene depth complexity |
| A way to draw an arbitrary list of clusters from one geometry buffer with no per-cluster CPU work | Otherwise selection results cannot be consumed |
| A fallback for when the fine data is missing or the path is disabled | Keeps a reference image and a non-cluster path |

Parts that are essential only beyond some scale:

| Part | The threshold it answers |
|---|---|
| Hierarchy over groups and level-by-level traversal | When testing every cluster of every instance is too slow. A 10-million-triangle mesh has roughly 80 thousand leaf clusters and about twice that in total, which a flat compute pass handles; millions of instances of such meshes do not. |
| Two-pass occlusion | When single-pass, previous-frame occlusion produces visible holes or forces conservative slack |
| Software rasterizer and 64-bit atomic visibility writes | When triangles approach pixel size, where hardware raster and 2 by 2 quads waste most of their work |
| Visibility buffer with deferred material evaluation | Same threshold: pixel-sized triangles make forward or G-buffer raster shade several times per pixel |
| Bit-packed encoding, pages and streaming | When source geometry exceeds memory |
| Instance chunk hierarchy | Hundreds of thousands to millions of instances |

Parts that exist for content breadth or production robustness, not for the idea: raster bins for
programmable materials, WPO and masked support, compute shade binning and variable-rate shading,
shader bundles and work graphs, multi-view rendering for shadow maps, depth-bucket sorting, time
budgets, fixed-capacity buffers with feedback, derived-data cache keys and determinism work, the
three hardware path variants, editor selection and debug views, and every non-triangle primitive.

## Distance from Luminex M9

The M9 text is the accepted one in
[the roadmap part](../../roadmap/gpu-driven-hybrid-rendering.md). "Present" means the text names
it as a deliverable.

| Nanite component | M9 status | Note |
|---|---|---|
| Leaf clustering to fixed-size clusters with bounds | Present | "meshlets, bounds/cones" through a maintained library |
| Group, lock, simplify, split DAG | Present in concept | "hierarchical simplification"; the crack-free invariant is not stated as a requirement or an exit gate |
| Monotonic error and the two-comparison cut | Not mentioned | "runtime selection/transition" could also describe discrete LOD switching with blending, which Nanite does not need |
| Hierarchy over groups, GPU traversal | Not mentioned | "GPU cluster culling" reads as a flat pass over the M7 path |
| Per-cluster frustum and HZB culling | Present | Over the M7 visibility path |
| Two-pass occlusion with same-frame recovery | Not mentioned | M7.4 is single-phase with previous-frame depth |
| Instance hierarchy | Not mentioned | M7 classifies a flat instance table |
| Hardware raster by vertex pulling from a cluster buffer | Present | "ordinary indirect cluster raster", "vertex/compute cluster path" |
| Mesh-shader raster | Present, optional | Matches Nanite's preferred hardware variant |
| Software rasterizer, 64-bit atomic visibility writes | Explicitly deferred | |
| Visibility buffer as the opaque path | Partially present | An experiment compared against compact deferred, not the default |
| Material classification and binned shading | Partially present | "material reconstruction/classification" is named; compute shading is not |
| Analytic attribute derivatives | Partially present | MipLab is extended "for derivatives" |
| Masked materials in the cluster path | Partially present | "alpha coverage" is a lab subject; no programmable-raster design |
| Cluster compression and bit-stream decode | Not mentioned | "general asset tooling" is deferred |
| Pages, streaming, GPU transcode | Explicitly deferred | To M11 |
| Fallback mesh and non-cluster reference | Present | "ordinary raster ... remain a reliable reference" |
| Multi-view culling for shadows | Not mentioned | M8 follows M9 |
| Fixed budgets with overflow reporting | Partially present | M7 already counts overflow |
| Published measurements | Present | Epic publishes no Nanite measurements for Apple hardware that this notebook found |

Verdict. M9 covers the offline half of Nanite's central idea and the simplest runtime that can
consume it: a cluster DAG from a library, a flat GPU cull with LOD selection, and hardware raster.
It names the visibility buffer only as an experiment. It leaves out the four things that make
Nanite "virtualized" and "micropolygon": hierarchy traversal, two-pass occlusion, the software
rasterizer, and streaming. Measured against the essential table above, M9 as written delivers the
first two rows if the crack-free invariant is made an explicit requirement, the third and fourth in
flat form, and the fifth through hardware raster. That is a real cluster-LOD renderer and a valid
subset of Nanite, comparable to running Unreal with compute rasterization disabled and a fully
resident mesh, but it is not close to Nanite's defining behavior, which is pixel-scale triangles at
a cost independent of scene size.

## Corrections to earlier research

- [`pipeline-state-of-the-art-m7-m11.md`](../2026-09-14-roadmap-review/pipeline-state-of-the-art-m7-m11.md)
  lists Nanite's hardware requirement as "SM5+". That is wrong for every release after 5.0: the
  commit of 2022-04-20 removed DX11 and the SM5 vendor-extension path, and Epic's 5.8 documentation
  states DirectX 12 with Shader Model 6.
- The same file marks the two-phase HZB and software rasterizer row "[UNVERIFIED live]" and says no
  official implementation is available. Both are now confirmed from source, and the source itself
  is readable under Epic's license.
- [`validated-techniques.md`](../validated-techniques.md) section 5 describes the two-pass scheme as
  first drawing "last-frame visible work". The source keeps no visible list. The main pass tests
  everything against last frame's depth pyramid with last frame's transforms and draws what passes;
  the post pass re-tests only what failed.
- [`validated-techniques.md`](../validated-techniques.md) section 10 and the 2021 talk describe the
  visibility payload as instance and triangle identity. The current payload is a visible-cluster
  index and a triangle index; the instance is reached through the visible-cluster record.
- [`production-engines.md`](../production-engines.md) and the notebooks above describe Nanite
  through the 2021 talk. Three parts of that talk no longer describe the shipped default:
  persistent-thread traversal, full-screen per-material draws, and the lock-buffer fallback.

## Implications for Luminex

Facts tied to the project's constraints, then options.

- **The invariant is small.** Crack-free selection needs one float and one sphere per cluster, one
  float and one sphere per group, and two comparisons at run time. It can be stated as a testable
  property and checked on the CPU: for random views, the selected set covers each source surface
  region exactly once, and no boundary between selected clusters has mismatched vertices. That
  suits the project's oracle-first habit and could be an exit gate for an offline slice before any
  GPU work.
- **Epic's own default traversal fits the current RHI.** Per-level indirect dispatches need only
  indirect dispatch, storage buffers and 32-bit atomics on buffers. The RHI has the first two;
  buffer atomics through the Slang Metal target are for the Apple notebook to confirm. Persistent
  threads are off by default in Unreal for a stated portability reason, so there is no case for
  building them.
- **Wave intrinsics are an optimization in Epic's shaders, not a requirement of the algorithm.**
  Traversal, binning and transcode use wave sums and prefix counts to replace one atomic per thread
  with one per wave. Unreal's Metal SM6 profile enables them, so Epic's own Metal builds rely on
  them, but the algorithm does not. A Luminex version can use per-thread atomics or a count,
  prefix-sum, scatter sequence like the existing M7 scan and emit passes; whether Metal's
  SIMD-group functions are usable from Slang is a question for the Apple notebook
  **[UNVERIFIED]**. The software pixel-programmable path is the one place where Epic requires a
  wave of at least 32.
- **A flat cull is defensible at Luminex content scale.** San Miguel at millions of triangles
  yields on the order of 10^5 clusters. The hierarchy is an optimization for instance counts the
  project does not have. It can be a later slice with a measured trigger.
- **Hardware-only Nanite is a real configuration.** Unreal has a switch for it and a vertex-shader
  path that needs neither mesh shaders nor an index buffer: an instanced draw of 384 vertices per
  cluster with vertex pulling. That maps directly to an indirect draw over a cluster buffer. If
  both rasterizers are not being merged, the visibility target can be an ordinary render target
  with a depth attachment; 64-bit atomics are needed only when a compute rasterizer shares the
  target.
- **The software rasterizer has one hard platform requirement and Unreal does not work around
  it.** If Metal's 64-bit texture atomics are unavailable or slow on the target, the only
  alternatives in Epic's history are a 32-bit depth-only target or an admittedly racy lock scheme.
  The Apple notebook should settle availability before any slice depends on it.
- **Two-pass occlusion is the missing half of M7.4.** The post pass is exactly the "same-frame
  recovery" that M7.4 lacks, and it reuses the main-pass shaders with a different input list.
- **Compute shading is not required and was the last thing Epic did.** Unreal shipped two releases
  on full-screen per-material draws, and kept Metal on that path in 5.4. With two material classes
  (opaque and masked) a visibility-buffer resolve in Luminex is one or two full-screen passes;
  binning by material only pays when materials are many.
- **Cluster size is tied to payload bits.** 128 triangles and 2^24 visible clusters fit 32 bits. A
  different cluster size from a library changes the bit budget and the vertex-reuse batching, not
  the algorithm.
- **Fixed capacities with feedback match existing practice.** The scale-down for this project is
  roughly 1/16 of Epic's defaults.

Options for slicing, as this notebook's opinion: (1) offline DAG with a CPU cut validator and a
viewer for the cut; (2) flat GPU cluster cull, LOD cut and hardware raster by vertex pulling, with
forward shading unchanged; (3) two-pass occlusion; (4) visibility target and material resolve with
analytic derivatives, compared against forward; (5) hierarchy traversal when instance counts
justify it; (6) a software rasterizer study gated on 64-bit atomics; (7) pages and streaming with
M11. Slices 1 to 3 need nothing the RHI lacks today. Putting the DAG validator first would let M9
claim the Nanite property that matters most with the least machinery.

## Open questions and what could not be confirmed

- Build time per million triangles and peak build memory: no figure in source or talks read.
- The CPU side of the instance hierarchy (`SceneCulling.cpp`) and the HZB builder were not read.
- The exact quadric formulation (`Quadric.cpp`) was not read line by line; the description of the
  metric comes from the call sites and the simplifier's merge evaluation.
- Whether the `release` branch (5.8.3) differs from `5.8` in any file discussed here was not
  checked; `5.8` was treated as the release.
- Which consoles use the primitive-shader path. **[UNVERIFIED]**
- The 2021 talk's timing table prints the main-pass instance cull as "108ms", which contradicts its
  2.5 ms total and is presumably microseconds. All performance numbers quoted from the talks are
  for Epic's 2021 and 2024 demo content on console hardware, not for Apple GPUs.
- Whether current Unreal on Metal uses compute shading; the 5.4 exclusion is confirmed, the present
  state is left to `nanite-system-integration.md`.
- Metal wave support was read from the platform headers' feature flags only; no Metal shader build
  was run to confirm that Epic's Nanite kernels compile with those wave operations enabled.
- No code clamp of `r.Nanite.MaxVisibleClusters` to the 24-bit payload range was found; the ceiling
  stated in this notebook is implied by the payload layout, not by a checked limit.
- The `5.0` header does not define the sub-pixel constant; where `5.0` set it was not checked.
- Streaming request generation, page fixups and residency policy were read only as far as needed
  to explain leaf flags.
- The HPG 2022 keynote "The Journey to Nanite" was located but not read; nothing here depends on
  it.

## Sources

Unreal Engine source, `EpicGames/UnrealEngine`, read through the GitHub API on 2026-10-01. Primary
branch `ue5-main`; comparisons on `ue6-main`, `5.8`, and `5.0` to `5.7` where stated. Licensed
under the Unreal Engine EULA; described here, not reproduced.

- `Engine/Source/Developer/NaniteBuilder/Private/`: `NaniteBuilder.cpp`, `ClusterDAG.cpp`,
  `ClusterDAG.h`, `Cluster.cpp`, `Cluster.h`, `GraphPartitioner.cpp`, `GraphPartitioner.h`,
  `BVHCluster.h`, and `Encode/` (`NaniteEncode.cpp`, `NaniteEncodeHierarchy.cpp`,
  `NaniteEncodePageAssignment.cpp`, `NaniteEncodeGeometryData.cpp`, `NaniteEncodeConstrain.cpp`,
  `NaniteEncodeTriStrip.cpp`, `NaniteEncodeVertReuseBatch.cpp`, `NaniteEncodeShared.h`)
- `Engine/Source/Developer/NaniteBuilder/NaniteBuilder.Build.cs`
- `Engine/Source/Developer/MeshSimplifier/Private/`: `MeshSimplify.cpp`, `MeshSimplify.h`
- `Engine/Source/Runtime/Renderer/Private/Nanite/`: `NaniteCullRaster.cpp`, `NaniteShared.cpp`,
  `NaniteShading.cpp`, `NaniteMaterials.cpp`, `NaniteComposition.cpp`
- `Engine/Source/Runtime/Engine/Private/Rendering/`: `NaniteStreamingManager.cpp`,
  `NaniteResources.cpp`; `Engine/Source/Runtime/Engine/Public/Rendering/NaniteResources.h`;
  `Engine/Source/Runtime/Engine/Classes/Engine/EngineTypes.h` (mesh settings defaults);
  `Engine/Source/Runtime/RenderCore/Private/RenderUtils.cpp` (`5.3`, `5.4`)
- `Engine/Shaders/Shared/NaniteDefinitions.h`, `Engine/Shaders/Shared/SceneCullingDefinitions.h`
- `Engine/Shaders/Private/Nanite/`: `NaniteClusterCulling.usf`, `NaniteHierarchyTraversal.ush`,
  `NaniteHierarchyTraversalCommon.ush`, `NaniteCulling.ush`, `NaniteCullingCommon.ush`,
  `NaniteHZBCull.ush`, `NaniteInstanceCulling.usf`, `NaniteInstanceHierarchyCulling.usf`,
  `NaniteRasterBinning.usf`, `NaniteRasterizer.usf`, `NaniteRasterizer.ush`,
  `NaniteRasterizationCommon.ush`, `NaniteWritePixel.ush` (also `5.0`), `NaniteDataDecode.ush`,
  `NaniteTranscode.usf`, `NaniteShadeBinning.usf`, `NaniteShadeCommon.ush`,
  `NaniteVertexFactory.ush`, `NaniteDepthExport.usf`, `NaniteExportGBuffer.usf`,
  `NaniteStreaming.ush`
- `Engine/Shaders/Public/Platform/Metal/MetalCommon.ush`
- Commit history on `ue5-main` for `NaniteWritePixel.ush`, `NaniteShadeBinning.usf`,
  `NaniteMaterials.cpp`, `NaniteShading.cpp`, `NaniteEncodeHierarchy.cpp`, `GraphPartitioner.cpp`
  and `BVHCluster.h`

Public material, fetched 2026-10-01:

- Karis, Stubbe, Wihlidal, "A Deep Dive into Nanite Virtualized Geometry", SIGGRAPH 2021 Advances
  in Real-Time Rendering in Games:
  <https://advances.realtimerendering.com/s2021/Karis_Nanite_SIGGRAPH_Advances_2021_final.pdf>
- Wihlidal, "Nanite GPU Driven Materials", GDC 2024:
  <https://media.gdcvault.com/gdc2024/Slides/GDC+slide+presentations/Nanite+GPU+Driven+Materials.pdf>
- Epic Games, "Nanite Virtualized Geometry", Unreal Engine 5.8 documentation:
  <https://dev.epicgames.com/documentation/en-us/unreal-engine/nanite-virtualized-geometry-in-unreal-engine>
- SIGGRAPH Advances in Real-Time Rendering course pages for 2024 and 2025, checked for Nanite
  talks (none found beyond a third-party reference in 2024):
  <https://advances.realtimerendering.com/s2024/index.html>,
  <https://advances.realtimerendering.com/s2025/index.html>
- Karis, "The Journey to Nanite", HPG 2022 keynote, located but not read:
  <https://www.highperformancegraphics.org/slides22/Journey_to_Nanite.pdf>
