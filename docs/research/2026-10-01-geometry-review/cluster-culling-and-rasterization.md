# Cluster culling and rasterization outside Epic

**Status**: Frozen — non-normative  
**Research date:** 2026-10-01

Evidence notebook for the [geometry direction review](../2026-10-01-geometry-direction-review.md),
covering the runtime half of cluster geometry outside Epic: how shipped engines and open projects
traverse, cull and rasterize clusters, what each choice costs, and a staged path from Luminex's
current GPU instance culling toward a Nanite-class runtime. It was collected by downloading the
slide decks, papers, READMEs and shader sources listed under Sources and reading their text and
notes; Unreal source was read only for comparison. Items marked **[UNVERIFIED]** were not
confirmed against a primary source; recheck them before a plan depends on them.

Unless marked **Reported**, a number below comes from a primary deck or page fetched on
2026-10-01. Hardware, resolution and scene are given where the source states them and marked "not
stated" where it does not.

Credit: Nanite and the other Unreal Engine systems described here are the work of Epic Games.
Unreal Engine source is copyright Epic Games, Inc. and was read under the Unreal Engine EULA; this
notebook describes mechanisms in its own words and reproduces no engine code, shader text or
comments. Unreal, Unreal Engine and Nanite are trademarks of Epic Games, Inc.

## Shipped and published designs

Every shipped cluster pipeline surveyed has the same skeleton: instance culling, expansion of
surviving instances into clusters, cluster culling, a compacted visible-cluster list, then one of
four raster back ends. They differ in cluster size, in whether level of detail is a discrete
per-instance choice or a per-cluster cut through a hierarchy, and in what the raster stage writes.

| System (source) | Cluster and LOD | Culling | Raster back end | Output | Published timings |
|---|---|---|---|---|---|
| Ubisoft AC Unity and RedLynx (Haar and Aaltonen, SIGGRAPH 2015) | 64-vertex strips (RedLynx) or 64-triangle clusters (ACU); discrete LOD | CPU quadtree, then GPU instance, chunk expansion, cluster frustum/occlusion, baked per-cluster triangle backface masks; two-phase in the RedLynx path | RedLynx: two `DrawInstancedIndirect` calls over fixed-topology clusters. ACU: index-buffer compaction then multi-draw | G-buffer; RedLynx stores UV and tangent (64 bits) for deferred texturing | Xbox One, 1080p, 250,000 moving objects: 2.3 ms GPU total, 0.2 ms CPU (table in the two-phase section) |
| Frostbite (Wihlidal, GDC 2016) | 256-triangle clusters; discrete LOD | Cluster cone/frustum/Hi-Z, then per-triangle orientation, depth, small-primitive, frustum filters in compute | Compacted 16-bit index buffers, `MultiDrawIndexedIndirect`; four reused 128K-triangle output buffers (3 MB) | G-buffer | 443,429 triangles at 1080p: cull 0.24 / 0.13 / 0.06 ms on Xbox One / PS4 / Fury X; draw falls 5.47 to 4.54, 4.56 to 3.76, 0.79 to 0.47 ms |
| id Tech 7, Doom Eternal (SIGGRAPH 2020) | No clusters; "geometry sets" of up to 256 meshes sharing a pipeline | Compute triangle culling (backface, frustum, micro-triangle, occlusion against an Umbra software depth mip chain) | One merged index buffer and one indexed indirect draw per geometry set; each 32-bit index packs vertex and instance IDs | Forward+ | About 70% of submitted triangles removed (budget 3 M after CPU culling, about 1 M reach the rasterizer); "up to 5 ms GPU savings in dense scenes"; hardware not stated |
| Activision (Drobot, REAC 2021) | 64-triangle clusters; work units are 64-bit masks of surfaces, clusters or triangles | Compute cluster and triangle culling; prefix-sum compaction; occlusion from a CPU-rasterized 64×64 depth buffer for the pre-pass | Index expansion pass feeding indirect draws; pre-pass uses one 32-bit payload per triangle (24-bit work-group ID, 8-bit triangle) | Hybrid Forward+ and visibility buffer, up to 64 bits per pixel plus 32-bit depth | Pre-pass 2.4 ms per-pixel versus 1.7 ms with 4×MSAA at half resolution; platform not stated |
| Remedy Northlight, Alan Wake 2 (REAC 2024, Digital Dragons 2024) | 64 vertices / 64 triangles, meshoptimizer; discrete Simplygon LODs | Two compute passes: geometry clusters (one thread each), then meshlets (group of 256 threads per cluster); frustum, contribution, cone, HZB; two-phase from a stored per-meshlet visibility bitfield (56.65 MB) | Mesh shaders, optional per-primitive culling; vertex shaders where mesh shaders are unsupported; `ExecuteIndirect` | Depth pre-pass and G-buffer | Xbox Series X Quality (HZB 2258×1270): culling cuts 173.2 M triangles and 42.4 ms to 10.6 M and 6.9 ms (table in the culling section) |
| Capcom RE Engine, Dragon's Dogma 2 and Monster Hunter Wilds (REAC 2025) | Up to 128 vertices / 128 triangles; authored LODs chosen per instance by a LOD factor; no automatic LOD | Instance then cluster culling on AABBs (frustum, zero-area, Hi-Z); two-phase; work distribution by wave subdivision, later a GPU ring buffer | Compute software rasterizer for small meshlets; mesh or vertex shaders for large ones; sorted "special rasterizers" for alpha test, decals, two-sided | 32-bit depth plus 32-bit ID (64-bit atomic or `R32G32Uint` target), resolved to a G-buffer in about 50 to 60 draws | PS5 1664p: visibility pass 1.705 ms (DD2), 3.127 ms (MHW); cluster culling 0.051 to 0.543 ms. PS5 development build: GPU 27.05 to 19.90 ms (DD2), 34.21 to 27.61 ms (MHW) |
| id Tech 8, Doom: The Dark Ages (GPC 2025) | Surfaces (mesh part plus material); GPU model gather and triangle culling carried over | Not detailed in the talk | Hardware raster writing `gl_PrimitiveID` in the depth pre-pass | 64-bit `RG32`: 32-bit triangle index, 21-bit surface index, winding and instancing flags | Xbox Series X 1440p "Siege": opaque 9.43 ms Forward+ to 7.0 ms deferred with variable-rate compute; RTX 5080 at 4K: 14.3 to 10.4 ms |
| Tencent NanoMesh (SIGGRAPH 2024) | Up to 128 triangles, Nanite-like hierarchy with stored merge error | GPU instance culling against HZB, then cluster frustum, occlusion, cone and LOD-error culling | Hardware only, by design: one draw per object class, IDs written from the vertex stage | 32-bit visibility buffer, 7 bits triangle | Immortalis-G715: instance cull 0.11, cluster cull 0.07, binning 0.2, raster 0.72, material classify 0.92, shading 0.87 ms; a five-year-old device totals about 20 ms. Resolution not stated; the profiled frame lists 116.7 K primitives (a separate demo scene has 80 M source triangles) |
| Saber, Space Marine 2 (REAC 2025) | No clusters; instances grouped into visibility and instance blocks | Compute culling of 100K+ boxes; HZB from reprojected depth plus about 1,000 manual occluders; results read back | CPU-issued instanced draws from a GPU-written parameter table | Not detailed | Culling about 0.5 ms; GPU instancing about 0.4 ms; "30-50% culled after frustum culling"; platform not stated on those slides |
| Unity 6 GPU Resident Drawer (manual, 6000.2 and 6000.3) | No clusters; Mesh LOD (6.2) stores discrete LODs in one index buffer | Per-instance bounding-sphere test against the current and previous frames' depth pyramids | `BatchRendererGroup` instancing, Forward+ only | Forward+ | None published |
| Ubisoft Anvil, AC Shadows (REAC 2025) | Three coexisting pipelines: BatchRenderer, GPU Instance Renderer, Micropolygon (virtualized geometry) | Not disclosed publicly | Not disclosed publicly | Not disclosed | Frame totals only: Xbox Series X 33.1 ms at 1620p Quality, 16.2 ms at 1080p Performance |
| Bevy 0.14 to 0.17 (author posts, release notes, source) | 64 then 128 triangles; cluster DAG | Flat per-cluster test through 0.16; 8-wide BVH with per-level dispatch in 0.17; two-phase throughout | 0.14: one non-indexed draw with a 4-byte ID per triangle. 0.15 on: compute raster for clusters under 64 px, one instanced draw for the rest | 64-bit atomic: 32-bit depth, 25-bit cluster, 7-bit triangle | RTX 3080, 2240×1260, 3,375 bunnies: visibility 4.97 ms (0.14) to 0.93 ms (0.15). RTX 4070: 115 billion instanced triangles in 3.5 ms, 900 billion in 4.5 ms (0.17) |
| NVIDIA `vk_lod_clusters` (README, read at `main`) | 128/128, meshoptimizer `clusterlod.h` | Node and group queues in compute; two-phase with reject lists | Mesh shaders; an untuned compute rasterizer option | Optional visibility buffer | Zorah, 1.64 billion unique triangles (18.9 billion instanced): 16 ms raster, 26 ms ray traced on a GeForce 3050 (zeux, 2025); resolution not stated |
| Wicked Engine (author post, 2024-12-10) | 124-triangle meshlets, trivial split | Meshlet culling in the amplification stage against previous-frame depth | Vertex shaders by default; optional mesh shaders | 32-bit: 25-bit meshlet, 7-bit primitive | Qualitative only |

