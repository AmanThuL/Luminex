# Cluster LOD construction outside Epic

**Status**: Frozen — non-normative  
**Research date:** 2026-10-01

Evidence notebook for the [geometry direction review](../2026-10-01-geometry-direction-review.md),
covering how a crack-free continuous cluster LOD hierarchy is built offline with maintained open
libraries: meshoptimizer and its `clusterlod.h`, the NVIDIA nvpro libraries, Bevy's meshlet builder,
the theory behind them and their lineage. It was collected by reading the cloned repositories and
blog sources directly, the 2021 Nanite course slides and the original papers, plus a local probe on
the repository's Sponza and San Miguel assets. Items marked **[UNVERIFIED]** were not confirmed
against a primary source; recheck them before a plan depends on them.

Epic's own builder is documented in `nanite-core-architecture.md`; runtime traversal and raster in
`cluster-culling-and-rasterization.md`; encodings and pages in `geometry-compression-and-streaming.md`.
Statements about Nanite here come only from the public 2021 course slides or from third-party posts
and are labeled as such.

## Summary

1. **One maintained standalone open builder exists, and it is small.** meshoptimizer 1.3 (2026-09-25, MIT)
   ships `demo/clusterlod.h`, a 962-line single header that builds the whole hierarchy from a handful
   of library calls. NVIDIA has archived both of its own cluster libraries (last pushed 2025-11-11)
   and points to `clusterlod.h`; its actively maintained `vk_lod_clusters` sample uses a one-change
   copy of it.
2. **The earlier notebook's meshoptimizer dates are two years early.** `clusterlod.h` shipped with
   v1.0 on 2025-12-08, not in 2023; the full history is in the table below.
3. **The build is fast and deterministic enough to pin by hash, with one compiler flag.** The local
   probe built San Miguel (5.6 M triangles) in 7 to 8 s on one core and produced bit-identical output
   across runs, optimization levels and an x86-64 build, provided floating-point contraction is
   disabled (`-ffp-contract=off`).
4. **The hard parts are the error metric and stuck simplification, not the algorithm.** With the
   library's permissive mode and sloppy fallback disabled, 25% of San Miguel's triangles ended in
   groups that can never be replaced by a coarser level; with the defaults, 0.22%. Including normals
   in the error raised the triangle count of a one-pixel cut of San Miguel by 1.5 to 5 times
   against a position-only error.
5. **Triangle LOD does not solve foliage.** The meshoptimizer author, the 2021 Nanite slides and
   Bevy's author all say so, and the only mitigation
   (border dilation, added to `clusterlod.h` in v1.3) is described by its author as unsafe outside
   foliage. San Miguel's alpha-masked geometry is 26% of its triangles.

## meshoptimizer today

### Release history of the cluster features

Dates are GitHub release publication dates read through the GitHub API on 2026-10-01; the
repository head was three commits past the v1.3 tag.

| Release | Date | Cluster-relevant change |
|---|---|---|
| v0.6 | 2017-08-24 | First edge-collapse simplifier (`meshopt::simplify`) |
| v0.9 | 2018-11-12 | `meshopt_buildMeshlets`, `meshopt_computeMeshletBounds`, `meshopt_computeClusterBounds` (experimental); `target_error` on `meshopt_simplify` |
| v0.11 | 2019-03-17 | `meshopt_simplifySloppy` (ignores topology) |
| v0.16 | 2021-04-08 | Meshlet builder rewritten (three output arrays, `cone_weight`); old linear algorithm kept as `meshopt_buildMeshletsScan`; `result_error` output on `meshopt_simplify` |
| v0.18 | 2022-08-01 | `meshopt_SimplifyLockBorder` |
| v0.20 | 2023-11-02 | `meshopt_simplifyWithAttributes` (experimental) |
| v0.21 | 2024-06-25 | `meshopt_SimplifySparse`, `meshopt_SimplifyErrorAbsolute`, per-vertex `vertex_lock`, `meshopt_optimizeMeshlet`; the notes name "Nanite-style processing pipelines" |
| v0.22 | 2024-10-25 | `meshopt_SimplifyPrune`; improved attribute metric; `meshopt_generateProvokingIndexBuffer` for visibility buffers |
| v0.23 | 2025-03-14 | `meshopt_buildMeshletsFlex`, `meshopt_partitionClusters`, `meshopt_computeSphereBounds` (experimental); `max_vertices` up to 256 |
| v0.24 | 2025-06-12 | `meshopt_buildMeshletsSpatial` (SAH clusters for ray tracing); partitioner accepts positions |
| v0.25 | 2025-08-20 | `meshopt_simplifyWithUpdate`, `meshopt_SimplifyPermissive`, `meshopt_SimplifyRegularize`, `meshopt_generatePositionRemap`; `meshopt_partitionClusters` stable |
| v1.0 | 2025-12-08 | `clusterlod.h` maintained alongside the library; everything but permissive mode declared stable; partitions capped at target + target/3 and merged across disconnected clusters |
| v1.1 | 2026-04-02 | Meshlet codec (`meshopt_encodeMeshlet`, `meshopt_decodeMeshlet`), `meshopt_optimizeMeshletLevel`, `meshopt_extractMeshletIndices`, `meshopt_SimplifyVertex_Priority`, `meshopt_SimplifyRegularizeLight` |
| v1.2 | 2026-06-30 | `clodBuildHierarchy` in `clusterlod.h`; meshlet codec stable; partitioner about 2x faster at size 64 |
| v1.3 | 2026-09-25 | Permissive mode stable; `meshopt_SimplifyErrorClamped` and `meshopt_SimplifyPreserveFolds` (experimental); `simplify_dilate_borders` in `clusterlod.h`; voxel remesher `meshopt_remesh` |