Notes that change how the table should be read:

- **RE Engine is the closest shipped analogue to the M9 text.** It has no cluster hierarchy and no
  automatic LOD ("Automatic LODs difficult to implement with current resources"; seamless LOD is
  listed under next steps), yet ships meshlets, two-phase occlusion, a visibility buffer and a
  software rasterizer in two games. Cluster culling and cluster LOD are separable in practice.
- **Alan Wake 2 culls in compute, not in the mesh shader.** The mesh shader receives an already
  compacted meshlet list; its own culling is the optional per-primitive pass. The Digital Dragons
  deck lists a micropolygon software rasterizer and seamless meshlet LOD as future work.
- **id Tech 8 names software rasterization as out of scope and as future work** ("Software
  rasterization for sufficiently small triangles? -> Out of scope"; "Use software-rasterization for
  visibility pass" under future developments). Its measured win comes from moving shading off
  the rasterizer, not from changing how triangles are rasterized.
- **Space Marine 2 and HypeHype are not cluster evidence.** Saber's pipeline is instance-level and
  its authors call culling performance "still debatable" because large mesh chunks are not culled.
  HypeHype's SIGGRAPH 2023 deck describes a CPU draw-stream renderer (10,000 draws under 1 ms on
  an integrated AMD GPU) and says GPU-driven rendering still has unsolved performance problems on
  mainstream mobile GPUs because storage-buffer loads are slow there. No HypeHype cluster or
  meshlet path was found.
- **Anvil's Micropolygon system has no public technical description.** The REAC 2025 deck only
  classifies geometry across three pipelines and lists "increase MPH compatibility" and "sunset
  BatchRenderer" as open work. The GDC 2026 talk "Micropolygon Rendering in 'Anvil'" (Berenguier,
  Minnetian) covers rasterization, deferred materials and GPU-driven streaming but is members-only
  on GDC Vault. Secondary coverage saying it uses mesh shaders is **Reported** and
  **[UNVERIFIED]**.
- **Mecha BREAK (Amazing Seasun, GDC 2024, presented by Unity)** is confirmed as a session; its
  content was not obtainable. **Reported** by Chinese trade press: CPU culling, LOD and sorting
  moved to the GPU, frame rate up 43% and 31% on two test machines, GPU utilization from 70% to
  97%, mesh memory from about 1,100 MB to about 300 MB, scenes above a billion triangles. The
  coverage disagrees with itself on which GPUs were tested; treat all of it as **[UNVERIFIED]**.

## Hierarchy traversal: flat test versus BVH with work queues

### Why a flat test is correct for a cluster DAG

A cluster in a Nanite-style DAG is drawn when its own error is imperceptible and its parent
group's error is not. Both values are stored with the cluster, so the test is independent per
cluster and any subset of clusters can be tested in any order. Bevy 0.14 to 0.16 did exactly
this: one thread per cluster over every cluster of every instance, frustum test, then
`lod_is_ok && !parent_lod_is_ok` (jms55, 2024-06-09). A hierarchy is therefore an accelerator, not
a requirement. What it accelerates is the rejection of clusters that are too detailed, which
dominate: "for a given view, there are orders of magnitude more clusters that are too detailed
than there are clusters that are not detailed enough" (Karis et al., 2021).

### Expanding instances into cluster work

The flat test still needs every surviving instance turned into per-cluster work items, and
instances differ in cluster count by orders of magnitude. Five published strategies:

1. **Prefix sum and pair list.** Bevy rejected uploading 8 bytes (instance, cluster) per cluster
   from the CPU after estimating 122 MB and 7.63 ms of PCIe transfer per frame for 15.3 M
   clusters. Bevy 0.14 uploads a per-instance prefix sum of cluster counts instead, and a compute
   pass writes the pairs: one thread per cluster, binary search over the prefix sum (0.22 ms on an
   RTX 3080). At 33.5 M clusters the pass writes 268 MB per frame and is bandwidth-bound at
   0.40 ms.
2. **Chunk expansion.** Haar and Aaltonen expand each visible instance into chunks of up to 64
   clusters, then each chunk into clusters, so no thread loops over a long list.
3. **One work group per instance.** Alan Wake 2 dispatches 256 threads per geometry cluster, one
   meshlet per thread. Bevy 0.15 dispatches per instance and reserves output space with one atomic.
4. **Fixed subdivision or work stealing.** RE Engine first split each instance over N waves, then
   replaced that with a ring buffer driven by `InterlockedCompareExchange` and atomic min. On PS5
   at 1664p, phase-one cluster culling took 0.4 ms (ring buffer), 0.945 ms (16 waves) and 4.42 ms
   (128 waves) in a scene dominated by distant HLODs, and 0.241 / 0.197 / 0.456 ms in a foliage
   scene. No single fixed granularity fits both.
5. **Hierarchy with queues.** See below.

### Hierarchical traversal

Three variants exist, in increasing dependence on GPU scheduling behavior:

- **Per-level dispatch.** Keep two node queues. Level k reads one, tests each child bound for
  frustum, occlusion and "parent error still perceptible", appends surviving children to the other
  queue with an atomic counter, and writes the next level's indirect dispatch size. Leaves append
  to a cluster queue consumed by a final cluster-cull dispatch. Bevy 0.17 does this with an 8-wide
  BVH, eight threads per node (read from `cull_bvh.wesl` at `main`). It needs only indirect
  dispatch and 32-bit atomics. This is also Unreal's default: in
  `Engine/Source/Runtime/Renderer/Private/Nanite/NaniteCullRaster.cpp` (branches `5.8` and
  `ue5-main`, read 2026-10-01) the frame adds one node culling pass per hierarchy level followed
  by a cluster culling pass, and `r.Nanite.PersistentThreadsCulling` defaults to 0 on both
  branches; its help string warns that the persistent variant depends on GPU scheduling and
  advises against it outside fixed hardware (whether console configuration turns it on was not
  checked).