Source: [meshoptimizer releases](https://github.com/zeux/meshoptimizer/releases). The repository
history adds the precursors: `demo/nanite.cpp` first appeared on 2024-09-03 with METIS as an optional
dependency, the partitioner source on 2025-02-10, `clusterlod.h` on 2025-09-22 and the meshlet codec
source on 2026-01-04 (git log of the cloned repository).

The license is MIT for the library and for `clusterlod.h`. The README states the stability contract:
stable APIs keep API, ABI and behavior compatibility, but "this does not mean that the output of
the algorithms will be identical: future versions may improve the algorithms and produce different
results." `clusterlod.h` is explicitly outside that contract (v1.0 announcement: the area "is
quickly evolving, and it's difficult to provide the same API stability guarantees").

### Building blocks

| Function | What it does for a hierarchy build |
|---|---|
| `meshopt_generatePositionRemap` | Maps every vertex to the first vertex with the same position, so connectivity and locking ignore attribute seams |
| `meshopt_buildMeshletsFlex` | Raster-oriented clusters with `min_triangles`, `max_triangles` and a `split_factor` (2.0 recommended) that decides when growing the cluster radius is worth filling it |
| `meshopt_buildMeshletsSpatial` | SAH-driven clusters for ray tracing with a `fill_weight` (0 to 1; the header calls 0.5 a safe default); usable for raster |
| `meshopt_partitionClusters` | Groups clusters by shared vertices and, when positions are given, spatial proximity; partitions reach at most target + target/3 |
| `meshopt_simplifyWithAttributes` | Edge-collapse simplifier over a sparse index subset with per-vertex locks, attribute weights and an absolute result error |
| `meshopt_simplifySloppy` | Topology-free fallback that always reaches the target, with poor attribute quality |
| `meshopt_computeClusterBounds`, `meshopt_computeMeshletBounds` | Bounding sphere and normal cone of one cluster |
| `meshopt_computeSphereBounds` | Sphere enclosing a set of spheres (used for group bounds) |
| `meshopt_optimizeMeshletLevel`, `meshopt_extractMeshletIndices` | Reorder a cluster for locality and compression; convert global indices to 8-bit local indices plus a vertex reference list |
| `meshopt_encodeMeshlet`, `meshopt_decodeMeshlet` | Per-cluster topology codec; the README quotes 9 to 12 bits per triangle in aggregate with vertex references, 5 to 8 after a general-purpose compressor, decoded at 7 to 10 GB/s on a desktop core; the local probe measured 6.5 to 6.7 bits for triangles alone |
| `meshopt_spatialClusterPoints` | Fixed-size spatial grouping of points, used to build the group hierarchy |

Source: `README.md` and `src/meshoptimizer.h` in the cloned repository. All simplification functions
except `meshopt_simplifyWithUpdate` return a new index buffer over the *original* vertices; they
never create vertices.

### Simplification option flags

| Flag | Meaning | In `clusterlod.h` |
|---|---|---|
| `meshopt_SimplifyLockBorder` | Lock every open border edge of the input | Not used; explicit locks instead |
| `meshopt_SimplifySparse` | Avoid work proportional to the whole vertex buffer; error becomes relative to the subset | Always |
| `meshopt_SimplifyErrorAbsolute` | Error limit and result in mesh units | Always |
| `meshopt_SimplifyPrune` | Remove small disconnected components | Unusable: needs the whole mesh |
| `meshopt_SimplifyRegularize`, `…RegularizeLight` | More uniform triangles at some geometric cost; for deforming meshes | Optional |
| `meshopt_SimplifyPermissive` | Allow collapses across attribute seams except at vertices tagged `meshopt_SimplifyVertex_Protect` | On by default |
| `meshopt_SimplifyPreserveFolds` (experimental) | Reduce erosion of creases and double-sided sheets | Optional |
| `meshopt_SimplifyErrorClamped` (experimental) | Clamp attribute error to the scale of the area removed | On by default |
| `vertex_lock` bits `Lock`, `Protect`, `Priority` | Per-vertex: immovable; seam kept under permissive mode; more likely to survive | `Lock` on group borders, `Protect` on UV seams |

### `clusterlod.h`

**API.** `clodBuild(config, mesh, callback)` takes positions, an optional float attribute block with
per-attribute weights, an optional lock array and a bit mask of attributes whose discontinuities
must be protected. It calls the callback once per group, in order of increasing depth, with the
group and its clusters; the value the callback returns becomes the group's ID. `clodLocalIndices`
converts a cluster to local indices. `clodBuildHierarchy` builds a selection hierarchy afterwards.

**Defaults** from `clodDefaultConfig(max_triangles)`, which accepts 4 to 256:

| Field | Default | Meaning |
|---|---|---|
| `max_vertices` | `max_triangles` | Use up to 256 when the source has many seams |
| `min_triangles` | `max_triangles / 3` | Lower bound used by the Flex builder |
| `partition_size` | 16 (so at most 21) | Clusters per group |
| `partition_spatial` | true | Give positions to the partitioner |
| `partition_sort` | false | Morton-sort groups for streaming locality |
| `simplify_ratio` | 0.5 | Target triangle fraction per group |
| `simplify_threshold` | 0.85 | A group that keeps more than this fraction is "stuck" |
| `simplify_error_merge_previous` | 1.0 | Scale on the inherited error |
| `simplify_error_merge_additive` | 0 | Extra share of the new error |
| `simplify_permissive` | true | Regular mode is the alternative, with permissive as a fallback |
| `simplify_fallback_sloppy` | true | Sloppy simplification when still over target |
| `simplify_error_factor_sloppy` | 2.0 | Error multiplier after the sloppy fallback |
| `simplify_error_clamped` | true | See the flag table |
| `simplify_error_edge_limit` | 0 (off) | Cap error by a multiple of the group's longest "short edge" |
| `simplify_dilate_borders` | false | Foliage area compensation; mutates positions in place |
| `optimize_clusters`, level | true, 1 | Triangle order per cluster; level 3 compresses best |

`clodDefaultConfigRT` switches to the spatial clusterizer with `fill_weight` 0.5, `min_triangles`
at a quarter of the maximum and `max_vertices` at twice the triangle limit, capped at 256.

**Algorithm**, restated from the implementation:

1. Build the position remap. If a protect mask is given, tag each vertex whose masked attributes
   differ from the canonical vertex at the same position.
2. Clusterize the whole mesh. Each level-0 cluster gets a tight bounding sphere and error 0.
3. While more than one cluster remains:
   1. Partition the clusters into groups using position-remapped indices. If the cluster count is
      at most `partition_size`, they form one group.
   2. Lock every position used by more than one group. The mesh's own open border is *not* locked.
   3. For each group: concatenate its clusters' triangles; set the group sphere to a sphere that
      encloses the member clusters' stored spheres and the group error to the largest member error;
      simplify to half the triangles under the locks, falling back to permissive and then sloppy
      simplification when the result is still over target (by default the first attempt is already
      permissive).
   4. If the result keeps more than 85% of the triangles, emit the group as **terminal** with error
      `FLT_MAX` and drop its clusters from further processing.
   5. Otherwise set the group error to `max(previous * merge_previous, new) + new * additive`, emit
      the group, optionally dilate, split the simplified triangles into new clusters, and give every
      new cluster the *group's* sphere and error as its stored bounds.
4. Emit the last remaining cluster as a terminal group.

**What it emits.** Per group: `depth` and `simplified` (sphere center, radius, error). Per cluster:
`refined` (the ID of the finer group it was simplified from, or -1 for original geometry), a culling
sphere whose error field is documented as not monotonic, global indices valid only during the
callback, and the unique vertex count. Normal cones are not emitted; the caller computes them with
`meshopt_computeClusterBounds`. Because indices refer to the original vertex buffer, a caller can
keep one shared vertex pool for all levels, unless dilation is enabled, in which case the header
instructs the caller to copy positions per cluster inside the callback.

**Runtime selection** as the header specifies it. The projected error of a bounds record is
`error / max(distance(center, camera) - radius, znear) * (proj[1][1] * 0.5)`, a fraction of the
viewport height. A cluster is drawn when its own group's `simplified` error is over the threshold
and it either is original geometry or the `simplified` error of `groups[refined]` is at or under the
threshold. `clodBuildHierarchy` returns a forest with one tree per depth level, roots first, each
leaf one group and each inner node holding the enclosing sphere and maximum error of up to
`node_width` children; traversal descends while a node's projected error is over the threshold.

### Lessons from the author's posts

- **Throughput.** The Zorah scene (1.64 B triangles, 18.9 B with instancing, a 36 GB glTF) went
  from about 30 minutes and over 180 GB of memory in NVIDIA's original pipeline to 2 m 35 s on a
  16-core Ryzen 7950X, with 45 to 60 GB peak, before serialization. The gains came from fixing
  work proportional to the full vertex count on sparse subsets, processing meshes largest first
  across threads, a SIMD box merge, Morton-sorting triangles first, and a per-thread arena for
  library allocations ([Billions of triangles in minutes](https://zeux.io/2025/09/30/billions-of-triangles-in-minutes/)).
  The 2026 version with attributes and compression takes about 3 m 45 s
  ([Billions of triangles redux](https://zeux.io/2026/09/30/billions-of-triangles-redux/)).
- **Reindex before building.** The source had meshes with 90 M vertices for 30 M triangles; poor
  indexing hurts both simplification quality and time (2025 post).
- **Parallelize across meshes, not inside one.** External parallelism shares no data; NVIDIA later
  removed its internal-parallel mode because it "made the result non-deterministic and rarely paid
  off" (`vk_lod_clusters` changelog, 2026-09-14).
- **The hierarchy doubles the triangle count.** Zorah stores 3.26 B triangles for 1.64 B source
  triangles, the geometric series 1 + 1/2 + 1/4 + … (2026 post).
- **Error drives memory.** "If a cluster error is higher than necessary, a higher resolution
  version of it will be streamed in even if using it as is would not be noticeable." Clamping
  attribute error cut the streamed geometry pool for the default view from 996 MB to 678 MB (2026
  post).
- **Topology compression is cheap to adopt.** The meshlet codec reduced Zorah's cluster indices
  from 9.8 GB to about 2.6 GB, around 6.5 bits per triangle (2026 post).
- **Cluster size is a tradeoff, not a constant.** Larger clusters reuse vertices better, but
  occlusion-culling efficiency fell from about 80% at 64 triangles to 66% at 256 in the author's
  test scene, and cone culling becomes nearly useless
  ([Meshlet size tradeoffs](https://zeux.io/2023/01/16/meshlet-size-tradeoffs/)).
- **The author's own verdict is cautious.** "Should all engines embrace clustered level of detail?
  I'm still not sure… runtime efficiency of these systems is often worse than potential
  alternatives… and on-disk impact is non-trivial" (2026 post).

## NVIDIA nvpro libraries

| Repository | State on 2026-10-01 | What it is |
|---|---|---|
| [`nv_cluster_builder`](https://github.com/nvpro-samples/nv_cluster_builder) | Archived; last push 2025-11-11; Apache-2.0 | Generic spatial clusterer: recursive axis-aligned splits like a BVH build, with optional weighted adjacency. Its README says the core ideas were "adopted and further optimized" in `meshopt_buildMeshletsSpatial` |
| [`nv_cluster_lod_builder`](https://github.com/nvpro-samples/nv_cluster_lod_builder) (called `nv_cluster_lod_library` in the sample's notes) | Archived; last push 2025-11-11; Apache-2.0 | Hierarchy builder over the clusterer plus meshoptimizer's simplifier. Its README redirects readers to `clusterlod.h` |
| [`vk_lod_clusters`](https://github.com/nvpro-samples/vk_lod_clusters) | Active; last push 2026-09-30; Apache-2.0 | Vulkan sample: build, cache, streaming, mesh-shader raster and cluster ray tracing |

Differences that matter:

- **Grouping.** The archived builder grouped clusters by minimum cut over a graph whose edge weights
  count shared, and especially locked, vertices, to push old borders into group interiors. It
  ignored attributes: "Texture seams are not preserved and in general vertex attributes are yet to
  be plumbed through", and its vertex count per cluster was unbounded (README, Limitations).
- **Replacement.** The sample switched to `clusterlod.h` on 2025-09-25, reporting "about 5x faster
  and 20x less memory during processing, also deterministic", while noting that the old library
  then did better "on meshes made of topology with little connectivity (leaves, rubble)". The
  partitioner fix for disconnected clusters followed in meshoptimizer 1.0, and the old library was
  removed on 2025-11-10 (changelog).
- **Local change.** The sample's copy differs from upstream only by emitting an artificial
  single-cluster terminal group when the top of the hierarchy is stuck, because its runtime assumes
  one root cluster per geometry (changelog, 2026-07-30 and 2026-09-14).
- **Configuration it settled on.** Attribute weights of 0.5 for normals, 0.5 for texture
  coordinates, 0 for tangents, 0.2 for the tangent sign and 0.1 for a material attribute;
  `loderrormergeprevious` 1.5; edge limit 1; clamped error on; dilation only for geometry with a
  two-sided material; per-mesh overrides by name pattern; `partition_sort` on
  ([scene processing](https://github.com/nvpro-samples/vk_lod_clusters/blob/main/docs/scene_processing.md)).
- **Bugs worth learning from.** A wrong attribute stride fed the simplifier misaligned data when any
  weight was zero (2026-09-16); a stuck top level broke the single-root assumption (2026-07-30);
  the pixel-error convention changed from radius to diameter (2026-08-28); the cache format broke
  compatibility at least seven times in a year.
- **Cache.** Results go to a memory-mappable file next to the model, with "only few compatibility
  checks", and group blobs are compressed on disk by default since 2026-08-03.

## Bevy's meshlet builder

Read from the author's three posts and from `crates/bevy_pbr/src/meshlet/from_mesh.rs` on `main`.

| Release | Post date | Build change | Lesson |
|---|---|---|---|
| 0.14 | 2024-06-09 | `meshopt_buildMeshlets`, METIS groups of about 4, `meshopt_simplify` with `LOCK_BORDER`, position error only, error summed up the levels | "In practice, my current code can't get to that point [a single root] for most meshes." The Stanford bunny stopped at 7 levels and 19 meshlets, with fill rates down to 20% |
| 0.15 | 2024-11-14 | 255 vertices and 128 triangles per meshlet; target error `f32::MAX`; group error is the maximum of children and own; group sphere encloses the children's spheres; groups of 8; locks only on group borders; position-only adjacency; METIS seed; normals in the error | "Using manual vertex locks to only lock vertices belonging to shared edges between meshlets… fixes this issue"; 12 levels down to one meshlet. "DAG building is really, really important" |
| 0.16 | 2025-03-27 | METIS also clusters triangles into meshlets, minimizing shared vertices; `UFactor` 1; partition count undershoots | Tiny meshlets under 10 triangles "tend to get 'stuck'"; fewer shared vertices means fewer locks |
| 0.17 | (no post) | Selection BVH built in the same file, merged 2025-06-29 by other contributors | The author's 2025-09-03 retrospective says he did not work on it; streaming is still future work |

Pitfalls the series documents:

- **`LOCK_BORDER` is the wrong lock.** It locks the whole open border of the simplified subset,
  including the original mesh border, at every level, so the hierarchy cannot converge.
- **Vertex-bound clusters.** With equal vertex and triangle limits "most meshlets [have] less than
  `t` triangles"; a 2:1 vertex-to-triangle limit fills them (0.15 post).
- **Error accumulation.** The first version added the child error to the parent error; 0.15
  replaced the sum with a maximum and derived the sphere from the children instead of from the
  simplified geometry, which is what makes the projected error monotonic.
- **The partitioner must be seeded.** METIS options carry a seed; without it the output changes
  between runs (0.15 post; the current source sets seed 17).
- **Per-meshlet vertex copies need compression.** Duplicating vertex data per meshlet for future
  streaming was affordable only with meshlet-relative quantized positions and octahedral normals;
  the bunny went from 5.05 MB to 3.61 MB on disk across the two releases (0.15 post).
- **A flat per-cluster test does not scale.** On a scene of 1,041 cliff instances with 32,217
  meshlets each (about 33.5 M clusters) the first culling pass took 1.27 ms against 0.19 ms for
  the bunny scene; "having to dispatch a thread per cluster in the
  scene is an enormous waste" (0.15 post).

The current source still uses METIS for both clustering and grouping (seed 17, `UFactor` 1 and
200), groups of 8, normal weight 0.5, `Sparse | ErrorAbsolute` and no permissive or sloppy fallback.
It rejects a group that keeps more than 60% of its triangles, puts stuck meshlets back in the queue
when enough others succeeded, and asserts error and sphere monotonicity over the finished BVH.

## Theory

### Why "group, lock, simplify, split" gives crack-free cuts

Let the level-`L` clusters tile the surface, and let groups tile the clusters. For a group `g`, call
its clusters' triangles `T(g)` and the simplified triangles `S(g)`. The only vertices that
simplification may not touch are those shared with another group at the same level, so `T(g)` and
`S(g)` meet their neighbors along identical edges. Three consequences follow.

- Swapping `T(g)` for `S(g)` is local. Any subset of groups can be swapped independently and the
  surface stays as closed as the input, because groups only touch along edges neither side moved.
- Swapping all groups yields the level-`L+1` surface, which is then cut into new clusters and
  *regrouped differently*. The split has no relation to the old clusters: a new cluster only
  remembers which group it came from.
- A mixed cut is closed. An edge between two regions drawn at different levels lies on a group
  border at every level between them, since a group holding that edge in its interior would have
  replaced both sides together (this presumes the consistent one-cluster-per-path cut that the
  monotonic error of the next section guarantees). The edge was therefore locked in every
  simplification step either side went through, and both sides contain it unchanged.

The open border of the mesh itself is not locked and does move. Two meshes that are built
separately but meet along a shared edge therefore need explicit locks on that edge; this is the
case for a model split into one primitive per material.

### Why stored error must be monotonic, and how builders enforce it

Give each cluster two records: its *own* record, the bounds and error of the group it was
simplified from (zero error for original geometry), and its *parent* record, the bounds and error of
the group it belongs to (infinite for a terminal group). Every cluster produced from `g` has `g` as
its own record; every cluster inside `g` has `g` as its parent record. So "is `g` replaced by its
simplification?" is asked of exactly one number by both sides.

Draw a cluster when its own error is acceptable and its parent error is not. If error never
decreases toward the root, the intervals `[own, parent)` along any path from an original cluster to
a root tile `[0, ∞)`, so exactly one cluster on each path is drawn for any threshold: no holes and
no overlaps. If a parent could have a smaller error than a child, some threshold would select both
or neither.

The error is compared after projection, so the projected value must be monotonic for every camera.
With `error / max(distance - radius, znear)`, that holds when the parent's error is at least the
child's and the parent's sphere contains the child's, since the nearest point of the larger sphere
is at least as near. Builders enforce both offline:

- `clusterlod.h` takes the maximum of the member errors and the new error, and builds the group
  sphere from the members' *stored* spheres. Its source warns that using the precise bounds of the
  merged or simplified mesh "may violate monotonicity".
- The 2021 slides state it as "Parent view error >= child view error… forced during the offline DAG
  building by modifying the parent's stored error and bounds".
- NVIDIA's documentation describes the failure it prevents: a simplified group whose sphere lies
  farther from the camera than its source group's sphere can project to a smaller error and be
  drawn on top of its own source.

### Why the test runs independently per cluster

All clusters in a group carry the same parent record, and all clusters produced from a group carry
the same own record, so every cluster reaches the same verdict about every group without
communication: "same input, same output" (2021 slides). The condition is the slides' "Render:
`ParentError > threshold && ClusterError <= threshold`". No DAG edges are needed at runtime, only
two sphere-and-error records per cluster, or two group indices into a group table.

### DAG versus tree

If each level's groups nested inside the previous level's groups, the result would be a tree, and
the border of a top-level region would stay locked at every level. Quick-VDR documents the effect:
"After simplifying several levels of the hierarchy most of the vertices in the base mesh… are shared
vertices." The fix is to choose groups so that last level's borders fall in the interior of this
level's groups: "Locked one level, unlocked the next" (2021 slides). Clusters produced from one
group then land in several next-level groups, and one group contains clusters from several source
groups. That many-to-many relation is the DAG. Grouping is a graph-partitioning problem whose
objective is the fewest locked edges; Nanite and Bevy use METIS for it, meshoptimizer its own
partitioner.

### Why a BVH over groups is added at scale

The per-cluster test is parallel but costs one evaluation per cluster per instance per view, and
most clusters are far too detailed for a distant instance. A cluster is useless when its parent
error is already under the threshold, so a hierarchy keyed on *parent* error lets traversal skip
whole subtrees: each node stores a sphere enclosing its children and the maximum of their parent
errors, and traversal descends only while the node's projected error is over the threshold. The
2021 slides describe a "tree based on ParentError, not ClusterError", eight wide, storing the
"max of children's ParentError"; `clodBuildHierarchy` and
Bevy's builder construct the same structure with groups as leaves. It also makes frustum and
occlusion culling hierarchical.

### When simplification gets stuck

The simplifier classifies each vertex as manifold, border, seam, complex, fringe or locked and only
allows collapses that keep those structures: a border vertex slides along its border, a seam vertex
along its seam, and a complex (non-manifold) vertex collapses only onto another complex, fringe or
locked vertex (`src/simplifier.cpp`). A group fails to halve when:

- **Too many vertices are locked.** Small or ragged clusters have a high border-to-area ratio.
- **Attribute seams dominate.** Faceted shading makes every edge a seam; UV islands and hard normals
  add more. The README warns the simplifier then "may not be able to reduce the triangle count at
  all".
- **The group is many small islands.** Leaves, rubble and gravel cannot shrink below a few
  triangles per island without deleting islands, and component pruning is unavailable because it
  needs the whole mesh (2026 post).
- **Clusters are vertex-bound**, so the next level starts with more, emptier clusters.

Mitigations in use:

| Mitigation | Where | Cost |
|---|---|---|
| Lock only inter-group borders, on position-remapped vertices | All three builders | None; required |
| Permissive mode with protected UV seams | `clusterlod.h` default | Attribute quality depends on the error metric; needs attributes passed in |
| Sloppy fallback with doubled error | `clusterlod.h` default; about 0.01% of Zorah's groups | "Low-quality output that disregards attributes completely" |
| Declare the group terminal | `clusterlod.h` (over 85% kept), Bevy (over 60%) | Those triangles are drawn at every distance |
| Retry stuck clusters with new neighbors next level | Bevy | Extra levels |
| Partition to minimize shared vertices | Bevy (METIS on triangles) | Extra dependency and build time |
| Spatial as well as topological grouping | meshoptimizer 1.0 partitioner | None |
| Cap error by edge length | `simplify_error_edge_limit` | The author calls it a failsafe that signals an upstream problem |
| Force a single root cluster | NVIDIA's local change | One artificial group |

### Attribute-aware error

meshoptimizer extends quadrics with attributes after Hoppe's
[new quadric metric](https://hhoppe.com/proj/newqem/): each attribute is modeled as a linear
function over space and its squared deviation is added with a weight. A weight `w` means a change
of `1/w` in the attribute over a distance `d` counts like a positional change of `d`; the README
suggests about 0.5 to 1 for normals and, if texture coordinates are included at all, 10 to 100 or
the reciprocal of the square root of the mean UV triangle area. Three facts matter for LOD
selection:

- The single returned error mixes position and attributes, and it is that mixed number that is
  projected to pixels. Attribute weights are therefore a direct multiplier on triangles drawn.
- The linear model extrapolates attributes without bound, so normals can leave the unit range and
  report errors that never appear on screen. `meshopt_SimplifyErrorClamped` bounds the attribute
  term by the accumulated triangle area, an idea the author credits to Unreal's simplifier (2026
  post).
- The error cannot know the material. The 2021 slides call this "the hardest part", put "probably a
  man year" into it and concede the weights are a "complete heuristic hack".

Tangents add seams wherever they are discontinuous. Bevy removed stored tangents and derives them
in screen space (0.15 post); the 2021 slides mention "implicit tangents"; NVIDIA weights only the
tangent sign.

### Thin features and volume loss

`meshopt_simplifyWithAttributes` keeps a subset of the original vertices, and `clusterlod.h` does
not use the variant that solves for new positions (2026 post). A collapse to an existing vertex
cuts across curvature, so convex shapes shrink level by level; this inference is the notebook's, not
a quoted claim. Thin plates modeled as two close sheets can fold or erode, which is what
`meshopt_SimplifyPreserveFolds` addresses. Rods and wires collapse cheaply along their length and
vanish once the allowed error reaches their radius: the edge-limit code keeps a special case
"for thin and long triangles like wires". Quadric error bounds distance to the original planes; it
does not bound lost silhouette coverage.

### Alpha-masked and aggregate geometry

Simplification preserves a surface; aggregates are perceived as *coverage*. Once every leaf is one
triangle, the only remaining collapse deletes leaves, and because all clusters of a hedge have
similar error they all switch at the same distance, "an act of coordinated omission": hedges thin
and then disappear (2026 post). The 2021 slides list the same case as open: "Subpixel partial
coverage and prefiltering is especially important for aggregate geometry like leaves and grass.
Solving that case is still an open question", and propose volumetric representations for subpixel
features. The available mitigation redistributes lost area by pushing surviving open borders
outward. Unreal's Preserve Area does it per group; `clusterlod.h` restricts it to area lost in
border triangles, skips it when less than a quarter of the border area survives, and still warns
that it is "not rigorous unless you *know* your geometry is triangle-based foliage" (2026 post).

Alpha-masked cards add two problems. Their detail lives in the texture, so fewer triangles do not
reduce alpha-test or overdraw cost; and dilating a card's border "may result in odd looking
branches from far away if they were baked into the texture" (2026 post). Geometry LOD does not
prefilter alpha.

## Lineage and recent work

| Work | Contribution to this design |
|---|---|
| Yoon, Salomon, Gayle, Manocha, [Quick-VDR](http://gamma-web.iacs.umd.edu/QVDR/QVDR-vis.pdf), IEEE Visualization 2004 | A clustered hierarchy of progressive meshes; names the locked-border problem and replaces precomputed constraints with runtime cluster dependencies |
| Cignoni, Ganovelli, Gobbetti, Marton, Ponchio, Scopigno, [Batched Multi-Triangulation](https://vcgdata.isti.cnr.it/Publications/2005/CGGMPS05/BatchedMT_Vis05.pdf), IEEE Visualization 2005 | Moves the multi-triangulation DAG "from triangles to precomputed optimized triangle patches"; alternating partitions so that no fragment keeps a border across levels |
| Ponchio, [dissertation](https://d-nb.info/997062789/34) (2008) and [Nexus](https://github.com/cnr-isti-vclab/nexus) | The readable account of batched multiresolution that the 2021 slides recommend; Nexus is its public implementation, GPL for C++ and MIT for JavaScript. Only the dissertation's title page was read for this notebook |
| Karis, Stubbe, Wihlidal, [Nanite: A Deep Dive](https://advances.realtimerendering.com/s2021/Karis_Nanite_SIGGRAPH_Advances_2021_final.pdf), SIGGRAPH 2021 course | Groups clusters instead of triangles so groups are multiples of 128 triangles; graph partitioning for grouping; parallel selection by two errors; BVH over parent error |
| Benthin and Peters, [Real-Time Ray Tracing of Micro-Poly Geometry with Hierarchical Level of Detail](https://momentsingraphics.de/HPG2023.html), HPG 2023 | The same hierarchy for ray tracing with per-frame BVH build over selected clusters; code on an Embree branch |
| Jensen, Frisvad, Bærentzen, [Performance Comparison of Meshlet Generation Strategies](https://jcgt.org/published/0012/02/01/), JCGT 2023 | Meshlet builders compared on meshes of 70 K to 39 M triangles. Reported: read from a search summary only |
| Pettett and Mantiuk, [Multiresolution Mesh Rendering Engine](https://www.cl.cam.ac.uk/~rkm38/pdfs/pettett2024_multiresolution_mesh_rendering.pdf), CESCG 2024 (not peer reviewed) | A student reimplementation with two selection methods; useful as a second small-team data point |
| Kuth et al., [Towards Practical Meshlet Compression](https://arxiv.org/abs/2404.06359), 2024 | Meshlet codec decodable in a mesh shader; belongs to the compression notebook |
| Liu et al., [Simplifying Textured Triangle Meshes in the Wild](https://arxiv.org/abs/2409.15458), 2024, revised 2025 | A modified quadric and UV-independent correspondence for non-manifold, multi-component meshes, the inputs on which ordinary quadric simplification gets stuck |
| Kulkarni and Narayanan, [A Comprehensive Guide to Mesh Simplification using Edge Collapse](https://arxiv.org/abs/2512.19959), 2025 | Implementation-level survey of quadric, attribute-aware and Lindstrom–Turk criteria and their safeguards |
| Ladeuil, Trabucato, Vaisse, Faraj, [Construction of clustered HLOD with As-Simplified-As-Possible boundaries](https://onlinelibrary.wiley.com/doi/10.1111/cgf.70380), Computer Graphics Forum 2026 | Simplifies cluster borders consistently instead of locking them. Reported: the publisher page returned HTTP 403, so only the search abstract was read |

## Practicalities

**Build time and memory.** About 0.65 M source triangles per second per core on a Ryzen 7950X
(1.64 B triangles in 155 s on 16 cores, from the 2025 post) and 0.74 to 0.78 M per second on one
M3 Max core in the local probe. Memory in the author's run was 45 to 60 GB for 16 concurrent meshes
of up to tens of millions of triangles; the probe peaked at 640 to 700 MB across runs for San
Miguel, including the loaded source. Largest-first scheduling and a triangle budget for concurrent meshes are the documented
controls.

**Determinism.** The library sources contain no random numbers, threads or clocks, and the only
math-library calls in the clusterizer, partitioner and simplifier are `sqrtf` and `fabsf`. The
probe confirms run-to-run identity and shows the one hazard: floating-point contraction. Results
are identical across optimization levels on arm64, and across arm64 and x86-64 once contraction is
turned off on arm64.
The other hazards are a library upgrade, which the README says may change output, and any
user-added internal parallelism. METIS-based builders need an explicit seed.

**On-disk size.**

| System | Stored | Per source triangle | Source |
|---|---|---|---|
| Nanite, 2021 demo | 4.61 GB for 433 M source triangles | 11.4 bytes | 2021 slides |
| `vk_lod_clusters`, Zorah, 2026 | 47.0 GB for 1.64 B source triangles, per-cluster vertex copies | about 28.7 bytes (derived) | 2026 post |
| Bevy 0.15, cliff | 49.83 MB for about 2.0 M source triangles | about 25 bytes (derived) | 0.15 post |

For the repository's content, the probe's counts give these estimates, assuming the 32-byte
position, normal and UV vertex stored in the converted glTF buffers:

| Layout | Sponza (9.0 MB source buffer) | San Miguel (255 MB source buffer) |
|---|---|---|
| Shared vertex pool kept as is; add per-cluster vertex references (4 bytes) and local indices (3 bytes per triangle) | +3.3 MB | +84 MB |
| Per-cluster vertex copies at 32 bytes, plus local indices | 15.6 MB | 439 MB |
| Per-cluster vertex copies quantized to 16 bytes, plus local indices | 8.6 MB | 236 MB |

The meshlet codec would shrink the local indices from 24 to about 6.5 bits per triangle in both
scenes.

**Small meshes.** A mesh that fits one cluster yields one terminal group at depth 0 without special
handling; 3 of Sponza's 25 primitives and 36 of San Miguel's 281 did. The hierarchy ends at one
cluster, at which point "cost stops scaling with resolution" and instance count becomes the cost
(2021 slides, "Tiny instances"). A builder can skip the hierarchy below a triangle count and keep
the ordinary indexed mesh; the threshold is a measurement question.

**Instancing.** The hierarchy is built once per mesh in mesh space and shared by all instances.
Selection transforms the sphere by the instance matrix and scales radius and error by the instance's
scale, as Bevy's selection function does with a single `world_scale`; non-uniform scale needs the
largest axis scale to stay conservative.

## Local probe

**What was run.** meshoptimizer at commit `4c203430` (2026-09-28, three commits after v1.3) was
cloned into the scratch directory. A driver of about 450 lines using the repository's `cgltf.h` and
`clusterlod.h` loaded each glTF primitive (positions, normals, first texture coordinates), welded
bit-identical vertices with `meshopt_generateVertexRemap`, dropped degenerate triangles, and ran
`clodBuild` with `clodDefaultConfig(128)`, normal weights of 0.5 and the texture-coordinate protect
mask, as `demo/nanite.cpp` does. It then ran `clodBuildHierarchy` with width 8, encoded every
cluster's local indices with `meshopt_encodeMeshlet`, hashed all output with FNV-1a, and counted the
triangles of three cuts. Build: Apple clang 21.0.0, `-std=c++17 -O2 -DNDEBUG`, one thread, Apple
M3 Max, macOS 26.7. Inputs were the fetched Sponza and San Miguel glTF files, read only. Nothing
was added to the repository.

Cuts use the header's projected-error formula with a threshold of one pixel at 1080 lines and a
60-degree vertical field of view, a 0.05 near distance, and no frustum or occlusion culling. "Far"
is four scene radii from the bounds center along the diagonal, "mid" is 1.5 radii, and "center" is
the bounds center, which is inside both scenes.

| | Sponza | San Miguel |
|---|---|---|
| Primitives | 25 | 281 |
| Source triangles | 262,267 | 5,608,441 (5,617,451 before dropping degenerates) |
| Vertices after welding | 184,406 | 5,855,519 |
| Build time, one thread | 0.32 to 0.33 s | 7.2 to 7.6 s across runs |
| Peak resident memory | about 30 MB | 640 to 700 MB across runs |
| Groups | 395 | 9,257 |
| Clusters | 4,863 | 129,048 |
| Stored triangles | 522,089 (1.99x) | 11,111,968 (1.98x) |
| Deepest level | 10 | 14 |
| Hierarchy nodes | 466 | 10,764 |
| Triangles in terminal groups | 1,142 (0.44%) | 12,223 (0.22%) |
| Primitives ending in more than one cluster | 0 | 0 |
| Cluster-local vertices | 439,872 (2.39x) | 12,691,514 (2.17x) |
| Meshlet codec, triangles only | 6.50 bits per triangle | 6.67 bits per triangle |
| Level-0 clusters at the full 128 triangles | 72.7% | 35.8% |
| Cut triangles: far, mid, center | 62,202 / 109,286 / 225,522 | 93,681 / 594,669 / 2,551,587 |

Triangles per level, summed over primitives:

| Level | Sponza | San Miguel | | Level | Sponza | San Miguel |
|---|---|---|---|---|---|---|
| 0 | 262,267 | 5,608,441 | | 8 | 788 | 17,996 |
| 1 | 130,971 | 2,788,709 | | 9 | 218 | 8,273 |
| 2 | 65,285 | 1,385,641 | | 10 | 30 | 3,501 |
| 3 | 32,549 | 684,206 | | 11 | | 1,452 |
| 4 | 16,182 | 334,623 | | 12 | | 327 |
| 5 | 7,995 | 161,842 | | 13 | | 96 |
| 6 | 3,943 | 78,664 | | 14 | | 38 |
| 7 | 1,861 | 38,159 | | | | |

San Miguel under configuration changes (one change each from the defaults):

| Variant | Clusters | Terminal triangles | Far cut | Mid cut | Center cut |
|---|---|---|---|---|---|
| Defaults | 129,048 | 0.22% | 93,681 | 594,669 | 2,551,587 |
| Permissive off, sloppy fallback kept | 121,766 | 0.24% | 146,004 | 740,466 | 2,810,422 |
| Permissive off, sloppy off | 115,546 | 25.30% | 1,479,129 | 1,949,858 | 3,454,969 |
| Position-only error (no attributes) | 128,367 | 0.21% | 18,134 | 313,923 | 1,657,178 |
| `max_vertices` 255 | 108,505 | 0.24% | 101,587 | 581,033 | 2,582,775 |
| 64-triangle clusters | 251,929 | 0.11% | 100,853 | 574,672 | 2,469,549 |
| Groups of 32 | 126,536 | 0.22% | 89,997 | 595,531 | 2,737,088 |
| Ray-tracing configuration | 109,149 | 0.41% | 105,104 | 562,174 | 2,445,117 |
| Border dilation on alpha-masked primitives | 128,796 | 0.22% | 165,513 | 642,758 | 2,552,286 |
| Lock positions shared between primitives | 128,227 | 1.19% | 178,457 | 674,395 | 2,590,698 |
| All primitives merged into one mesh | 118,630 | 45 triangles | 316,898 | 1,055,344 | 2,727,574 |

Readings:

- With both fallbacks off, 156 of 281 primitives ended in more than one cluster and a quarter of
  the scene had no coarser level. Sponza under the same setting left 6.2% terminal.
- `max_vertices` 255 raised the share of full level-0 clusters from 35.8% to 65.2% and cut the
  cluster count by 16%. San Miguel has more vertices than triangles, so 128-vertex clusters are
  vertex-bound: level 0 holds 58,544 clusters averaging 96 triangles, which is why the default
  total of 129,048 exceeds the 88 K that full 128-triangle clusters and a doubling hierarchy
  would give (5.6 M / 128 × 2); with `max_vertices` 255, level 0 has 49,108 clusters and the
  total is 108,505.
- 99,334 welded vertices (1.7%) share a position with another primitive. Locking them roughly
  doubled the far cut and changed the center cut by 1.5%; 38 primitives no longer reached one
  cluster. Sponza has only 72 such vertices and was unaffected.
- From inside Sponza, a one-pixel cut keeps 86% of the triangles: the scene is too coarse for
  cluster LOD to reduce much up close.
- Why the merged and dilated variants draw more triangles at distance was not investigated.

Determinism, default configuration, output hash compared per scene:

| Build | Same hash as the `-O2` arm64 build? |
|---|---|
| Repeated runs | Yes |
| `-O0`, `-O3` | Yes |
| `-DMESHOPTIMIZER_NO_SIMD` | Yes |
| `-ffp-contract=off` | No |
| x86-64 `-O2` under Rosetta | No, but identical to the arm64 `-ffp-contract=off` build |
| `-ffast-math` | No |

Limits of the probe: two scenes, one compiler, x86-64 through translation instead of hardware, no
image comparison of any cut, no watertightness check, and no timing of the GPU side.

## Corrections to earlier research

- [`pipeline-state-of-the-art-m7-m11.md`](../2026-09-14-roadmap-review/pipeline-state-of-the-art-m7-m11.md)
  dates the cluster features two years early. It says `clusterlod.h` "landed v1.0 (2023-12-08)",
  v0.24 on 2023-06-12, v1.1 on 2024-04-02 and v1.2 on 2024-06-30, and its verdict repeats "Dec
  2023". The releases were published on 2025-12-08, 2025-06-12, 2026-04-02 and 2026-06-30.
- The same row credits v0.24 with `meshopt_partitionClusters`. That function arrived in v0.23
  (2025-03-14); v0.24 added `meshopt_buildMeshletsSpatial` and the partitioner's position input.
- [`open-source-references-2024-2026.md`](../2026-09-14-roadmap-review/open-source-references-2024-2026.md)
  lists meshoptimizer at v1.2, correct on its date; v1.3 followed on 2026-09-25 and changes the
  defaults relevant here (clamped error, stable permissive mode, border dilation).
- The same notebook presents `vk_lod_clusters` as offering a reusable "meshoptimizer-based
  cluster-generation pipeline". That is right, but the two NVIDIA libraries it was originally built
  on are archived, so the reusable part is `clusterlod.h` itself.
- The pipeline notebook says Bevy's author "reports BVH-based culling still unfinished at 0.16".
  The BVH build and culling were merged on 2025-06-29 by other contributors and shipped in 0.17;
  the author's posts stop at 0.16.

## Implications for Luminex

### What the evidence supports

- **Adopt `clusterlod.h` rather than write a builder.** It is the only maintained standalone option
  in the sources read, it is the one NVIDIA converged on, and it is short enough to vendor and read. Bevy's three releases of
  builder problems are the cost of the alternative.
- **A geometry bake fits the existing hash-pinned setup.** Pin the meshoptimizer commit, compile the
  bake with `-ffp-contract=off`, keep one thread per mesh, and record the configuration in the
  manifest. The probe gives no reason to expect hash drift on Apple Silicon under those rules.
- **Build cost is negligible for current content**: under 10 seconds for San Miguel on one core.
- **Current content will not show the technique off.** Sponza barely simplifies from inside, and a
  quarter of San Miguel's triangles are alpha-masked foliage, the documented failure case. A dense
  opaque asset is needed for a convincing measurement; see `test-content-and-measurement.md`.
- **A flat per-cluster selection pass is enough at this scale.** San Miguel has 129 K clusters in a
  single instance. The group hierarchy costs one extra call at bake time and can be emitted from the
  start, but its traversal can wait until instanced dense content exists.

### Recommended bake, in the shape of `Tools/TextureBake`

A thin command-line tool over a deterministic library function in the asset layer, run by setup
after glTF conversion, writing one binary file and one JSON manifest per source mesh.

**Inputs:** one glTF mesh with its primitives (positions, normals, texture coordinates, indices),
each primitive's alpha mode and double-sided flag, and a configuration record.

**Steps:**

1. Weld bit-identical vertices; drop degenerate triangles; optionally Morton-sort triangles.
2. Find positions shared between primitives of the same mesh and mark them locked (option A below).
3. Run `clodBuild` per primitive with 128 triangles, 255 vertices, groups of 16, normals weighted
   0.5 and texture-coordinate seams protected.
4. In the callback, copy each cluster's local indices and vertex references, compute its culling
   sphere and normal cone, and record its own group and the group it was refined from.
5. Run `clodBuildHierarchy`; write the file; write the manifest.

**Per cluster:** triangle and vertex counts, offsets of local indices and vertex references,
culling sphere, normal cone, index of the group it belongs to, index of the group it was refined
from (or none), primitive and material index.

**Per group:** sphere center and radius, error, depth, cluster range. Terminal groups carry
infinite error.

**Per mesh:** hierarchy nodes, counts per level, terminal triangle count, the configuration, the
meshoptimizer commit, a tool version and the source hash.

### Choices

| Choice | Options | Pick |
|---|---|---|
| Vertex storage | (1) Keep the scene's shared vertex pool and add cluster index data; (2) per-cluster vertex copies, quantized | (1) first: it reuses the existing immutable pool and adds roughly a third to the source buffer size. (2) is needed only for streaming and for border dilation |
| Multi-material meshes | (A) Per-primitive hierarchies with locked shared positions; (B) merge primitives, carry material as a protected attribute, emit per-triangle material | (A) first: it keeps one material per cluster, which the forward path needs. In the probe its cost fell mostly on far views (far cut 1.9x, mid +13%, center +1.5%), and terminal triangles rose from 0.22% to 1.19%. (B) fits a visibility-buffer path |
| Cluster limits | 128 triangles with 128 or 255 vertices; 64 triangles | 128 and 255. Without mesh shaders the vertex limit is not a hardware constraint, and 8-bit local indices still fit |
| Clusterizer | Flex (raster) or Spatial (ray tracing) | Flex now. Spatial gave fewer clusters here but more terminal triangles; revisit when ray tracing needs clusters |
| Selection hierarchy | Emit now or later | Emit now, traverse later |
| Foliage and alpha-masked primitives | (i) Exclude from cluster LOD and keep the ordinary path; (ii) build with dilation; (iii) build without | (i) for the first slice, stated as a limit. (ii) only with image evidence |
| Partitioner | meshoptimizer's or METIS | meshoptimizer's: no extra dependency, deterministic without a seed |

### Pitfall checklist

1. Lock inter-group borders on position-remapped vertices; never use `meshopt_SimplifyLockBorder`.
2. Lock positions shared with separately built neighbors (other primitives, other chunks).
3. Take group bounds from the children's stored bounds, never from the simplified geometry.
4. Keep the stored error a running maximum, and assert monotonic error and nested spheres after the
   build, as Bevy does.
5. Use absolute error with sparse simplification; relative error changes meaning per subset.
6. Pass the correct attribute stride and weights; a mismatch silently corrupts the attribute error.
7. Weld the source and check the vertex-to-triangle ratio before clustering.
8. Report terminal triangles per mesh, and fail the bake over a budget.
9. Decide the single-root question explicitly; upstream can end a stuck mesh in several clusters.
10. Agree on the pixel-error convention (radius or diameter) between bake, shader and documentation.
11. Scale error and radius by instance scale at selection time.
12. Pin the library commit and `-ffp-contract=off`; never enable `-ffast-math` for the bake.
13. Do not parallelize inside one mesh.
14. Copy positions inside the callback if dilation is ever enabled.
15. Version the file format and store the configuration; NVIDIA's cache broke repeatedly without it.

### Rough effort ranking, smallest first

This is a judgment from the material above, not a measurement.

1. Vendor and pin meshoptimizer; bake tool that calls `clodBuild` and writes a manifest.
2. Determinism gate: compile flags, a hash in setup, a rebuild test.
3. Shared-position locks between primitives.
4. Group hierarchy emission.
5. File format and loader into new cluster and group scene tables.
6. A CPU cut oracle: evaluate random cuts and check that the border-edge count matches the source,
   that no surface is drawn twice, and that errors are monotonic. This matches the project's
   existing habit of independent oracles.
7. Quantized per-cluster vertex data and topology codec.
8. Error-metric tuning against image comparisons. Open-ended; Epic reported about a person-year.
9. Foliage and alpha-masked handling. Open-ended, and unsolved in the sources read.

## Open questions and what could not be confirmed

- **Image quality of any cut on Luminex content.** The probe counted triangles only. Whether a
  one-pixel threshold with normal weight 0.5 is invisible under the project's TAA, and how much
  the threshold can be relaxed, needs rendered comparisons.
- **Tangents.** The fetched Sponza and San Miguel files carry no tangent attribute. How the renderer
  derives tangent frames determines whether simplification must preserve them; this was not
  checked.
- **Determinism beyond the probe.** Not tested: real x86-64 hardware, other compilers, other
  library versions, larger meshes, or the `simplify_dilate_borders` path under different builds.
- **Why the merged-mesh and dilated variants select more triangles at distance.**
- **The Ladeuil et al. 2026 paper**, which claims to simplify cluster borders without locking them,
  could not be fetched. If it holds up it removes the need for the regrouping trick.
- **The JCGT 2023 meshlet comparison** was read only as a search summary.
- **Whether meshoptimizer is packaged for the build system in use**, and whether such a package
  includes `demo/clusterlod.h`, was not checked. **[UNVERIFIED]**
- **METIS licensing and packaging** were not checked, since the recommendation avoids it.
  **[UNVERIFIED]**
- **Ponchio's dissertation** was cited through the 2021 slides; only its title page was read.
- **Quality of `meshopt_simplifyWithUpdate` inside a hierarchy.** The author notes `clusterlod.h`
  does not use it; whether solved vertex positions would reduce volume loss enough to justify
  per-cluster vertex data is untested.

## Sources

Fetched and read on 2026-10-01 unless noted.

- meshoptimizer repository, cloned at commit `4c203430`: `README.md`, `src/meshoptimizer.h`,
  `src/simplifier.cpp`, `demo/clusterlod.h`, `demo/nanite.cpp`, `LICENSE.md`, git history —
  https://github.com/zeux/meshoptimizer
- meshoptimizer release notes v0.5 to v1.3 through the GitHub API —
  https://github.com/zeux/meshoptimizer/releases
- meshoptimizer v1.0 announcement — https://meshoptimizer.org/v1
- Arseny Kapoulkine, "Billions of triangles in minutes", 2025-09-30 —
  https://zeux.io/2025/09/30/billions-of-triangles-in-minutes/
- Arseny Kapoulkine, "Billions of triangles redux", 2026-09-30 —
  https://zeux.io/2026/09/30/billions-of-triangles-redux/
- Arseny Kapoulkine, "Meshlet size tradeoffs", 2023-01-16 —
  https://zeux.io/2023/01/16/meshlet-size-tradeoffs/
- nvpro-samples `nv_cluster_builder` README and changelog, cloned; archive status through the GitHub
  API — https://github.com/nvpro-samples/nv_cluster_builder
- nvpro-samples `nv_cluster_lod_builder` README, cloned —
  https://github.com/nvpro-samples/nv_cluster_lod_builder
- nvpro-samples `vk_lod_clusters` README, `CHANGELOG.md`, `docs/lod_generation.md`,
  `docs/scene_processing.md`, `src/meshopt_clusterlod.h`, `src/scene_cluster_lod.cpp`, cloned —
  https://github.com/nvpro-samples/vk_lod_clusters
- JMS55, "Virtual Geometry in Bevy 0.14" — https://jms55.github.io/posts/2024-06-09-virtual-geometry-bevy-0-14/
- JMS55, "Virtual Geometry in Bevy 0.15" — https://jms55.github.io/posts/2024-11-14-virtual-geometry-bevy-0-15/
- JMS55, "Virtual Geometry in Bevy 0.16" — https://jms55.github.io/posts/2025-03-27-virtual-geometry-bevy-0-16/
- JMS55, "Bevy's Fifth Birthday", 2025-09-03 — https://jms55.github.io/posts/2025-09-03-bevy-fifth-birthday/
- Bevy `from_mesh.rs` on `main` and its commit history through the GitHub API —
  https://github.com/bevyengine/bevy/blob/main/crates/bevy_pbr/src/meshlet/from_mesh.rs
- Karis, Stubbe, Wihlidal, "Nanite: A Deep Dive", SIGGRAPH 2021 Advances in Real-Time Rendering,
  slides and speaker notes —
  https://advances.realtimerendering.com/s2021/Karis_Nanite_SIGGRAPH_Advances_2021_final.pdf
- Cignoni et al., "Batched Multi Triangulation", IEEE Visualization 2005 —
  https://vcgdata.isti.cnr.it/Publications/2005/CGGMPS05/BatchedMT_Vis05.pdf
- Yoon et al., "Quick-VDR: Interactive View-Dependent Rendering of Massive Models", IEEE
  Visualization 2004 — http://gamma-web.iacs.umd.edu/QVDR/QVDR-vis.pdf
- Nexus repository page — https://github.com/cnr-isti-vclab/nexus
- Benthin and Peters, HPG 2023 project page — https://momentsingraphics.de/HPG2023.html
- Pettett and Mantiuk, CESCG 2024 —
  https://www.cl.cam.ac.uk/~rkm38/pdfs/pettett2024_multiresolution_mesh_rendering.pdf
- Liu et al., arXiv 2409.15458 — https://arxiv.org/abs/2409.15458
- Kulkarni and Narayanan, arXiv 2512.19959 — https://arxiv.org/abs/2512.19959
- Kuth et al., arXiv 2404.06359 — https://arxiv.org/abs/2404.06359
- Reported only (search summaries; page not readable): Ladeuil et al., Computer Graphics Forum 2026
  — https://onlinelibrary.wiley.com/doi/10.1111/cgf.70380 ; Jensen et al., JCGT 2023 —
  https://jcgt.org/published/0012/02/01/
- Linked from the sources above and not read beyond the title: Hoppe, "New quadric metric" —
  https://hhoppe.com/proj/newqem/ ; Ponchio, "Multiresolution structures for interactive
  visualization of very large 3D datasets", Clausthal University of Technology, 2008 —
  https://d-nb.info/997062789/34
- Local probe: driver and output kept in the session scratch directory, not in the repository.