- **Persistent threads.** One dispatch; each thread loops popping nodes from a
  multi-producer multi-consumer queue until it is empty. Karis reports it "25% faster on average"
  than per-level dispatch (10 to 60%) and states the catch: it "relies on scheduling behavior"
  that the API does not define.
- **Explicit queues across kernels.** `vk_lod_clusters` runs an init kernel that seeds root nodes
  (or directly emits the coarsest cluster for distant instances), then a traversal kernel over
  node and group queues that repacks children across the SIMD group so lanes stay full.

### When flat stops being enough

Cluster counts follow from triangle counts. At 128 triangles per cluster and full clusters, a
mesh has T/128 leaf clusters and its DAG stores about twice the triangles; in clusters the factor
over T/128 is nearer 2 to 3, because level-0 clusters are rarely full. Bevy's 144,042-triangle
bunny at 64 triangles per meshlet has 4,936 meshlets in its LOD tree, 2.19 times the 2,251 leaf
meshlets that count would fill; the Zorah scene grows from 1.64 to 3.26 billion stored triangles
after DAG construction (zeux, 2026-09-30); and the
[construction notebook](cluster-lod-construction.md)'s measured default builds give 4,863 clusters
for Sponza (2.4 times 262,267/128) and 129,048 for San Miguel (2.9 times 5,608,441/128), with only
35.8% of San Miguel's level-0 clusters full.

Measured flat throughput: Bevy 0.14 tested 3,092 × 4,936 = 15.3 M clusters in 0.49 ms, and Bevy
0.15 tested 847 × 32,217 = 27.3 M clusters in 1.27 ms, both first-pass culling on an RTX 3080 at
2240×1260. That is 2 to 3 × 10¹⁰ cluster tests per second. No equivalent figure exists for an
Apple GPU.

| Content | Source triangles | Leaf clusters | DAG clusters | Flat test at 2 × 10¹⁰ per second | Pair list at 8 bytes |
|---|---|---|---|---|---|
| Sponza (measured build) | 262 K | 2.0 K if full | 4,863 | 0.24 µs | 39 KB |
| San Miguel (Luminex's realtime OBJ, measured build) | 5.61 M | 43.8 K if full | 129,048 | 6.5 µs | 1.0 MB |
| 100 M unique | 100 M | 781 K | 1.56 M | 78 µs | 12.5 MB |
| 300 M unique | 300 M | 2.34 M | 4.7 M | 0.23 ms | 37.5 MB |
| 1 billion instanced | 1 B | 7.8 M | 15.6 M | 0.78 ms | 125 MB |
| Zorah, instanced | 18.9 B | 148 M | 295 M | 15 ms | 2.4 GB |
| Bevy dragon field | 115 B | 0.9 B | 1.8 B | 90 ms | 14 GB |

Reading the table:

- At Luminex's current scale the flat test is free. Even if an M3 Max were ten times slower per
  test than an RTX 3080, San Miguel would cost about 65 µs. The rows below San Miguel assume
  full clusters and a factor of two, so they are lower bounds; the two measured scenes run 2.4
  to 2.9 times T/128.
- A few hundred million unique triangles still fit a flat test in well under a millisecond. What
  breaks first at that scale is memory and residency, not compute: a flat test needs every
  cluster's cull record resident, which is exactly what streaming removes.
- Flat stops being enough between roughly 10 and 30 million candidate cluster-instances per view,
  which is about 0.6 to 2 billion instanced source triangles inside the frustum. Bevy hit this
  wall from three sides at once at 27 to 33 million clusters: culling became the largest pass
  (1.27 ms against 0.34 ms of rasterization), the 25-bit cluster ID overflowed at 33.5 million,
  and the pair-list pass became bound by memory bandwidth.
- Each additional view multiplies the candidate count. Four cascades and a few local-light views
  move the threshold down by an order of magnitude unless views share one traversal.
- Unreal's default budgets are a useful size reference: 2 M traversed nodes, 16 M candidate
  clusters and 4 M visible clusters per culling pass (`NaniteShared.cpp`, branches `5.8` and
  `ue5-main`).

Bevy 0.17's result shows what the hierarchy buys: 130,000 dragons (115 billion triangles) render
in about 3.5 ms on an RTX 4070, and eight times as many instances cost 30% more. Nanite's 2021
frame statistics show the same funnel from the inside: 896,322 instances are cut to 3,668, the
hierarchy visits 39,274 nodes to produce 1,536,794 candidate clusters, and 191,514 of those are
visible, 96.5% of them software-rasterized. The second occlusion phase adds 7,906 clusters, 4% of
the 199,420 clusters and 25 million triangles rasterized in total.

## Two-phase occlusion

### Mechanism

Occlusion culling needs depth that does not exist until the scene is drawn. Two-phase culling
resolves this within one frame:

1. Cull everything against the previous frame's HZB and draw the survivors.
2. Build an HZB from the depth just drawn.
3. Re-test only what phase one rejected for occlusion, now against the new HZB with current
   transforms, and draw what passes.
4. Build the final HZB from complete depth for the next frame.

Two formulations of step 1 are in use. Haar and Aaltonen, and Alan Wake 2, keep a record of what
was visible last frame (Alan Wake 2 stores a bit per meshlet per visibility set) and draw that
first. Nanite cannot, because the LOD cut and streaming change which clusters exist from frame to
frame, so it asks instead whether the currently selected cluster would have been visible last
frame: it tests the cluster's bounds against the previous HZB using the previous transforms.
`vk_lod_clusters` documents the efficient form of step 3: because the view and error threshold
are identical in both phases, every LOD decision repeats exactly, so phase one records "wanted by
LOD, rejected by occlusion" as lists (instances, inner nodes, groups, and a bitmask of rejected
clusters per group) and phase two only repeats the visibility test over those lists. Entries are
recorded only if they pass the current frustum, otherwise nearly every off-screen instance would
be recorded. The README states that almost all the benefit comes from phase two no longer
iterating every instance.

### Measured cost

| Source | Hardware, resolution, scene | Phase one | Phase two and extra HZB | Share |
|---|---|---|---|---|
| Haar and Aaltonen 2015 | Xbox One, 1080p, 250,000 moving objects | Object cull 0.28, cluster cull 0.09, draw 1.60 ms | Object cull 0.26, cluster cull 0.04, draw under 0.01, pyramid 0.06 ms | 0.36 of 2.3 ms |
| Karis et al. 2021 | UE5 reveal-demo content, about 2496×1404; hardware not stated on the slide | Cluster cull 406 µs, raster 1148 µs | Build HZB 99, instance cull 125, cluster cull 102, raster 183 µs | About 0.5 of 2.5 ms |
| Bevy 0.14 | RTX 3080, 2240×1260, 3,092 bunnies | Cull 0.49, raster 1.85 ms | Pyramid 0.03, cull 0.11, raster under 0.01 ms | 0.14 of 2.78 ms |
| Bevy 0.15 | RTX 3080, 2240×1260, 3,375 bunnies | Cull 0.19, raster 0.42 ms | Pyramid 0.03, cull 0.06, raster under 0.01 ms | 0.09 of 0.93 ms |
| RE Engine shadows | PS5, 3 cascades plus 1 spot | Reprojected-depth occluders: 10.65 M triangles, 3.19 ms | With two-phase: 3.30 M triangles, 2.28 ms | Net saving |

The Nanite slide prints the main-pass instance cull as "108ms"; from the 2.5 ms total it is
evidently 108 µs. The second phase costs 5 to 20% of the visibility pass. Without reject lists it
is dominated by re-culling instances; the second draw is nearly empty when frames are coherent.

### Granularity

Every surveyed system that occludes clusters against the previous frame's depth applies
two-phase at both levels: instances first (cheap, removes whole work items), then clusters or
nodes. Systems with a depth source that exists before the main pass cull in one phase against it
at whatever granularity they have: an occluder pre-pass with reprojected previous depth (ACU: 300
best occluders in about 0.6 ms on PS4), a CPU-rasterized occluder buffer loaded into the Hi-Z
(Frostbite, at cluster and triangle level; Activision's pre-pass at cluster level; id Tech 7 at
triangle level), reprojected depth plus manual occluders (Space Marine 2), or current and
previous depth pyramids together (Unity 6, instances only). NanoMesh's deck lists cluster
occlusion culling without saying which frame's HZB it tests or whether a second phase exists.

### What Luminex must change, and whether clusters force it

[M7.4](../../milestones/m7/m7.4.md) projects each instance's current world box with the previous
frame's view-projection and tests it against the previous frame's pyramid, invalidating all
history whenever any occluder moves; that is Nanite's phase-one formulation apart from the
per-instance previous transform. Moving to two phases needs:

- a reject list written by classification for candidates that failed only the occlusion test and
  pass the current frustum;
- an HZB build between two draws of the same frame (the M7.4 validation record measures HZB plus
  publication at 0.05 to 0.37 ms at 1280×720 on the development machine);
- a second classification dispatch over the reject list against the new pyramid with current
  transforms, and a second draw that loads depth instead of clearing it;
- the same oracle that exists today, extended to assert that no visible identity is missing.

The Apple-specific cost is the split render pass. Luminex's scene pass writes color, depth,
motion and reactive together, so a forward-shaded second phase stores and reloads four
attachments. A depth-only or visibility-buffer first phase stores and reloads one or two. This
cost is unmeasured and belongs in the published table.

Cluster culling by itself does not force two-phase: frustum, LOD and cone tests have no temporal
input, and instance-level previous-frame occlusion can keep its accepted one-frame latency while
clusters are culled without occlusion. Per-cluster occlusion does force it in practice. With
instances, a disocclusion drops a whole object for one frame, and M7.4 measured a maximum missing
streak of one. With clusters, every camera move disoccludes clusters along silhouettes, so holes
open inside otherwise continuous surfaces on most frames and temporal history is rejected there.
The evidence is consistent: Haar and Aaltonen, Nanite, Alan Wake 2, RE Engine, Bevy and
`vk_lod_clusters` all pair previous-frame cluster occlusion with a second phase, and open
projects that run it single-phase describe that as a defect (`vk_lod_clusters`, whose default mode is single-pass:
"can cause artifacts on faster motion"; Wicked Engine: "there was still some lag"; the
nanite-webgpu port: "No reprojection and no two-pass"). Shadow views raise the same issue
earlier, because a shadow view has no previous-frame pyramid unless one is cached per view.
Capcom reused the previous shadow map as an occluder, saw artifacts, and moved directional
shadows to two-phase.

## Raster back ends

### The small-triangle problem

Two separate hardware limits make small triangles expensive, and a visibility buffer removes only
one of them.

- **Setup rate.** A hardware rasterizer is parallel across pixels, not across triangles. Wihlidal
  gives the AMD GCN figures: each rasterizer consumes one triangle per clock and emits up to 16
  pixels per clock; consoles of that generation have two, desktop parts four. A triangle covering
  one pixel uses 6.25% of that capacity. Karis gives "4 tris/clock max" for modern GPUs and notes
  that emitting a primitive ID makes it worse.
- **Quad shading.** Fragments are shaded in 2×2 quads so derivatives exist. id Software's example
  triangle runs 14 active and 18 helper threads (43% utilization); a small one runs 2 of 8 (25%).
  Activision reports Forward+ quad occupancy as low as 20% in foliage and finds Forward+ unbeatable
  only while occupancy stays above 75%. Fatahalian et al. (SIGGRAPH 2010) measure an eightfold
  reduction in shading work from merging quad fragments when triangles are pixel-sized.

A visibility buffer makes the per-quad shader trivial, which addresses quad shading. Only a
rasterizer that is parallel across triangles addresses the setup rate: one compute thread per
triangle, a bounding-box or scanline loop of a few arithmetic operations per covered pixel, and a
64-bit atomic max that packs depth above the payload so the atomic is the depth test.

Tile-based GPUs add a third cost. Geometry that survives clipping is binned into per-tile lists
before any fragment work, so the geometry stage is paid in full for triangles that hidden-surface
removal later discards (a general property of tile-based deferred rendering, stated here without
an Apple source). Arm states that tile-based GPUs "have a relatively low vertex throughput"
(2026-03-18), and Tellusim attributes poor multi-draw results on mobile to tile-based rendering.
No Apple-authored figure for this was found.

### Evidence by back end

**(a) One indexed indirect draw per visible cluster.** On APIs with multi-draw-indirect this is
viable on some vendors only. Tellusim (2021, 40 M triangles in 64/126 meshlets, no culling)
measures NVIDIA at parity with a single draw (11.2 against 11.8 billion triangles per second on
a 2080 Ti) but a Radeon 6700 XT at 3.6 against 14.7 billion. Bevy 0.14 tried one sub-draw per
cluster and found the GPU's command processor became the bottleneck. Wihlidal reports that runs
of empty draws can cost 1.5 ms of a 60 Hz frame in shipped games, which is why his pipeline
compacts them. On Metal,
Tellusim's Apple M1 numbers are the only public ones found, and they depend strongly on cluster
size: a CPU loop of indirect draws reaches 740 M and an indirect command buffer 840 M triangles
per second at 64/126, against 1.36 billion for one ordinary indexed draw, but the loop reaches
1.22 B at 96/169 and 1.49 B at 128/212 (the command buffer 933 M and 1.05 B), above the single
draw, and the command buffer falls behind the loop above about 200 triangles per draw. No M1
figure exists for the compaction or instancing forms below; Tellusim's emulation table covers a
Radeon Vega 56 under Metal only. Luminex's own record is that Metal needs one CPU-issued command
per candidate slot without an indirect command buffer ([M7.3](../../milestones/m7/m7.3.md)) and
that the indirect command buffer spike passed runtime execution, texture sampling and 900-frame
reliability but failed its labelled-capture gate, so no adapter was authorized
([spike record](../2026-09-15-m7.3-icb-runtime-spike.md)). Per-cluster draws multiply slot count
by one to two orders of magnitude.

**(b) Compaction into one buffer and a single draw.** Three shipped variants:

- *Compacted index buffer.* A compute pass writes surviving triangles' indices contiguously; one
  indexed indirect draw consumes them. Frostbite, id Tech 7 and Activision ship this. Indices are
  32-bit values that pack a cluster or instance ID with a local vertex index, and the vertex
  shader pulls its own data. Tellusim's emulation (cluster index in the upper bits, vertex in the
  low 8) costs 11 MB written and read per million triangles and beats both multi-draw and mesh
  shaders on AMD (7.7 against 4.1 and 4.6 billion triangles per second on a 6700 XT) and on
  Qualcomm, and reaches 2.7 billion against 0.86 billion for per-meshlet draws on a Radeon Vega
  56 under Metal. Per-triangle culling is free to add because the pass already touches every
  triangle. The buffer must be sized for the worst case; Frostbite bounds it with four reused
  128K-triangle buffers and mid-frame flushes.
- *One ID per triangle, non-indexed.* Activision's pre-pass writes one 32-bit value per triangle
  and decodes three vertices from it. Bevy 0.14 did the same: its author recalls the indexed
  form as 10 to 20% faster but rejected its worst-case allocation of 12 bytes per triangle in
  favor of 4.
- *Fixed-topology instancing.* Draw `3 × maxTriangles` vertices with instance count equal to the
  visible cluster count; the vertex shader maps instance to cluster and emits a degenerate vertex
  past the cluster's real triangle count. RedLynx's 2015 path and Bevy's current hardware path
  work this way. It needs no compaction pass and no per-frame index memory, but its cost follows
  cluster fill. Bevy 0.14 tried it and found it "performed very poorly" because dummy triangles
  occupy the fixed-function front end; Bevy adopted it in 0.15 only after the builder raised the
  worst per-level share of full clusters from 20% to 76% and small clusters moved to the software
  path.

All three need exactly one indirect draw per pipeline state, which is what Metal provides.

**(c) Mesh shaders.** Positive: Alan Wake 2 on an RTX 4090 drops from 4.1 to 2.7 ms across depth,
G-buffer and shadow passes against the vertex path. NVIDIA's CAD sample reports 2.19 to 1.20 ms
on an RTX 6000 (32 M triangles, 2048²) and 2.90 to 1.63 ms, or 1.00 ms with fragment
barycentrics, on an RTX 3080. RE Engine's hardware path costs 139 to 158 µs on an RTX 4090 and
its table shows why cluster size matters more than the mesh shader: going from 128/128 to 64/64
raises cluster culling from 168 to 300 µs and the whole visibility pass from 882 to 1002 µs.
Negative or neutral: Wicked Engine found mesh shaders "vastly" faster in a replicated-bunny test
but "in real scenes, the performance was always worse than the vertex-shader based rendering",
partly because they starve async compute. Tellusim finds "any Mesh shader configuration is
decreasing the hardware capabilities 3 times" on a 6700 XT. Kristóf explains the AMD mechanism:
each thread can emit one vertex and one primitive, so the hardware group is as large as the
output limits, and task shaders run as compute on a separate queue with a ring buffer. Anagnostou
(RTX 3080 mobile, 1080p, San Miguel) measures 2.75 to 2.83 ms for mesh shaders without culling
against 2.85 ms for vertex shaders, and 1.64 ms once frustum and occlusion culling are added;
a pass-through mesh shader was slightly slower than vertex shaders in depth-only passes. The
gain is the culling, not the stage. On Apple hardware the only data point is a December 2022
developer forum thread in which a ported mesh-shader renderer ran twice as slow as draws on an
M1 (and 1.5 times faster than draws on an RTX 3070), and an Apple engineer replied that this was
probably expected on M1 and that draws should be used where they are faster.

**(d) Compute software rasterizer.**

| Source | Hardware | Workload | Result |
|---|---|---|---|
| Karis et al. 2021 | Not stated per number | UE5 demo content | "3x faster than hardware on average" against the fastest primitive-shader path; clusters with edges under 32 px go to software |
| Capcom 2025 | "older hardware" | Not stated | 2.4 times faster; scanline variant rejected for artifacts; passes the DirectX rasterization-rule tests |
| Tellusim 2021 | GeForce 2080 Ti | 498,990 meshlets of 64/128, depth only | 17.3 against 12.1 billion triangles per second (1.4×) |
| Tellusim 2021 | Radeon 6700 XT | Same | 16.7 against 14.7 billion (1.1×) |
| Tellusim 2021 | Apple M1, Metal | Same | 2.30 against 1.37 billion (1.7×) |
| Tellusim 2021 | Apple A14, Metal | Same | 1.02 billion against 666 M (1.5×) |
| Tellusim 2021 | Intel UHD; Adreno 660 | Same | 0.82×; 0.88× (hardware wins) |
| Bevy 0.15 | RTX 3080, 2240×1260 | 3,375 bunnies | Raster 3.44 ms (0.14, hardware only) to 0.42 ms (software) plus under 0.01 ms (hardware); confounded by a rebuilt DAG and a weak hardware path |
| `vk_lod_clusters` | NVIDIA | Default scenes | "not faster than the mesh-shader because clusters tend to have larger than single pixel triangles"; untuned |
| Schütz et al. 2026 (CuRast) | CUDA | Hundreds of millions of triangles | 2 to 5× over Vulkan for unique geometry, up to 12× instanced; Vulkan faster for low-polygon meshes |
| Laine and Karras 2011 | Then-current GPU | General-purpose pipeline with API ordering | 2 to 8× slower than hardware |

Software rasterization wins when triangles approach pixel size, depth-only or ID-only output
suffices, and 64-bit atomics are available. It loses or is unavailable for large triangles (Karis
found "no middle ground": where work distribution would help, hardware is faster yet), clusters
that cross the near plane (Bevy routes those to hardware), alpha-tested and deforming materials
(RE Engine keeps separate sorted hardware rasterizers), and platforms without 64-bit atomics.
NanoMesh avoided it on mobile "due to extra scene depth passes, lack of atomic64 support, and
higher bandwidth from 64-bit visbuffers". A WebGPU port that packed 16-bit depth into 32 bits
reports z-fighting and leaks. Tellusim's Metal runs were depth-only for the same reason. The
dependency runs the other way too: if the LOD target is coarser than about one pixel per triangle,
there is little for a software rasterizer to win, which is the `vk_lod_clusters` observation.
Bevy's author, after building one, advises others "to skip software raster until close to the
end" and relays from other projects that it is "only a 10-20% performance improvement over mesh
shaders in most scenes" unless tiny triangles dominate (second-hand, **Reported**).

### What depends on Metal's limits

These decide the back-end choice and belong to the
[Metal constraints notebook](apple-metal-geometry-constraints.md): absence of
multi-draw-indirect with a GPU count; indirect command buffer length (Tellusim reports 16,384
commands in 2021) and GPU encoding; 64-bit atomics and atomics on textures in MSL through Slang;
wave or subgroup intrinsics, which RE Engine, Frostbite and id Tech rely on for compaction;
mesh-shader output limits and whether pre-M3 hardware emulates the stage; scheduling fairness for
persistent threads; and layered or amplified rendering for cascades.

## Triangle-level and cone culling

Alan Wake 2 gives the cleanest cumulative table (Xbox Series X Quality; view triangles, shadow
triangles, then depth pre-pass, G-buffer and shadow-map time):

| Stage added | View | Shadow | Depth | G-buffer | Shadows |
|---|---|---|---|---|---|
| None | 49.9 M | 123.3 M | 6.64 ms | 10.19 ms | 25.60 ms |
| Frustum | 14.3 M | 10.5 M | 3.33 | 3.71 | 3.55 |
| Contribution (shadows only) | 14.3 M | 9.0 M | 3.33 | 3.71 | 3.37 |
| Normal cone | 13.9 M | 8.9 M | 3.17 | 3.69 | 3.06 |
| Occlusion | 7.3 M | 5.0 M | 2.56 | 2.56 | 2.60 |
| Per-triangle, in the mesh shader | 6.8 M | 3.8 M | 2.24 | 2.56 | 2.13 |

- **Cone culling is cheap and its value depends on content.** It removed 2% of triangles there,
  and Remedy concluded it "wasn't worth it in the end for us". zeux measures the range: a single
  cone rejects at most about 25% on dense smooth meshes and 4 to 8% on architectural scenes
  (Bistro interior 4.2%, Sponza 7.7%), and efficiency falls as clusters grow or LOD coarsens (14%
  at 64 triangles and 3% at 256 with LOD enabled). In niagara's dense kitten scene on an RTX
  4070 Ti with 64-triangle meshlets, the frame takes 8.38 ms without backface culling, 7.25 ms
  with cones (13% of triangles rejected) and 6.96 ms with cones plus brute-force per-triangle
  culling (53%). It costs four bytes per cluster and one dot product, so it is cheap to keep and
  cheap to drop.
- **Per-triangle culling depends on the vendor and the back end.** In a compute pass it removed
  78% of 443,429 triangles for Frostbite (orientation 46%, then depth 20%, small-primitive 8%,
  frustum 4% incrementally) and saved 15 to 30% of draw time; id Tech 7 removes about 70%. In
  Activision's examples cluster culling takes 28 K triangles to 24 K and triangle culling to
  14 K. In a mesh shader, Alan Wake 2 saved 0.8 ms on Series X and about 1.67 ms on Series S, but
  performance degraded on RTX 3090 and 4090, was unchanged on an RX 7900 XT and improved by
  1.8 ms on an RX 6700 XT, so it shipped on console only. RE Engine's deck and Q&A describe no
  per-triangle pass; whether it was tested there is **[UNVERIFIED]**.
  Nanite does none: the software rasterizer rejects backfaces per thread and two-phase cluster
  occlusion replaces per-triangle occlusion.
- **Haar and Aaltonen's result is a caution.** In AC Unity, backface and cluster-bounds culling
  removed 20 to 40% of triangles for "only small overall gain: <10% of geometry rendering", while
  30 to 80% of shadow triangles were culled. The value is concentrated in depth-only passes.
- **When it is not worth it.** Small-primitive culling is redundant once LOD selection keeps
  triangles near one pixel. Cluster culling is pointless where cluster bounds are not much smaller
  than instance bounds; Capcom now bypasses it for such foliage by comparing the two boxes.
  Per-triangle culling pays in a compaction back end, where the pass already exists, and in
  shadow views.

## Multiple views

Shadow views use the same culling and raster with three differences: depth-only output, a
different error scale, and usually no previous-frame occlusion data.

- **Nanite** passes an array of views to one chain of dispatches and tags work with a view ID
  instead of running the pipeline once per shadow map, reporting "a 100x speedup" in extreme
  cases, and biases shadow LOD to two pixels of error.
- **RE Engine** culls shadow casters in one phase with frustum and zero-area tests. Directional
  cascades add occluders made by reprojecting camera depth into a 64×64 map with mips per cascade
  (the 2015 Ubisoft technique), then the previous shadow map and injected ray-hit positions, then
  two-phase. Three cascades on PS5: 13.54 M triangles and 6.71 ms with frustum culling only,
  7.93 M and 4.83 ms with reprojected occluders. The software rasterizer writes shadow depth with
  32-bit atomics.
- **Alan Wake 2** uses one frustum routine with a variable plane count for cameras, spot lights,
  point lights and cascades, fits cascade frusta to the camera, and applies screen-size
  contribution culling only in shadow passes.
- **Activision** keeps visibility as bitmasks so the union or intersection of several views'
  visible triangles is a bitwise operation. **Space Marine 2** supports up to 64 cameras in one
  instancing pass. **NanoMesh** renders shadows from coarser clusters.

What must be designed in from the first cluster stage: a view table whose entries carry planes
(variable count), current and previous view-projection, an error scale that is correct for
orthographic projection, an optional pyramid, and an output range; visible-cluster records that
name their view; a depth-only variant of every raster path including the alpha-masked one; and
per-view capacity accounting. Luminex's M7.3 view record (plane set, candidate range, output
range) already has this shape at instance level.

## Visibility output formats

| System | Bits | Packing | Limit that results |
|---|---|---|---|
| Nanite (2021 slides) | 64, atomic | 30 depth, 27 visible-cluster index, 7 triangle | Format allows 134 M visible clusters; default budget is 4 M |
| RE Engine | 64, atomic or `R32G32Uint` | 32 depth; 1-bit signature, 24-bit index into the culled-meshlet list, 7 triangle. The list entry is 64 bits: 24-bit instance, 8-bit LOD transition, 32-bit meshlet offset. The Q&A describes the ID as 25-bit cluster and 7-bit triangle | 16.7 M visible meshlets, 16.7 M instances |
| Bevy 0.15 | 64, atomic | 32 depth, 25 scene-wide cluster, 7 triangle | Failed at 33.5 M scene clusters (1,042 instances of one mesh) |
| Bevy 0.17 | 64, atomic | 32 depth, slot in the per-frame raster list, 7 triangle | Limit moves to visible clusters |
| id Tech 8 | 64, render target | 32 triangle index within the mesh, 21 surface or instance, 2 flags; hardware depth separate | 2 M surfaces; no cluster limit |
| Activision | Up to 64 | 24-bit work group, 8-bit triangle on the triangle-ID path | 16.7 M work groups |
| Wicked Engine | 32, render target | 25 scene-wide meshlet (plus one, zero means empty), 7 primitive | 33.5 M meshlets in the scene |
| NanoMesh | 32, render target | Cluster, 7 triangle | 33.5 M clusters |
| Bevy 0.14 | 32, render target | 26 cluster, 6 triangle | 64-triangle meshlets |

Three rules follow. Seven triangle bits fix clusters at 128 triangles or fewer. The cluster field
should index the per-frame visible-cluster list, not a scene-wide ID; both Bevy and RE Engine
arrived there, and it also lets the list entry carry the instance. Thirty-two bits with a
hardware depth buffer are enough for hardware raster; the 64-bit form exists so that an atomic
max can be the depth test, and it costs bandwidth that id Software measured as free at 64 bits
per pixel but not at 128.

## Corrections to earlier research

- `production-engines.md`, RE Engine section: the 1.578 ms to 1.159 ms improvement is the
  visibility-buffer pass, not instance culling (cluster culling 0.543 to 0.051 ms is correct).
  The 256-byte structure is the mesh header; the 32-byte one is the per-meshlet header. The
  visibility ID is 32 bits (signature, 24-bit culled-meshlet index, 7-bit triangle) beside 32-bit
  depth. The deck is titled "RE ENGINE Meshlet Rendering Pipeline".
- `2026-09-14-rendering-direction-review.md`, section 1.1, lists Space Marine 2 as cluster or
  meshlet geometry shipped beyond Epic. Its REAC 2025 deck describes instance-level GPU culling
  with no clusters.
- The same review's Doom: The Dark Ages slide link now returns 404; the deck moved from
  `/public/2025/slides/` to `/public/2025/talks/` on the conference's static host.
- `pipeline-state-of-the-art-m7-m11.md`, section 6: the meshoptimizer dates are two years early.
  Releases are v0.24 2025-06-12, v0.25 2025-08-20, v1.0 2025-12-08, v1.1 2026-04-02, v1.2
  2026-06-30 and v1.3 2026-09-25 (GitHub releases API). The current release is 1.3.
- The same file's section 1 calls Bevy's BVH culling an unfinished multi-release struggle. It
  merged on 2025-06-29 (pull request 19318) and shipped in 0.17 on 2025-09-30 with measurements.
- The same file marks Unity's GPU Resident Drawer and "Nanite: A Deep Dive" unverified. Both were
  fetched for this notebook. Unity's occlusion test uses current and previous depth at instance
  granularity; Mesh LOD is discrete LODs in one index buffer, not cluster LOD.
- `studio-and-engine-disclosures-2023-2026.md` lists Mecha BREAK's developer as to be determined.
  It is Amazing Seasun Games, in a Unity-presented session, speakers Huang Jinshou and Mintao
  Huang. The same file describes Northlight as mesh-shader-driven culling; culling runs in
  compute and the mesh shader rasterizes.
- Nanite's persistent-thread traversal, often cited as the design, is off by default in current
  Unreal source (`r.Nanite.PersistentThreadsCulling` is 0 on `5.8` and `ue5-main`); whether
  console configuration enables it was not checked.

## Implications for Luminex

**The M9 phrase "ordinary indirect cluster raster" needs a definition.** One indirect draw per
visible cluster has the weakest case on Metal: it needs a CPU-issued command per cluster slot or
an indirect command buffer whose spike passed at runtime but failed its capture gate, and the
only public Apple measurement puts it at 54 to 62% of single-draw throughput at 64/126 yet at or
above parity at 128/212 on an M1, with no M1 comparison against the single-draw forms at all.
The case against it therefore rests on command cost and tooling, not on a measured throughput
deficit. The two single-draw forms,
fixed-topology instancing and a compacted index or triangle-ID buffer, need only what the RHI has
today: compute with storage buffers, 32-bit atomics or a prefix sum, and one indirect draw per
pipeline state. Both are shipped designs. Mesh shaders are a third candidate with mixed evidence
and one negative Apple data point; they should be adopted only on a paired measurement.

**Flat cluster culling is sufficient for every scene Luminex has today.** A hierarchy
is justified by instanced scenes above roughly ten million candidate cluster-instances per view,
by streaming, or by many views. When it is needed, per-level dispatch is the portable form and is
what Unreal and Bevy run by default. Luminex's existing reset, classify, scan and emit kernels
map directly onto instance classification, a prefix sum over cluster counts, and cluster
classification.

**A staged path, each stage with a gate:**

| Stage | Adds | Gate |
|---|---|---|
| 1. Cluster cull and single-draw raster | Clusters with bounds over existing discrete meshes; flat frustum test (cone optional); both single-draw back ends; view table | Visible-cluster list equals a CPU oracle; image exact against the instance path; triangles submitted and GPU time reported against the M7 indirect path on San Miguel and a dense lab scene |
| 2. Cluster LOD selection | DAG clusters from the offline builder; per-cluster error test; a pixel-error control | No cracks under an ID or edge oracle; rendered triangle count stays within a stated factor as instance count scales; image error against the full-detail reference inside a declared tolerance |
| 3. Depth or visibility pre-pass with two-phase occlusion | Reject lists at instance and cluster level; mid-frame pyramid; second draw | Zero missing identities on the M7.4 motion rails where single-phase shows streaks of one; second-phase and render-pass-split cost reported |
| 4. Mesh-shader execution (M3 and later) | Object and mesh stages over the same visible list | Same visible set and image; adopt only if the paired GPU interval excludes zero in its favor |
| 5. Hierarchical traversal | Per-level node culling; per-frame budgets with overflow reporting | Eight times the instances costs under 1.5 times the culling time; flat path retained as oracle |
| 6. Compute raster for small clusters | Triangle-parallel rasterizer with atomic depth; per-cluster path choice by projected edge length | Coverage identical to hardware on a rule-conformance set; no cracks at mixed boundaries; a stated speedup on sub-threshold clusters, or a recorded negative result |
| 7. Shadow views through the same path | Multiple views per traversal; depth-only raster; orthographic error | Shadow-view images exact against the instance path (cascades only once M8 adds them); cost per added view reported |

Each of the seven stages can be accepted on its own. Stage 1 depends on nothing new. Stage 2
depends on the builder ([construction notebook](cluster-lod-construction.md)). Stage 3 should be
cheaper on a tile-based GPU once a depth or visibility pre-pass exists
([surface paths notebook](visibility-buffer-and-surface-paths.md)), by the attachment-count
argument above; that cost is unmeasured, so placing it after the opaque-path decision is an
inference to confirm with the split-pass measurement, not a settled dependency. Stage 6 depends
on a visibility buffer and
on atomics that the Metal notebook must confirm. Stages 5 and 6, plus streaming, are what
separate a cluster renderer from a Nanite-class one; RE Engine shows that stages 1, 3 and 6 ship
without stage 2, and Alan Wake 2 that stages 1, 3 and 4 ship without 2, 5 or 6.

Per-cluster occlusion should not ship single-phase. If stage 3 slips, keep occlusion at instance
level with the latency M7.4 already accepts and cull clusters by frustum and LOD only.

**What this notebook's author would change in the M9 text.** Replace "ordinary indirect cluster
raster" with a single-draw cluster back end chosen by measurement between the two forms above.
Make two-phase occlusion its own slice, placed after the opaque-path decision so that it runs on
a depth or visibility pre-pass, unless the split-pass measurement on forward shading shows the
penalty is small. Keep hierarchical traversal and software rasterization deferred,
as M9 already does, but state their triggers: candidate counts above about ten million per view
for the hierarchy, and a pixel-scale LOD target with confirmed 64-bit atomics for the
rasterizer. Keep mesh shaders optional. The evidence does not support promising a speedup from
them on Apple hardware.

**Numbers worth publishing.** Public native-Metal figures consist of one 2021 vendor blog on an
M1 and one forum thread. A Luminex table on the frozen M3 Max would be new for each of these:

- cluster tests per second for the flat kernel, and the fixed cost of one indirect compute
  dispatch with its barriers (the per-level tax of a hierarchy);
- triangles per second by back end (ordinary indexed draw, per-cluster draws, compacted indices,
  fixed-topology instancing, mesh shader, compute raster) at 64/64, 64/126 and 128/128, in the
  form of Tellusim's table;
- the same by projected triangle size, to locate the software and hardware crossover on a
  tile-based GPU, with any available tiler or parameter-buffer counters;
- a cumulative culling table in Alan Wake 2's form (triangles and milliseconds after each test)
  for view and shadow passes;
- two-phase cost split into second classification, mid-frame pyramid, second draw and the
  load/store penalty of the split pass, for forward and for a depth-only pre-pass;
- mesh-shader against vertex-path paired deltas, published even if negative;
- 32-bit against 64-bit visibility output cost, and memory per cluster for cull records and work
  lists.

## Open questions and what could not be confirmed

- No Apple-GPU measurement of cluster culling throughput, two-phase cost, mesh-shader cluster
  raster on M3-class hardware or compute raster with a payload was found. Tellusim's M1 figures
  are from 2021, use unoptimized shaders, state no resolution, and are depth-only for the compute
  path.
- Anvil's Micropolygon runtime (cluster size, hierarchy, raster path, output format) is not
  public. The GDC 2025 "Rendering Assassin's Creed Shadows" slides and the GPU Zen 3 chapter on
  AC Mirage were not obtained.
- Mecha BREAK's technical content is known only through secondary reports.
- RE Engine's decks do not give cluster counts for the timed scenes, so its culling times cannot
  be converted to a per-cluster rate. Its cascade-2 table lists time spent in the software and
  hardware paths on different clusters; it is not a head-to-head comparison.
- id Tech 8's talk gives no culling or raster timings, only shading.
- Bevy's release notes say the lifted limit was 2²⁴ clusters; the author's 0.15 post and the
  shader packing say 2²⁵. The shader is taken as authoritative here.
- Whether persistent-thread traversal is safe on Apple GPUs, whether Slang's Metal target exposes
  64-bit atomics and wave intrinsics, and what a mid-frame render-pass split costs on Apple
  hardware are open and belong to local probes; stage 3's placement after the opaque-path
  decision rests on that unmeasured split cost.
- Tellusim's per-cluster-draw figures on the M1 move from 54% of single-draw throughput at 64/126
  to 110% at 128/212, and no public Apple measurement compares per-cluster draws with compaction
  or fixed-topology instancing, so the ranking of raster back ends on Apple GPUs is open until
  measured.
- NanoMesh's deck does not say whether its cluster occlusion test uses the previous frame's HZB
  or has a second phase, and no statement that RE Engine tested per-triangle culling was found in
  its deck or Q&A.
- The 2 × 10¹⁰ tests-per-second anchor is derived from Bevy pass timings that include some
  early-out threads; it is an order of magnitude, not a benchmark.

## Sources

Decks and papers (downloaded and read as text):

- Haar, Aaltonen, "GPU-Driven Rendering Pipelines", SIGGRAPH 2015: https://advances.realtimerendering.com/s2015/aaltonenhaar_siggraph2015_combined_final_footer_220dpi.pdf
- Wihlidal, "Optimizing the Graphics Pipeline with Compute", GDC 2016 (Internet Archive text): https://archive.org/details/GDC2016Wihlidal
- Geffroy, Gneiting, Wang, "Rendering the Hellscape of Doom Eternal", SIGGRAPH 2020: https://advances.realtimerendering.com/s2020/RenderingDoomEternal.pdf
- Karis, Stubbe, Wihlidal, "Nanite: A Deep Dive", SIGGRAPH 2021: https://advances.realtimerendering.com/s2021/Karis_Nanite_SIGGRAPH_Advances_2021_final.pdf
- Drobot, "Geometry Rendering Pipeline Architecture", REAC 2021: https://www.enginearchitecture.org/downloads/reac2021_geometry_pipeline_rendering_architecture.pptx
- Aaltonen, "HypeHype Mobile Rendering Architecture", SIGGRAPH 2023: https://advances.realtimerendering.com/s2023/AaltonenHypeHypeAdvances2023.pdf
- Aalto, "Modernizing geometry rendering in Alan Wake 2", REAC 2024: https://www.enginearchitecture.org/downloads/REAC_2024_Remedy.pdf
- Jansson, "GPU-driven rendering with mesh shaders in Alan Wake 2", Digital Dragons 2024: https://presentations.remedy.fi/gpu-driven-rendering-with-mesh-shaders-in-alan-wake-2-dd2024.ppsx
- Cao, "Seamless rendering on mobile" (NanoMesh), SIGGRAPH 2024: https://www.advances.realtimerendering.com/s2024/content/Cao-NanoMesh/AdavanceRealtimeRendering_NanoMesh0810.pdf
- Mishima, "RE ENGINE Meshlet Rendering Pipeline", REAC 2025, and its Q&A: https://www.enginearchitecture.org/downloads/REAC_2025_Capcom.pdf , https://www.enginearchitecture.org/downloads/REAC_2025_Capcom_QA.pdf
- Lopez, Bouchard, "Anvil Rendering Architecture", REAC 2025: https://www.enginearchitecture.org/downloads/REAC_2025_Anvil.pdf
- Bukhalov, Orachev, "Geometry rendering and shaders infrastructure in Warhammer 40000: Space Marine 2", REAC 2025: https://www.enginearchitecture.org/downloads/REAC_2025_Saber.pdf
- Lazarek, Hammer, "Visibility Buffer and Deferred Rendering in DOOM: The Dark Ages", Graphics Programming Conference 2025: https://static.graphicsprogrammingconference.com/public/2025/talks/visibility-buffer-and-deferred-rendering-in-doom-the-dark-ages/Lazarek-Hammer-visibility-buffer-and-deferred-rendering-in-doom-the-dark-ages.pdf
- Fatahalian et al., "Reducing Shading on GPUs using Quad-Fragment Merging", SIGGRAPH 2010: https://graphics.stanford.edu/papers/fragmerging/
- Laine, Karras, "High-Performance Software Rasterization on GPUs", HPG 2011: https://research.nvidia.com/publication/2011-08_high-performance-software-rasterization-gpus
- Schütz, Lipp, Kristmann, Wimmer, "CuRast", arXiv 2604.21749 (abstract only): https://arxiv.org/abs/2604.21749

Engine and project pages:

- REAC programs: https://www.enginearchitecture.org/2025.htm , https://www.enginearchitecture.org/2024.htm , https://www.enginearchitecture.org/2021.htm
- Graphics Programming Conference archive: https://graphicsprogrammingconference.com/archive/2025/
- GDC Vault listings: https://gdcvault.com/play/1035671/Micropolygon-Rendering-in-Anvil , https://gdcvault.com/play/1034911/Mecha-BREAK-s-Virtual-Geometry , https://gdcvault.com/play/1035526/Rendering-Assassin-s-Creed-Shadows
- Unity 6 manual: https://docs.unity3d.com/6000.2/Documentation/Manual/urp/gpu-resident-drawer.html , https://docs.unity3d.com/6000.2/Documentation/Manual/urp/gpu-culling.html , https://docs.unity3d.com/6000.3/Documentation/Manual/lod/mesh-lod-introduction.html
- Bevy: https://jms55.github.io/posts/2024-06-09-virtual-geometry-bevy-0-14/ , https://jms55.github.io/posts/2024-11-14-virtual-geometry-bevy-0-15/ , https://jms55.github.io/posts/2025-03-27-virtual-geometry-bevy-0-16/ , https://bevy.org/news/bevy-0-17/ , https://github.com/bevyengine/bevy/pull/19318 , and `crates/bevy_pbr/src/meshlet/` at `main`
- NVIDIA samples: https://github.com/nvpro-samples/vk_lod_clusters (README and `docs/raster_twopass_culling.md`), https://github.com/nvpro-samples/gl_vk_meshlet_cadscene
- zeux: https://zeux.io/2023/01/16/meshlet-size-tradeoffs/ , https://zeux.io/2023/04/28/triangle-backface-culling/ , https://zeux.io/2025/09/30/billions-of-triangles-in-minutes/ , https://zeux.io/2026/09/30/billions-of-triangles-redux/ , https://github.com/zeux/niagara , meshoptimizer release dates from the GitHub releases API
- Tellusim: https://tellusim.com/mesh-shader/ , https://tellusim.com/metal-mdi/ , https://tellusim.com/compute-raster/ , https://tellusim.com/mesh-shader-performance/ , https://tellusim.com/mesh-shader-emulation/
- Wicked Engine: https://turanszkij.wordpress.com/2024/12/10/wicked-engines-graphics-in-2024/
- Anagnostou: https://interplayoflight.wordpress.com/2025/05/05/meshlets-and-mesh-shaders/ , https://interplayoflight.wordpress.com/2017/11/15/experiments-in-gpu-based-occlusion-culling/
- Kristóf: https://timur.hu/blog/2022/how-mesh-shaders-are-implemented , https://timur.hu/blog/2022/how-task-shaders-are-implemented
- Arm, Nanite on Mali: https://developer.arm.com/community/arm-community-blogs/b/mobile-graphics-and-gaming-blog/posts/mali-and-unreal-engine-s-nanite-enabling-the-future-of-mobile-graphics
- Apple Developer Forums, "Bad mesh shader performance": https://developer.apple.com/forums/thread/722047
- Scthe, nanite-webgpu README: https://github.com/Scthe/nanite-webgpu
- Eidos-Montréal, Deferred+: https://www.eidosmontreal.com/news/deferred-next-gen-culling-and-rendering-for-dawn-engine/
- Ubisoft, "Inside Anvil": https://www.ubisoft.com/en-us/game/assassins-creed/news/3aw71nNlR7kZJzoCATuNtm/inside-anvil-the-technology-powering-assassins-creed-shadows
- Reported only (Mecha BREAK): https://www.gameres.com/904234.html , https://www.qbitai.com/2024/03/131894.html

Unreal Engine source, read for comparison on 2026-10-01, branches `5.8` and `ue5-main`:
`Engine/Source/Runtime/Renderer/Private/Nanite/NaniteCullRaster.cpp` (per-level node culling,
`r.Nanite.PersistentThreadsCulling`, `r.Nanite.Culling.TwoPass`, `r.Nanite.MinPixelsPerEdgeHW`)
and `Engine/Source/Runtime/Renderer/Private/Nanite/NaniteShared.cpp` (`r.Nanite.MaxNodes`,
`r.Nanite.MaxCandidateClusters`, `r.Nanite.MaxVisibleClusters`). The defaults cited are the same
on both branches.
