# Open Nanite-like implementations: a code-level survey

**Status**: Frozen — non-normative  
**Research date:** 2026-10-01

Evidence notebook for the [geometry direction review](../2026-10-01-geometry-direction-review.md),
covering open virtualized-geometry and cluster-LOD implementations as they stand on 2026-10-01.
Repositories were read through the GitHub API (README, docs, key source files, commit and release
history) and the authors' blog posts and slides were fetched; no project code was built or run.
The load-bearing claims (Apple GPU-family facts, wgpu's Metal gating, the Forge slide numbers, the
Nyx and light-system README numbers, the Bevy 0.17 release-note numbers, the meshoptimizer release
dates, Unreal's Mac platform flags) were re-fetched a second time while assembling this page. Items
marked **[UNVERIFIED]** were not confirmed against a primary source; recheck them before a plan
depends on them.

Credit: Nanite and the other Unreal Engine systems described here are the work of Epic Games.
Unreal Engine source is copyright Epic Games, Inc. and was read under the Unreal Engine EULA; this
notebook describes mechanisms in its own words and reproduces no engine code, shader text or
comments. Unreal, Unreal Engine and Nanite are trademarks of Epic Games, Inc.

## 1. How to read this notebook

- **Self-reported** means a number or claim the project's own authors wrote in a README, blog, PR or
  slide that was not reproduced here. Nothing in this notebook was reproduced. "Reproducible in
  principle" means the repository ships the scene or a script; it does not mean someone ran it.
- Dates are GitHub API dates (push, commit, release `published_at`) read on 2026-10-01 unless noted.
- Several 2026 repositories carry a visible signal of agent assistance (a committed `.claude/skills`
  folder, a `claude.ai/code/session` link in a PR body, a README that says "AI-native", a PR
  description introduced as written by an assistant). The entry for each such repository names the
  signal that was seen; it is not a claim about who wrote the code or about its quality. Feature
  lists in any repository in this survey are claims until a number or a test backs them.
- Library names in this page: **meshoptimizer** (zeux, MIT) supplies meshlet building, simplification
  and, since v1.0, `clusterlod.h`; **METIS** is a graph partitioner (C library).

## 2. Comparison tables

### 2.1 Builder, LOD structure, runtime selection

| Project | Builder | LOD structure | Runtime selection |
|---|---|---|---|
| Bevy meshlets | meshoptimizer (Rust binding) for meshlets and simplification; METIS for triangle clustering and for grouping 8 meshlets; BVH builder copied from another Rust project | DAG, groups of 8 meshlets, up to 256 vertices / 128 triangles | per-cluster error and parent-error test, under 1 px; BVH8 over groups, one tree per LOD level, level-synchronous indirect dispatches |
| `vk_lod_clusters` (NVIDIA) | meshoptimizer `clusterlod.h` (local copy, one change); 128/128 clusters, 32 clusters per group | DAG | queue traversal on the GPU: persistent threads (NVIDIA only) or multi-pass; one 8-wide tree per LOD level |
| RTXMG 2.0 cluster LOD | fork of `clusterlod.h`, same defaults | DAG | multi-pass traversal only |
| niagara | `meshopt_simplifyWithAttributes` chain, up to 8 LODs, then meshlets per LOD (64/96) | discrete LOD chain, not a DAG | per draw in a compute cull pass, 1 px target |
| Nyx | meshoptimizer: `buildMeshletsFlex`, `partitionClusters`, `simplifyWithAttributes`; 128/128, 32 meshlets per group | DAG plus 8-wide BVH over groups | persistent-thread queue in Slang; per-meshlet check against the refining group's error |
| Solis | `clusterlod.h` (`clodBuild`), 16 clusters per group | DAG plus 8-wide BVH | persistent queue modeled on `vk_lod_clusters` |
| Carrot | own quadric simplifier plus METIS (2023-12 to 2026-03); `clusterlod.h` since 2026-03-28 | DAG | CPU first (2024-01), then task shader |
| nanite-webgpu | meshoptimizer and METIS compiled to WASM, run in the browser | DAG, multiple roots allowed | CPU or GPU, one thread per meshlet, no queue |
| light-system | `clusterlod.h` (pinned commit) | DAG with a partition tree | CPU depth-first walk; GPU traversal abandoned after 23 days |
| SynapseEngine | `meshopt_simplify` halving ladder, meshlets per LOD | discrete per-mesh ladder, levels 0 to 3 | compute, projected sphere size |
| Wicked Engine | `meshopt_buildMeshlets` 64/124 at mesh creation; `meshopt_simplify` chains made in the editor | discrete LOD per instance | CPU |
| metal-mesh (Swift) | own Swift quadric collapse; groups of at most 4 meshlets | DAG | flat test over every meshlet in the model; no traversal |
| nanite-moltenvk | `clusterlod.h` | DAG | persistent GPU traversal |
| cluster-lod-renderer (D3D12) | `clusterlod.h` | DAG | amplification-shader flat cut, then BVH group culling |
| O3DE draft PR 20064 | meshoptimizer-built DAG, `.azmeshletpack` | DAG | flat per-cluster cut, no traversal |
| Pocketcat, tomicz engine, MeshletSandbox | simplified LOD chains (Pocketcat via meshoptimizer; the others do not say) | discrete; Pocketcat builds 5 LODs and renders LOD0 only | none or distance |
| metalrender, RealEngine, lighthugger, Forge VB2, RavEngine | meshlets only (Forge: per triangle, RavEngine: neither) | none | none |

### 2.2 Culling, raster, visibility buffer, streaming

| Project | Culling | Raster back end | Visibility buffer and materials | Streaming |
|---|---|---|---|---|
| Bevy | instance, BVH node, cluster; frustum plus HZB; two phases; no cone cull | compute software raster (screen box at most 64 px) plus instanced hardware draws; 64-bit texture atomics | `R64Uint` buffer; one fullscreen triangle per material with depth-equal test | none after 3 years |
| `vk_lod_clusters` | frustum plus HiZ at instance, node, group, cluster; single phase default, optional two-phase with reject lists | one mesh-shader workgroup per cluster (NV or EXT); software raster stub "not faster" | no (forward-style fragment shading; visibility buffer is a debug view) | per group, CPU readback, budgets, hole-avoidance rule |
| RTXMG 2.0 | frustum and HiZ coarsen detail ("soft") instead of removing it | none (ray tracing only) | n/a | same loop, fixed pools |
| niagara | draw and meshlet level, full early/late/post passes, cone | task plus mesh, or compute cluster cull plus indirect draws | 2-target G-buffer | none |
| Nyx | instance, node, meshlet; two-phase HZB; backface and tiny-cluster culls | indirect `DispatchMesh`, hardware raster only | `R64` image via pixel-shader atomic max; one compute resolve to a G-buffer | 256 KB pages, LZ4, request-mask readback |
| Solis | instance, node, meshlet; frustum, backface, HZB; early pass plus "fix" pass | mesh shader, or compute-written `DrawIndexedIndirect` | direct G-buffer, no visibility buffer | readback, age eviction at 16 frames |
| Carrot | task-shader frustum and LOD cut | task plus mesh | `R64Uint`; one ubershader material pass | none |
| nanite-webgpu | per-instance and per-meshlet frustum, previous-frame HZB (cone cull tried, removed) | hardware, compute software (32-bit packed), billboard impostors | none (WebGPU has no 64-bit atomics) | none |
| light-system | CPU frustum and normal cone | vertex pulling, draws folded into 2 instanced draws on MoltenVK | diagnostic only | opt-in mmap |
| Wicked Engine | CPU frustum plus occlusion queries; mesh-shader path adds meshlet sphere, cone and previous-frame depth tests | ordinary draws; optional mesh shaders (off by default) | 32-bit ID (25-bit meshlet, 7-bit primitive); compute tile classification and shading | none for geometry |
| Forge VB2 | per-triangle: backface, frustum, small-triangle | compute only: in-place for small triangles, binned (128x128 bins) for large | linear `uint64` buffer, forward++ shade | none |
| RealEngine | two-phase, instance then meshlet, HZB | mesh shader | none (forward; box unchecked in README) | none |
| metalrender (Metal) | instance and meshlet; frustum, cone, last-frame bit, late HZB | task plus mesh | G-buffer | none |
| Pocketcat (Metal 4) | stub (`visible = true`) | ICB-driven mesh shader | `rg32Uint` visibility target | none |
| metal-mesh (Metal 3) | frustum, cone, two-pass HZB | object plus mesh shader | forward | none |

### 2.3 Platform, license, activity

| Project | Platforms | License | Last push | Alive? |
|---|---|---|---|---|
| Bevy | Vulkan and Metal in principle; needs 64-bit texture atomics | MIT or Apache-2.0 | 2026-10-01 (main) | maintained experimental; author not actively developing it |
| `vk_lod_clusters` | Windows, Linux, Vulkan; ray-traced half is NVIDIA-only | Apache-2.0 | 2026-09-30 | yes, entries every few days |
| RTXMG | Windows 10+, RTX, D3D12 or Vulkan | NVIDIA RTX SDKs License (source-available) | 2026-09-04 | yes |
| niagara | Vulkan 1.4, Windows, Linux | MIT | 2026-09-26 | yes |
| meshoptimizer | library | MIT | 2026-09-28 | yes |
| Nyx | Windows, D3D12 SM 6.6 | MIT | 2026-09-30 (branch `VSM`); main 2026-07-16 | yes |
| Solis | Windows, Vulkan and D3D12 | none (no license file) | 2026-09-13 | yes; author moved on to GI |
| Carrot | Windows, Linux, Vulkan | MIT | 2026-09-29 | yes |
| nanite-webgpu | browser WebGPU | MIT | 2026-05-09 (real work ended 2024-09-30) | finished by design |
| light-system | macOS via MoltenVK; Linux less tested | MIT | 2026-09-13 | burst-driven |
| SynapseEngine | Windows, Linux, Vulkan | AGPL-3.0 plus commercial | 2026-09-07 | yes |
| Wicked Engine | DX12, Vulkan, Metal 4 only | MIT | 2026-09-29 | yes |
| The Forge | GitHub: stale since 2025-03; Codeberg: DX12 only | Apache-2.0 | GitHub v1.63 2025-03-21 | split across hosts |
| RealEngine | DX12, Vulkan, Metal (new) | MIT | 2026-09-16 | yes |
| RavEngine | Metal, DX12, Vulkan | Apache-2.0 | 2025-07-29 | idle 14 months |
| ue5-nanite-macos | UE5 fork, M1 Max | MIT | 2023-05-24, archived | no |

## 3. Project notes

### 3.1 Bevy meshlets (virtual geometry), `main` at commit `52c3ec0d5e`

**State.** Still named "meshlet", still behind the cargo features `meshlet` and `meshlet_processor`,
still described as experimental. Latest stable is v0.19.1 (2026-08-13); 0.20 is at rc.2 (2026-09-28)
with no final tag yet *(GitHub releases API)*. The founding author's last virtual-geometry post is
dated 2025-03-27; the sixth-birthday post (2026-08-29) says the plan is to "potentially go back to
virtual geometry" after Solari. September 2026 fixes (UV derivatives, mip bias, backface handling,
glTF meshopt compression) come from other contributors porting NVIDIA's Zorah scene *(bevy PRs
#25796, #25801, #25928, #25953)*. Tracking issue #11518 still lists streaming, compute material
shading, normal/UV compression and multi-material meshlets as open.

**Builder** (`from_mesh.rs`, needs `meshlet_processor`). Input is a triangle list with position,
normal and UV only. Triangles are first partitioned by METIS (edge weight = shared position
vertices, `partition_count = triangles / 126` so partitions undershoot), then `build_meshlets`
runs inside each partition. The reason given by the author: meshoptimizer's own clusterer
optimizes for culling and vertex reuse, but virtual geometry wants meshlets that share few
vertices so fewer vertices are locked during simplification. Each LOD level then repeats: group
connected meshlets into groups of 8 with METIS, lock only vertices shared between groups, simplify
each group to about half its triangles with attribute-aware simplification, reject a group if more
than 60% of triangles survive, force group error to at least the max child error, re-split into
meshlets. Stuck groups retry on a later level. Group bounding spheres are merged from child
spheres, a technique taken from meshoptimizer's `demo/nanite.cpp`. An 8-wide BVH is built over
groups per LOD level (SAH splits), and the per-level roots are joined. Vertex data is copied
per meshlet, positions quantized and bit-packed, normals octahedral, indices 8-bit; assets are
LZ4-compressed on disk. Automatic glTF-to-meshlet asset processing "proved to be unfeasible", so
users convert manually *(fifth-birthday post)*.

**Runtime.** The selection rule: project the stored object-space error through the instance scale
and the distance to the nearest point of the group's sphere; draw a cluster when its own error is
under 1 px and the error of the group that would replace it is not. The BVH slot stores the
parent error of the groups beneath it, and inner nodes store the maximum, so a subtree whose
parent error is already invisible is skipped. Per view and frame the sequence runs twice (early,
late): instance cull (one thread per instance; frustum, then occlusion against the previous
frame's depth pyramid with previous transforms), BVH cull (eight threads per node, the CPU
records one indirect dispatch per BVH level ping-ponging two queues, i.e. breadth-first
level-synchronous traversal, not persistent threads), cluster cull (one thread per cluster).
Occluded items are deferred to the late pass, which re-tests them against the pyramid built from
the early pass's depth. Occlusion samples 16 texels by `textureLoad` because wgpu lacks a
min-reduction sampler. The 0.14-era sphere and cone tests are gone; culling uses AABBs.

**Raster and shading.** Clusters whose screen box is at most 64 by 64 px and that do not cross the
near plane go to a compute software rasterizer (128-thread group per cluster, `textureAtomicMax`
on a 64-bit texture, high 32 bits reverse-Z depth, low 32 bits `(cluster_id << 7) | triangle_id`);
the rest go to one instanced indirect draw of 128 triangles per cluster whose fragment shader does
the same atomic max. Both paths write one texture. Resolve passes write real depth and a 16-bit
"material depth" (`material_id / 65535`); each material is then shaded by a fullscreen triangle
placed at that depth with an Equal depth test. Materials must be opaque, cannot use a vertex shader
or implicit derivatives. No mesh shaders (0.14 notes: "does not use GPU mesh shaders, so older GPUs
are compatible"; generic mesh-shader pipelines merged in 2026-09 are not used by the renderer).

**Build order and elapsed time** *(PR merge dates)*: first meshlet commit 2023-09-30 (the author's
own statement in the fourth-birthday post; the PR branch was rebased, so the API does not show it);
prototype merged 2024-03-25 (PR #10164, opened 2023-10-17), which already contained hardware
raster into a visibility buffer, a depth pyramid and two-pass occlusion culling but no LOD;
continuous LOD DAG 2024-04-23;
LOD-compatible two-pass occlusion 2024-04-28; GPU-filled cluster list 2024-05-04; single-pass
depth downsample 2024-06-03; 0.14 ships 2024-07-13; software raster 2024-08-26 (11 months in);
255 v / 128 t meshlets (256 v since 2025-10-27) and compression 2024-09 to 10; builder rework and normal-aware
simplification 2024-10 to 11; METIS clustering 2025-01-05; BVH culling 2025-06-29 (21 months in,
finished by two other contributors after the founder took a burnout break); streaming never.

**Measured (all self-reported; no Apple number anywhere; rows are in section 4.3).** The blog and
release-note figures are visibility-pass or full-render times on an RTX 3080 or a "4070". Between
0.14 and 0.15 the bunny scene fell from 4.97 to 0.93 ms, but the release changed the builder and added
software raster together, so the split is not isolated. The author's account is that DAG quality
dominates: the 0.15 builder changes (255 v / 128 t meshlets, groups of 8, partial border locks) took
the bunny from 7 LOD levels ending at 19 meshlets to 12 levels ending at 1, and worst-level fill
rate from 20% to 76%.

**Metal, the part that matters here.** The plugin requires `TEXTURE_INT64_ATOMIC`, `TEXTURE_ATOMIC`,
`SHADER_INT64`, `SUBGROUP`, `DEPTH_CLIP_CONTROL`, `IMMEDIATES` and exits if any is missing
*(`meshlet/mod.rs`)*. wgpu's Metal backend sets `TEXTURE_INT64_ATOMIC` only when the device supports
the Apple9 family and MSL 3.1, and `SHADER_INT64_ATOMIC_MIN_MAX` for Apple9, or Apple8 on Mac2
*(wgpu `trunk`, `wgpu-hal/src/metal/adapter.rs`, re-read 2026-10-01)*. Apple's own table (section 6.1)
maps M1 to Apple7, M2 to Apple8, M3 and M4 to Apple9. A user on an M1 Pro reported a startup panic
and the author replied "you need an M2 GPU or newer" *(bevy PR #17765 comment)*; the wgpu gating says
Apple9, so the author's statement and the source disagree and I have not tested either. An
Apple driver or shader-compiler defect reached wgpu in 2026: texture-atomic writes from a fragment
shader are randomly lost unless an ordinary texture write follows them, which is exactly the
hardware-raster pattern *(wgpu PR #9185, "Workaround Metal driver bug with atomic texture writes",
merged 2026-03-13)*. The merged change makes naga emit, after every texture-atomic operation, an
ordinary write to the same texture guarded by a condition that is never true. Its repro uses a
32-bit `r32uint` atomic texture, so the defect is not specific to 64-bit atomics. The PR says only
"run the provided example on Apple silicon": it names no GPU model, GPU family or OS version, and
its author writes that he is "not fully sure why this works". The same workaround is in Blender
*(commit `aa952205`, 2025-09-22, "Metal: Add workaround for imageAtomic synchronization issue")*,
whose message calls it a compiler or driver bug to be kept "until this is fixed upstream" and whose
code comment says it can occur in any shader; it names no hardware or OS either. Bevy's meshlets
had been broken for two releases when PR #23307 (2026-03-11) fixed several Bevy-side shader errors
and pointed to the wgpu PR for this defect. Whether Slang-generated MSL needs the same trick is
**[UNVERIFIED]**.

**Lessons the author wrote** *(blog posts 0.14, 0.15, 0.16, birthday posts)*: "DAG quality is the
most important part of virtual geometry (and the hardest to get right)." "Skip software raster
until close to the end. If you have mesh shaders, stick with those." A thread-per-cluster scan
over millions of clusters is "an enormous waste of time"; the cure is hierarchy traversal (the
25-bit cluster ID overflowed at about 33.5 M clusters). The author refused a no-64-bit-atomics
fallback because it would be "pretty much an entire extra copy of the codebase" *(PR #14623)*.
Repeated blockers were wgpu feature gaps (mesh shaders, 64-bit atomics, min-reduction samplers).
The founder also wrote that occlusion culling was "broken" in 2024 and that he was avoiding
debugging it; the HZB was made conservative only on 2026-01-20 *(bevy PR #22603)*.

### 3.2 `vk_lod_clusters` and RTXMG

**State.** `nvpro-samples/vk_lod_clusters`: created 2025-01-23, Apache-2.0, 204 stars, last push
2026-09-30, three issues ever. RTXMG (NVIDIA-RTX) v2.0.0 (2026-08-28) ports the same cluster-LOD
system to HLSL on D3D12 and Vulkan through NVRHI; it is ray tracing only (the repo says there is
no rasterization or mesh-shader path) and its license is the NVIDIA RTX SDKs License, which grants
use and modification of sample source but is not an OSI license; source files carry
`LicenseRef-NvidiaProprietary`. The archived `nv_cluster_builder` and `nv_cluster_lod_builder`
point users to meshoptimizer, and `nvpro-samples/nv_cluster_lod_library` now returns 404. The
meshoptimizer-based builder replaced NVIDIA's on 2025-09-25 (changelog: about 5x faster, 20x less
memory, deterministic; NVIDIA's still did better on low-connectivity geometry such as leaves and
rubble; self-reported).

**Builder loop** *(local `meshopt_clusterlod.h` and `docs/lod_generation.md`)*, which is the same
code path as meshoptimizer's upstream `clodBuild`:

1. Weld positions (`meshopt_generatePositionRemap`) so attribute seams do not hide topology.
2. Cluster the whole mesh (`meshopt_buildMeshletsFlex` for raster, `meshopt_buildMeshletsSpatial`
   for ray tracing; sample defaults 128 triangles / 128 vertices, 32 clusters per group).
3. While more than one cluster remains (one iteration is one DAG level): partition clusters into
   groups with `meshopt_partitionClusters` (adjacency plus spatial locality, Morton-sorted for
   streaming locality; partitions can exceed the target by a third, so the target is reduced until
   the worst case fits 32); lock every vertex touched by clusters of two different groups; simplify
   each group to half its triangles with attribute-aware simplification (sparse, absolute error,
   optional permissive mode); a group that cannot get below 85% of its input is "stuck" and becomes
   a terminal group with error `FLT_MAX`; make bounds and error monotonic; re-clusterize the
   simplified triangles for the next level.
4. Monotonicity is what makes parallel, crack-free selection possible. Group bounds are the sphere
   union of the input clusters' bounds, never a tight sphere of the simplified geometry (the
   source comment says a precise sphere "may violate monotonicity"), and group error is
   `max(previous * 1.5, current)` in the sample (`1.0` library default).
5. The result is a DAG, not a tree: a cluster descends from several groups and feeds one.

**Selection rule** *(header comment in `clusterlod.h`)*: a cluster C sits in a group G (the group
that will simplify it into a coarser level) and was itself produced by simplifying a group R at the
level below (clusters of the original mesh have no R). Draw C when R's simplified error is at or
under the pixel threshold (C is fine enough) and G's simplified error is over it (G's coarser
replacement would be too coarse). Because spheres and errors are monotone up the DAG, every cluster
decides independently and the union is watertight with no graph-cut search. The runtime hierarchy is one 8-wide tree per LOD level over that level's
groups, with a root over the level roots; inner nodes carry the maximum error and union sphere.
Meshoptimizer added the same idea as `clodBuildHierarchy` in v1.2 (2026-06-30).

**Runtime** *(README, `docs/`, shaders)*. All selection and culling run on the GPU. Traversal seeds
one item per visible instance, runs a producer/consumer node queue (persistent threads with a
global in-flight counter, default on NVIDIA only, or a multi-pass mode added 2026-04-20 because the
persistent kernel "has off-spec behavior"), then a separate group kernel checks residency,
applies the two-sided cluster test, then frustum and HiZ per cluster, and appends survivors to
render lists (hardware, software, alpha-tested bins). Occlusion has three modes: single-pass
previous-frame HiZ (the default; "can cause artifacts on faster motion"), opt-in two-pass
(2025-12-15), and two-pass with reject lists (2026-09-15; the reject-list option is on by default
once two-pass is enabled), where pass 1 records everything the LOD metric accepted but occlusion
rejected so pass 2 re-tests only those instead of re-traversing; the changelog claims 9 to 12% frame
time on multi-million-instance scenes (self-reported, no scene or GPU). Raster is one mesh-shader
workgroup per cluster from an indirect mesh-task draw, with forward-style fragment shading; the
"visibility buffer" mode is a debug visualization. A compute software rasterizer (2025-12-02,
64-bit image atomics) exists, and the README says it "hasn't been tuned yet and in typical usage
scenarios is not faster than the mesh-shader because clusters tend to have larger than single pixel
triangles".

**Streaming.** The unit is the cluster group (up to 32 clusters, about 4 K triangles): one blob on
disk, one allocation in VRAM, aged and loaded as a unit. The coarsest group of every geometry stays
resident. Traversal appends missing groups to a GPU request list (an atomic max of the frame index
dedupes within a frame), the list is read back to the host, the host clamps to per-frame limits and
memory budgets, uploads blobs, and a compute pass patches device-side scene buffers before the next
traversal; freed memory is released only after the device applied the unload. The key correctness
rule: a non-resident group is never traversed, and if a cluster's generating (finer) group is not
resident the cluster is drawn anyway, so a missing finer group degrades to the coarser surface
instead of a hole. Latency is at least one frame. Textures are not streamed. Compression (on by
default since 2026-08-03) drops mantissa bits of positions and UVs and encodes indices with
`meshopt_encodeMeshlet`; decompression is on the CPU.

**Numbers.** There is no frame-time table in the repository. The only raster number is outside it:
zeux reports Zorah on a GeForce RTX 3050 at 16 ms rasterization and 26 ms ray tracing with about
2 GB of geometry pool, resolution not stated *(zeux.io 2025-09-30, author-reported)*. RTXMG's
README banner reports 15.5 ms per frame on an RTX 5090 at 4K with DLSS Quality, 56 M unique and
778 M instanced triangles in the frame; the frame is path traced with DLSS ray reconstruction, so
this is not a geometry cost (self-reported). The repo ships a camera-path benchmark harness and a
154-permutation test suite with "no golden image comparison".

**Chronology** *(`CHANGELOG.md`)*: month 0 (2025-01-30) shipped builder, GPU traversal, streaming,
ray-traced path and mesh-shader raster together; a disk cache of processed LOD followed within
weeks because the build was too slow to redo per run; ray-tracing acceleration-structure build
cost dominated the next six months (BLAS sharing, caching, merging); the builder switched to
meshoptimizer in month 8; two-pass raster culling arrived in month 11, reject lists in month 19.

**Lessons recorded:** the vegetation on Zorah "will appear to fade out a bit quickly" because
mesh-based simplification does not preserve volume on sparse geometry; early-out of too-coarse
subtrees "may interfere with streaming, depending on how dependencies are implemented"; a flat
dedicated kernel for discrete LOD was measured and rejected in favor of the hierarchical traversal.

### 3.3 meshoptimizer cluster LOD and niagara

**meshoptimizer.** Releases (API `published_at`): v0.22 2024-10-25, v0.23 2025-03-14
(`meshopt_buildMeshletsFlex`, experimental `meshopt_partitionClusters`, `meshopt_computeSphereBounds`),
v0.24 2025-06-12 (`meshopt_buildMeshletsSpatial`), v0.25 2025-08-20 (permissive simplification,
`meshopt_generatePositionRemap`), v1.0 2025-12-08 (`clusterlod.h` announced), v1.1 2026-04-02
(meshlet codec), v1.2 2026-06-30 (`clodBuildHierarchy`), v1.3 2026-09-25 (permissive mode stable,
border dilation, voxel remesher). `demo/clusterlod.h` is a 40 KB single header; `demo/nanite.cpp`
is a small harness that simulates a DAG cut for statistics. The header's data model is
deliberately minimal: no streaming, no pages, no materials, no traversal runtime. `clodBuild`
calls back once per group; `clodBuildHierarchy` builds the per-level trees. Config highlights:
`simplify_ratio` 0.5, `simplify_threshold` 0.85, sloppy-simplification fallback, error clamping
(`simplify_error_clamped`, on by default, the idea zeux credits to Unreal), edge-length error
limit, border dilation for foliage, and two default sets (raster and ray tracing).

The 2024 meshoptimizer discussion #750 between the Bevy author, zeux and the nanite-webgpu author
is the origin of the monotonic-bounds fix: the Bevy author could not reduce hierarchies to one root
and saw meshlet occupancy fall from 99.6% at LOD0 to 15.7% by LOD5; zeux replied that balanced
64v/64t meshlets "will never occur" and suggested 256v/128t; the nanite-webgpu author documented
that Nanite forbids simplifying vertices shared between meshlets, freezing 30 to 50% of vertices at
lower DAG levels. METIS stopped being necessary in the demo on 2025-09-18.

zeux's two posts give the strongest published builder numbers *(zeux.io 2025-09-30 and 2026-09-30,
author-reported, reproducible with the repos)*: Zorah (1.64 B triangles) processing fell from 9m20s
to about 3m45s across several rounds (sparsity fix, largest-mesh-first scheduling, SIMD box merging,
Morton-sorted triangles, a per-thread arena allocator that took Windows from 4m20s to 2m38s); error
clamping cut resident geometry on Zorah from 996 MB to 678 MB and its CLAS from 880 to 574 MB; the
meshlet codec reaches about 6.5 bits per triangle and index data fell from 9.8 GB to about 2.6 GB.
His closing view on clustered LOD: "I'm still not sure"; triangles stay suboptimal and the extra
complexity of managing billions of triangles is "difficult to wave away".

**niagara** is not a cluster-LOD implementation. `src/scene.cpp` builds a chain of at most 8 LODs by
repeatedly simplifying to 60% and then builds meshlets per LOD; the LOD is chosen per draw in
`drawcull.comp.glsl` (coarsest whose error is under a 1 px threshold). No `clusterlod`, DAG or
hierarchy appears in `src/`, and the commits since 2025-01 concern ray-tracing clusters, the meshlet
codec and opacity micromaps. It is the reference for full two-phase culling: early cull, early
render, late cull, late render and a post pass for non-opaque draws; draw-level culling uses the
previous frame's visibility bit and a min-reduction depth pyramid; meshlet occlusion runs in the
task shader; a compute cluster-culling path with indirect draws serves as the non-mesh-shader
fallback. The 33 streams show how one expert sequenced the work: mesh shading, multi-draw indirect
with GPU frustum culling, depth pyramid and instance occlusion, triangle culling, meshlet occlusion,
task commands and compute cluster culling, glTF and bindless, ray tracing. The README publishes no
frame times. A 2026-03 commit mentions macOS; whether niagara runs there is **[UNVERIFIED]**.

### 3.4 Nyx (moonlovelj)

Created 2025-07-31, MIT, 148 stars, built on Microsoft MiniEngine (D3D12) with Slang compiled to
SM 6.6 at runtime. Releases v1.0.0 (2026-02-08), v1.0.1, v1.1.0 (2026-07-16). The most complete
small system found: DAG plus BVH, GPU traversal, two-phase HZB, mesh shaders, visibility buffer,
paged streaming.

- **Builder** (`MiniEngine/Model/MeshletBuilder.cpp`): LOD0 meshlets by `buildMeshletsFlex`
  (128/128, at least 32 triangles); groups of 32 meshlets via `partitionClusters`; boundaries
  locked through a position-only remap; group simplification with sparse, absolute-error,
  permissive options and a sloppy fallback; monotone error `max(parent, child * 1.5)`; an 8-wide
  BVH over groups; each meshlet header stores the index of the group that refined it. METIS was used
  from 2025-11-10 to 2026-02-05 and then removed.
- **Traversal** (`Shaders/DAGCull.slang`): a persistent compute kernel claims batches from a global
  ring queue (globally coherent buffers, atomics on a queue-state struct); each thread handles one
  child of an 8-wide node, tests its AABB against the HZB and the projected error from the node's
  stored parent error, pushes surviving inner children back, and expands leaf groups to meshlet
  candidates; a second tier re-checks the meshlet's AABB and the refining group's error. Leaf
  visits set bits in a request mask; non-resident groups are skipped. Pass 0 uses the previous
  frame's matrices and HZB; pass 1 re-tests against the rebuilt HZB and draws only
  occluded-then-visible meshlets.
- **Raster and shading**: indirect `DispatchMesh`, a 32-thread mesh shader emitting up to 128
  vertices and triangles with in-shader triangle culling; the pixel shader alpha tests and writes a
  64-bit `InterlockedMax` into an `R64` image (float depth in the high bits, command index and
  primitive in the low bits); one compute pass resolves it to four G-buffer targets using
  hand-built barycentrics and `SampleGrad` over bindless textures. There is no software rasterizer
  in the tree, so the design depends on 64-bit image atomics even though it never uses compute raster.
- **Streaming**: 256 KB pages in 256 MB GPU chunks allocated on demand, a CPU page table with
  last-used frame, pinned root pages, request mask read back through three buffers with fences,
  one I/O thread doing LZ4 decompression.
- **Order of work** *(commits)*: about nine weeks of deferred-renderer groundwork first; indirect
  draw 2025-09-23; GPU frustum cull 10-23; meshlets 10-30; LOD DAG 11-05; HZB 11-20; two-phase
  12-05; mesh shaders 12-13; visibility buffer 12-14; alpha test and skinning disabled "for
  performance" 12-16; BVH hierarchy cull authored 2025-12-27 and committed 2026-01-07; streaming
  01-15; alpha test back 01-26; LZ4
  01-31; public 02-08. That is 77 days from first meshlets to streaming and 101 days to release.
  A virtual-shadow-map branch has 47 commits from 2026-07-22 to 2026-09-30.
- **Measured (self-reported)**: README states Zorah at 1,639,668,228 unique and 18,949,504,889
  instanced triangles on an i7-14700KF with an RTX 4070 Ti SUPER at 4K, with the caption "4K, 144Hz,
  18.9B Instanced Triangles". The repo's counting script defines "instanced" as the sum over scene
  nodes including GPU-instancing counts, so 18.9 B is a scene statistic, identical to NVIDIA's
  published Zorah figure, not triangles rasterized. The only timing artifact is one Nsight Graphics
  frame (a screenshot in the README): 6.94 ms (5.94 ms active), with the main indirect mesh pass at
  2.88 ms and SM throughput 24.4%. The capture states neither its resolution nor its camera
  position (the README's "4K" describes the screenshots and video), and no camera path or per-pass
  table is published. The author wrote no lessons.

### 3.5 Solis, Carrot, nanite-webgpu

**Solis** (Vovan675; created 2024-02-26; 104 commits; no license file). Builder calls `clodBuild`
(16 clusters per group, spatial partition) and builds an 8-wide BVH with
`meshopt_spatialClusterPoints`. The traversal shader's header says "Reference: NVIDIA
vk_lod_clusters" and the work distribution is copied from that sample. The "compute fallback" in
the README is compute-written `DrawIndexedIndirect` arguments feeding hardware raster, not a
software rasterizer. Shading goes straight to a G-buffer. Streaming reads back a GPU request list,
evicts at 16 frames of age and pins the coarsest groups. Order: about two years of engine work
(bindless, PBR, frame graph, DX12 RHI, DDGI) before geometry; first meshlets 2026-02-07;
continuous LOD five days later; hierarchy streaming and a new mesh format 2026-04-20; two-pass
occlusion 2026-04-24; then the author moved to DLSS and GI. About 11 weeks of geometry work. README
claim: one Zorah is "~10 GB as glTF and ~70 GB once baked", 25 copies are packed into one world and
rendered with "up to 1 GB of VRAM"; no timings, no hardware named, and the release assets hold
Bistro and Cerberus, not Zorah. A Metal 4 backend appears under "Coming next".

**Carrot** (jglrxavpok; MIT; "Recreating Nanite" blog series 2023-11-12 to 2024-08-21). Path:
visibility buffer first (`R64Uint`, `imageAtomicMax`, then a material pass; 2023-11-26), clusters
and a METIS-based LOD hierarchy (2023-12), own simplifier with k-d tree, CPU runtime selection
(2024-01-25), task/mesh-shader culling and LOD cut on the GPU (2024-02-25), ray-traced lighting with
per-cluster-group BLASes (2024-08). About 9 months from plan to RT, about 3 months to GPU-side
selection; no software raster ("maybe" in the plan) and no streaming. Lessons the author wrote:
LOD quality was bad at first ("some meshes got basically destroyed"); not merging vertices on
meshlet borders "massively improves LOD quality" (commit 2024-02-10); the Happy Buddha took over 4 minutes to
build before dropping to 40 seconds; the first mesh-shader draw cost 8.65 ms on Bistro because
workgroups used one thread; one BLAS per cluster (30,000 BLASes) exhausted VRAM. On 2026-03-28 the
author deleted the custom builder and adopted `clusterlod.h` ("It was fun having my own version",
net -537 lines). End state in the blog: Bistro at about 80 FPS, lighting-bound, GPU model not
named. Windows and Linux only.

**nanite-webgpu** (Scthe; MIT; 1,110 stars; browser). Six weeks from first bunny (2024-06-04) to
public (2024-07-15), real work ended 2024-09-30. Pipeline built in the browser with WASM
meshoptimizer and METIS, many DAG roots allowed (a region that cannot shrink by 6% is kept),
one-thread-per-meshlet GPU selection with a CPU twin for debugging, frustum plus previous-frame
HZB, hardware raster, a compute software rasterizer for tiny triangles and billboard impostors.
WebGPU has no 64-bit atomics, so the software path packs 16-bit depth and an octahedral normal in
32 bits, renders untextured, and no visibility buffer is possible. The software rasterizer was
the last major feature, built in two days. The README FAQ is the best written postmortem in the
survey: "Meshlet LOD hierarchy is quite easy to get working... But if you want to do it
efficiently, it will be a pain"; "You spend more time working on culling and meshlets instead of
Nanite itself"; the error metric ("what does the right meshlet mean?") is the heart of Nanite. A
companion repository (`nanite-simplification-tests`) shows that standard rules cannot always
yield an optimal DAG and that a random-triangle-removal fallback with inflated error always works.
It publishes no frame times (RTX 3060, Chrome 126 for the demos).

### 3.6 light-system, lighthugger, SynapseEngine

**light-system** (usestemframework; created on GitHub 2026-09-03 with history from 2026-03-25; MIT;
0 stars; its `AGENTS.md`, audit file and a worktree-agent merge commit indicate agent-assisted
development, inferred from file and commit names). The earlier survey called it a Nanite-class
renderer with measured Apple M4 numbers; its own status document disagrees: cluster and LOD
selection and frustum/cone culling run on the CPU (`simulate_traversal`, thread-pooled since
2026-09-07), the GPU traversal kernel "is retained but not dispatched", and the visibility buffer
is written only on capture frames. README numbers (self-reported): Apple M4, MoltenVK, 1280x720,
Stanford Dragon (871 K triangles) median 15.4 ms, generated city (1 M triangles) 30.6 ms. The same
repository's later logs show 8.3 to 8.5 ms "paired under load" and 37 to 191 ms in the first
comparison. Its status document calls the Godot comparison unfavorable: stock Godot Forward+ was
faster on the city scene in every recorded run (about 1.6 to 2.1 times at the end), while the
dragon ended at parity with Godot under the same machine load.
Useful Metal data from its logs: MoltenVK advertises `drawIndirectCount` but implements a stub, so
each indirect entry cost a Metal draw encode (about 0.15 to 0.2 microseconds each); folding 20,732
draws into 2 instanced draws with degenerate-triangle padding cut submit time from 6.6 to 1.9 ms;
the remaining 2 ms was Metal command-buffer encoding.

**lighthugger** (expenses; Vulkan 1.3; 2023-11-08 to 2023-12-10; GPL-3.0, MIT on request; 65 stars).
A four-week meshlet plus visibility-buffer renderer on San Miguel 2.0: instance culling with a
single-pass prefix sum over per-instance meshlet counts, a per-meshlet indirect dispatch "essentially
emulating mesh shaders in compute", a visibility buffer, one compute lighting resolve. No LOD, HZB,
streaming or timings. It is a readable example of the compute-emulated mesh-shader pattern.

**SynapseEngine** (TamasPetii; Vulkan; AGPL-3.0 plus commercial; xmake). Task/mesh-shader meshlet
culling with a discrete LOD ladder, hierarchical chunk, model, mesh, meshlet culling (frustum,
Hi-Z, cone, zero-pixel triangle), a work-graph variant that was added and deleted in 2026. Its
"virtualized shadow mapping" is a 4K bucketed atlas, not a page-table cache. Meshlets arrived 13
months after the repository began. A conference paper in the repo reports, on an RTX 4060 with
1,000,000 instances of 12 meshes in an unlit mode, cumulative culling 124.4 ms (none) to 0.653 ms
(cone, hierarchical frustum, Hi-Z, dynamic LOD) and mesh versus traditional geometry pass 0.411
versus 0.611 ms on Bistro (self-reported; resolution and triangle counts not stated). "1M entities"
means instances of 12 meshes.

### 3.7 Wicked Engine and The Forge Visibility Buffer 2

**Wicked Engine** (turanszkij; MIT; v0.72.113 on 2026-08-24; last push 2026-09-29). Not a Nanite
clone: meshlets are a packing and culling unit, LOD is a discrete per-instance chain, and there is
no geometry streaming. The relevant parts are the visibility buffer and the Metal backend.

- *Visibility buffer*: one 32-bit target, 25 bits meshlet ID and 7 bits primitive ID. A compute pass
  writes one meshlet record per instance and meshlet (instance, geometry, material, primitive
  offset, and an 8-bit shader type). Shading is compute: a tile-analysis kernel (PR #1639, merged
  2026-06-08) classifies each screen tile as uniform-primitive or divergent, then per-tile bins
  shade by shader type through bindless material fetch; the author runs it on the async compute
  queue after shadow maps and reflections.
- *Mesh shaders*: optional, off by default (`MESH_SHADER_ALLOWED = false`). The amplification
  shader culls 32 meshlets per group (sphere frustum, cone, projected-size, previous-frame depth
  pyramid, single phase). The author's 2024 retrospective: they won on an untextured bunny test but
  "in real scenes, the performance was always worse than the vertex-shader based rendering" and hurt
  async-compute overlap *(wickedengine blog 2024-12-10; no hardware or numbers given)*.
- *Metal*: `wiGraphicsDevice_Metal.cpp` is Metal 4 only (exits if unavailable), uses argument
  tables, counter heaps and `MTL4::` acceleration structures; shaders are HLSL converted by Apple's
  Metal Shader Converter. macOS support merged 2026-01-11, iOS 2026-06-03. Tracking issue #1479
  (opened 2026-01-05, updated 2026-08-18) lists mesh and amplification shaders as done with a
  workaround (a mesh shader without a pixel shader is not allowed, so a dummy one is bound),
  multi-draw indirect and indirect-count mesh dispatch as to-do, sparse resources as crashing, and
  path tracing as "extremely slow". Per the PR text, the macOS port does not support `SV_PrimitiveID`
  as a pixel-shader input, so the ID pass uses two extra index streams (PR #1237, 2025-10-07). Issue #1597 shows an M1 validation
  abort on `ulong` texture atomics, fixed by gating on Apple8 or Apple9 or Mac2.
- No Metal frame-time number for any of this has been published.

**The Forge Visibility Buffer 2 ("TVB 2.0")**. The GitHub repository is not archived (Apache-2.0)
but its last non-README commit is the v1.63 merge of 2025-03-19; its README now says "Release 1.64
... Continued on https://codeberg.org/The-Forge". The Codeberg tree (created 2026-08-14) holds only
the PC/DirectX 12 runtime: its Visibility Buffer 2 example has a PC project only and its graphics
layer has no Metal directory. The last public snapshot containing Metal and macOS builds of VB2 is
therefore the GitHub tag v1.63 (2025-03-21).

Mechanism *(example shaders on GitHub master, I3D 2024 slides)*: a per-triangle pipeline with no
clusters, hierarchy or LOD. A triangle-filtering compute pass runs one thread per triangle across
all views (camera and shadows): backface (determinant), near-plane clip count, frustum on the
clamped screen box, sub-pixel box. Survivors are classified by box area. Small triangles are
rasterized right there by looping over the box and doing a 64-bit atomic max into the visibility
buffer; large ones are written into 128x128-pixel bins (per-bin counters in groupshared memory,
reserved ranges, no per-triangle global atomics). A second dispatch covers all bins: 16x16 thread
groups, each thread loading one binned triangle and covering a 4x4 footprint of a 64x64 sub-bin,
testing barycentric coverage, packing `depth | triangle data` into a `uint64`, taking a groupshared
atomic max and flushing the sub-bin to global memory with another 64-bit max. The visibility buffer
is a linear `uint64` buffer, not a texture; the host code says Metal 2 lacked image atomic min/max,
"Thus, we fallback to a regular buffer for metal" (the comment may predate Apple8 texture atomics).
A full-screen forward++ pass reads it and shades with tiled light clusters. The slides say TVB 2.0
is "currently 1.25x to 2x slower than TVB 1.0" and that VB1 was about 2x faster than a G-buffer
renderer on an Xbox One on San Miguel.

| Stage (self-reported, I3D 2024 slide 20) | RX 7600 1080p | PS5 1080p | PS5 4K | MacBook M2 1080p | iPhone 15 Pro 1704x786 |
|---|---|---|---|---|---|
| Triangle processing | 3.5 ms | 3.4 | 15.5 | 11.3 | 19.3 |
| Bin rasterization | 1.1 | 1.4 | 8 | 10.9 | 10.2 |
| Shading | 0.7 | 1.3 | 3.8 | 1.8 | 6.4 |
| Total (async compute overlap) | 3.8 | 5.2 | 15.6 | 13.7 | 19.3 |

The slides do not name the scene on this slide (a speaker note on slide 7 says "runs at 7ms on RX
7600, around 3 million triangles") or the M2 configuration. This is the only Apple-silicon timing
found for a compute-rasterized visibility buffer with 64-bit atomics. Compared with the RX 7600,
the M2 spends about 3.2 times longer on triangle processing and about 10 times longer on bin
rasterization.

### 3.8 RealEngine, RavEngine, ue5-nanite-macos

**RealEngine** (zhaijialong; MIT; 318 stars; last push 2026-09-16): the cleanest short read of
two-phase culling at both instance and meshlet level. `instance_culling.hlsl` tests instances
against the previous frame's HZB, appends failures to a second-phase list and re-tests them against
an HZB built from phase-1 depth; `meshlet_culling.hlsl` is an amplification shader (32 meshlets per
group) doing sphere frustum, normal cone and HZB, with phase-1 occluded meshlets appended to a
second list. README: always two indirect `DispatchMesh` calls per PSO. Meshlets only: no LOD, no
visibility buffer (unchecked on its roadmap), no streaming, no timings. A Metal backend exists
(first commit 2023-07-29, Metal Shader Converter 2.0 since 2025-01-27, `DispatchMesh` wired to
`drawMeshThreadgroups`, Apple7 minimum), and the three newest commits (2026-09-16) are Mac build
fixes. Whether the mesh-shader path renders correctly on Metal is **[UNVERIFIED]**.

**RavEngine** (Apache-2.0; last push 2025-07-29; README "early alpha"): `defaultcull.csh` is a
per-object compute cull with frustum, depth-pyramid occlusion and distance LOD writing indirect
commands. meshoptimizer is vendored but only vertex-cache, overdraw and fetch passes are used; no
meshlets, no visibility buffer.

**ue5-nanite-macos** (philipturner; archived; last push 2023-05-24; 66 stars). An attempt in 2022
to run Unreal 5's Nanite on an M1 Max by forcing Nanite on for the Metal SM5 platform and replacing
64-bit texture atomics with non-atomic or lock-based fallbacks. Result in the README: a Nanite
sphere appeared in all eight debug views in the editor but not in the main view, then the Mac froze
and needed a reboot. What blocked it, from the README and the repository: (1) no 64-bit atomics on
M1-class hardware and no engine path to reuse (Epic had deleted the old lock-buffer fallback in
commit 9b68f6b, 2022-04-20); (2) the author's 32-bit-atomics workaround (lock word with 24-bit depth
and an 8-bit counter) was built and tested as a standalone demo but never integrated, with the
author's own estimate of 2.5x bandwidth and 5x latency; (3) an undiagnosed GPU hang with no debugger
signal; (4) builds took 44 to 55 minutes on a roughly 200 GB tree. The final status text: "I do not
plan to finish this project myself." His March 2023 atomics tests show native 64-bit min/max
passing on M2 Max and failing on M1 Max, A15 and A16, which matches Apple's table (section 6.1).
The earlier survey's "archived 2024-08-16" could not be confirmed; the API shows `archived: true`
and a last push of 2023-05-24.

Unreal today: on both `release` and `ue5-main` (read 2026-10-01),
`Engine/Config/Mac/DataDrivenPlatformInfo.ini` marks `METAL_SM5` as `bSupportsNanite=false` and the
new `METAL_SM6` platform as `bSupportsNanite=true` with `bSupportsUInt64ImageAtomics=true`. Epic's
own Mac Nanite therefore exists on the shader-converter path; the integration notebook
([nanite-system-integration.md](nanite-system-integration.md)) owns the details.

### 3.9 Native Metal and Apple-GPU projects

The long tail of Metal repositories is small: GitHub topic `cluster-lod` is empty and
`virtual-geometry` lists 5 repositories (all examined). Searches such as "virtual geometry metal",
"cluster lod metal" and "Metal 4 mesh shader" returned no further cluster-LOD renderers.

| Project | What it really is | Evidence |
|---|---|---|
| `tonadr1022/metalrender` (C++, Metal 4 via metal-cpp, HLSL through DXC and Metal Shader Converter; 581 commits; no license) | Meshlet culling renderer, no LOD | Task-shader culling per meshlet: last-frame visibility bit, bounding-sphere frustum, normal cone, and in the late variant an HZB test with the min of four texel samples; instance-level frustum and HZB in a compute pass; two PSO sets, early and late. Self-reported: "~2000 sponzas in view at ~19 ms/frame on Macbook M4 Pro at full resolution" with about 90% of geometry culled, one G-buffer pass and diffuse shading. `TODO.md` lists "why is object level occlusion culling so slow?" and "fix cone culling". The README says Metal 4 was "practically unusable... due to a lack of GPU debugging support" before a later update. |
| `AmelieHeinrich/Pocketcat` (Swift, Metal 4, M3+; 24 stars; no license) | ICB-driven mesh-shader visibility buffer; stalled before culling and LOD | A compute kernel encodes `draw_mesh_threadgroups` commands into an indirect command buffer (indirect mesh ICBs are an Apple9 feature); the per-instance visibility test is the constant `true`; the visibility target is `rg32Uint` with meshlet and primitive packed in one channel; builds LOD chains 0 to 4 and renders LOD0. No numbers. |
| `ximhear/metal-mesh` (Swift, Metal 3 mesh shaders, Apple7 and later; created 2026-09-05; `.claude/skills` committed) | Complete cluster-LOD DAG, flat cut and two-pass HZB in about two days | Own CPU quadric simplifier, groups of at most 4 meshlets, locked borders, monotone error as `max(simplification error, child error)`; the object shader runs one thread per meshlet over the whole model (selection rule `projected(self) <= threshold < projected(parent)`), then frustum, cone, two-pass HZB; a unit test asserts the two-pass image equals the no-occlusion image pixel for pixel. Bunny: 7 levels, 69,451 to 277 triangles, 310 ms build. Culled fractions only; no GPU times. Records Metal facts: the object-stage threadgroup limit bounds meshlets per model; the depth texture needs `shaderRead` and must not be memoryless to feed Hi-Z. |
| `Hopelimb/MeshletSandbox` (Metal 4, D3D12, Vulkan from one `ShaderTypes.h`; `.claude/skills` and `CLAUDE.md` committed) | Teaching sample from a Japanese conference talk | Distance-picked discrete LOD, frustum and cone culling; its own chapter says DAG and decimation are not implemented. |
| `wukangmh2022-cmyk/nanite-moltenvk` (DiligentCore on MoltenVK; one-time publish 2026-09-12, four commits; no license file) | Claims a full Nanite-like chain with hand-written MSL for 64-bit atomics | SPIRV-Cross cannot emit 64-bit atomics, so the author patches the engine to load `.metal` source with `atomic_max_explicit` on a `ulong`; M1 runs a 32-bit two-pass fallback, M2 and later the native path (the author's statement). The traversal shader is a persistent-thread queue, and its comments record two Apple-specific precautions: queue counters written by other workgroups are re-read through an atomic operation because a plain load may never observe the write, and SIMD-group reductions are avoided in divergent control flow. Read in source, not run. |
| `tomicz/tomicz-ai-game-engine` (C++20, Metal 4, Slang, own RHI and render graph; 331 commits in four to five days; README "AI-native") | Closest architectural sibling to Luminex; stops before clusters | Per-instance GPU frustum cull with discrete LOD chains and a two-phase scheme using a depth pyramid; no clusters. |
| three.js examples (PRs #33605 merged 2026-05-21, #33783 merged 2026-06-24) | Browser WebGPU compute rasterizer with 64-triangle chunks | Discrete LOD, compute software raster plus indirect hardware draw, 32-bit packed atomic max visibility buffer. Self-reported fps in PR comments: 129,600 DamagedHelmet instances, M2 Pro 92 fps flat and 24 fps volume before a LOD fix, 63 and 30 after, 80 and 38 with tuning; an M1 reported 32 and 15 fps. No resolution. An existence proof on Apple GPUs through the browser's translation layer, not a controlled measurement. The body of PR #33783 introduces its technical description with "Here's what Claude has to say". |
| wgpu PR #9640 (open, 3,190 lines) | Metal multi-draw-indirect through indirect command buffers | Self-reported: below about 512 draws the per-draw loop is cheaper on every Apple GPU measured; the crossover sits between 256 and 1024 draws on A10X through M4. A maintainer asked for benchmarks and whether the code is LLM-written. |

Apple's own samples are a different class: "Adjusting the level of detail using Metal mesh
shaders" is tied to WWDC22 session 10162, requires Apple7/Mac2, and is a discrete-LOD sample where
an object shader picks primitive and vertex counts, not a cluster hierarchy; "Encoding indirect
command buffers on the GPU" frustum-culls per object and encodes one draw per thread.

### 3.10 Engine proposals and 2026 arrivals

| Item | State on 2026-10-01 |
|---|---|
| Godot: proposal #2793 "Implement virtualized geometry rendering" | Open since 2021-05-27; no maintainer commitment; in 2024 and 2025 the Bevy author offered the meshlet code as a reference. |
| Godot: proposal #6109 mesh streaming (maintainer design, 2023-01-16) | Open; fixed video-memory page pool culled on the GPU; no code. |
| Godot: PR #88934 mesh shaders in RenderingDevice (Vulkan, D3D12) | Open since 2024-02-27, active 2026-09-18; no Metal path; maintainer comment that it targets custom shaders, not engine use. |
| Godot: personal fork PR claiming cluster LOD (2026-09-28) | Self-merged in a fork (`Ackustik1990/godot` PR #1), 18 commits; the branch is named `claude/...` and the body ends with a `claude.ai/code/session` link; not reviewed upstream; not built. |
| O3DE: `Gems/Meshlets` (merged 2022-06-09 as a proof of concept) | Exists; its PR text frames it as a first step toward GPU-driven rendering "ala Epic Nanite". |
| O3DE: draft PR #20064 "Wdmeshlets backport" (opened 2026-08-31) | Draft, mergeable state dirty, +27,496 / -3,627 lines in 231 files; DispatchMesh meshlets, amplification-shader cluster culling, persistent HiZ with two-phase occlusion, meshoptimizer DAG with "a flat per-cluster screen-space-error cut, no traversal", always-resident coarse set with CPU LRU paging; every feature off behind cvars; no performance numbers; reviewer notes list AMD and DX12 driver pitfalls; tested on Windows and Ubuntu only. |
| Unity 6.2+ "Mesh LOD" | Discrete LODs stored in the original mesh's index buffer; not virtualized geometry. |
| Unity community (UNanite, UnityNanite, VirtualGeometry, DELTation "Virtual Mesh") | All DX12 or DX11 Windows. UNanite (RTX 3060, 1080p, self-reported): 12.9 ms versus 94 ms for stock MeshRenderers on a 82 M-triangle rock scene (best case, no hand LODs), and 8.1 ms versus 13.3 ms (Unity Mesh LOD) versus 49.5 ms (no LOD) on a 249 M-triangle quarry. The UNanite author built virtual shadow maps and a virtual texture and shipped both switched off because "they don't make the frame faster" in the test scenes. |
| `mogmog-0110/cluster-lod-renderer` (D3D12; about 30 hours, 26 commits, 2026-07-14 to 07-15) | Clean-room build log with an objective pass criterion for each milestone (zoom sequence with no cracks, pixel-identical images versus no occlusion, zero-diff software versus hardware raster). Self-reported on an RTX 5070 Ti at 720p with the D3D12 debug layer on: 88.6 B source triangles at 131,072 instances, 9.5 M drawn, 0.51 ms GPU frame; software versus hardware raster on 8,192 dragons with triangle density varied through the error threshold (0.57 M visible triangles: hardware 0.031 ms, software 0.044 ms; 1.3 M: about equal; 10.69 M: 0.326 versus 0.227 ms, software 1.4x faster; the generated `docs/BENCH.md` records 0.217 ms for the last cell), so the crossover lies between 1.3 M and 4.4 M visible triangles at 720p. The author notes cone culling was deliberately skipped, that the HZB test must use a mip where the rectangle spans at most 4x4 texels or boundary clusters oscillate, and that orbit-camera captures never reach a steady state. The 0.51 ms figure is a mostly-culled instanced scene of three meshes, not a general frame time. |
| CuRast (CUDA, arXiv 2604.21749, April 2026) | NVIDIA-only three-stage software raster; self-reported RTX 5090: 400 M triangles in 7.98 ms at 1080p; Zorah 13.5 B triangles in view in 67.3 ms at 4K; for models with large triangles "Vulkan remains 10x faster". Raster evidence only. |

## 4. Synthesis

### 4.1 The common architecture skeleton

Nearly every project that got past meshlets has these stages, in this data-flow order:

1. **Offline meshlet build**: meshoptimizer `buildMeshlets*` (64 to 256 vertices, 124 to 128 triangles),
   bounding sphere and optionally a normal cone. Everything in the survey except RavEngine and the
   Forge's per-triangle design starts here.
2. **Offline hierarchy build**: group clusters (topological plus spatial adjacency), lock group
   borders, simplify to about half, re-split, repeat until one root; error and sphere bounds made
   monotone up the DAG. Every DAG builder in the survey except the two hand-written ones (metal-mesh,
   Carrot until 2026-03) uses meshoptimizer primitives, and since v1.0 (2025-12-08) five projects
   (Solis, Carrot, light-system, nanite-moltenvk, cluster-lod-renderer) plus NVIDIA's sample call
   `clusterlod.h` as a library. NVIDIA retired its own builder in its favor.
3. **Runtime cut**: a per-cluster or per-group test of projected error against about 1 px, using the
   monotone parent error so each cluster decides alone. Flat scan over all clusters (ximhear,
   O3DE PR, early Bevy and Nyx) or a BVH over groups (Bevy 0.17, Nyx, vk_lod_clusters, Solis,
   cluster-lod-renderer).
4. **GPU culling**: instance, then node or group, then cluster; frustum, previous-frame depth
   pyramid; cone culling is common but contested (Bevy dropped it, cluster-lod-renderer skipped it
   citing Epic's talk, vk_lod_clusters does primitive backface culling only under NV mesh shaders). Two-phase occlusion (draw what was
   visible, rebuild the pyramid, draw the rest) in Bevy, niagara, Nyx, RealEngine, metalrender,
   metal-mesh, Solis, cluster-lod-renderer, vk_lod_clusters.
5. **Indirect submission of visible clusters**: mesh-shader dispatch (Nyx, NVIDIA, Carrot, RealEngine,
   metalrender, Pocketcat) or compute-written indirect draw arguments (Bevy, Solis, niagara's fallback,
   light-system).
6. **Visibility buffer or direct G-buffer**, then a material pass: 64-bit packed `depth | id`
   (Bevy, Nyx, Carrot, Forge, cluster-lod-renderer), 32-bit ID with a separate depth (Wicked, Pocketcat),
   or no visibility buffer (Solis, niagara, Synapse, metalrender, vk_lod_clusters).
7. **Optional last**: software raster (Bevy, Forge, nanite-webgpu, cluster-lod-renderer, partial
   stubs elsewhere) and streaming (Nyx, Solis, NVIDIA, O3DE PR, light-system opt-in).

### 4.2 Order built, durations, where projects stalled

Most common order: renderer base, GPU instance culling and indirect draws, meshlets, hierarchy (days
after meshlets when a library or METIS is used), previous-frame HZB, two-phase occlusion, mesh
shaders, visibility buffer, GPU hierarchy traversal, streaming, software raster last. This is
closest to Nyx's history and is not a rule. Carrot wrote its visibility buffer before it had
clusters; Bevy's first merged prototype had a visibility buffer and two-pass occlusion a month
before it had LOD; metal-mesh built two-pass HZB before its DAG; cluster-lod-renderer built a 64-bit
visibility buffer, two-pass HZB and software raster in the same 30 hours as the DAG; and NVIDIA
shipped streaming in its first release.

| Project | Meshlets to LOD DAG | HZB / two-phase | Hierarchy traversal | Streaming | Software raster |
|---|---|---|---|---|---|
| Bevy | about 7 months from first commit (2023-09-30, author's statement) to DAG LOD merged 2024-04-23; hardware-raster prototype merged 2024-03-25 | two-pass in the 2024-03-25 prototype, before LOD; LOD-compatible rework 5 days after DAG | month 21 (BVH, 2025-06-29) | never | month 11 |
| Nyx | 6 days (2025-10-30 to 11-05) | 21 days to HZB, 36 to two-phase | 58 days authored, 69 committed | 77 days | not built |
| Solis | 5 days | "New Two Pass occlusion culling" commit about 10 weeks after DAG | by 2026-04-20 | 2026-04-20 | not built |
| nanite-webgpu | 3 days | HZB 2 weeks, no two-phase | one thread per meshlet | not built | last, 2 days |
| Carrot | about 3 weeks (usable quality after Feb to Mar 2024) | not built | GPU cut 2024-02-25 | not built | not built |
| `vk_lod_clusters` | month 0 | two-pass month 11, reject lists month 19 | month 0 | month 0 | stub, month 10 |
| light-system | scripted via `clusterlod.h` | diagnostics only | abandoned day 23 | opt-in | not built |
| cluster-lod-renderer | same session | within 30 hours | BVH within 30 hours | not built | within 30 hours |
| metal-mesh | under one day | within one day | flat scan only | not built | not built |
| Wicked, Forge, RealEngine | no DAG | Wicked and RealEngine single or two-phase | n/a | none | Forge only |

Where projects stall or stop, with evidence:

- **Simplification quality and the error metric** is the most cited difficulty: nanite-webgpu's FAQ
  and simplification-tests; Carrot's bad early LOD, a 4-minute Happy Buddha build and the 2026
  switch to `clusterlod.h`; Bevy's statement that DAG quality "is the most important part" and its
  bunny going from 7 to 12 levels; light-system's city hierarchy being "threshold-degenerate" until
  partitioning used a position remap. Projects that adopt `clusterlod.h` get past this stage in
  days, on the three timelines the commit histories expose: Solis went from first meshlets to a
  `clodBuild` hierarchy in five days, cluster-lod-renderer did it inside its 30-hour build, and
  Carrot replaced its two-year-old builder in six days (2026-03-22 to 03-28). light-system and
  nanite-moltenvk also call the header but show no usable timeline, and no adopter publishes a
  quality comparison. (metal-mesh reached a DAG in under a day with its own simplifier, on small
  models.) Remaining quality problems are
  content-specific: foliage and sparse geometry erode (NVIDIA's Zorah note, UNanite keeping artist
  LODs for foliage, zeux's border dilation in v1.3).
- **Hierarchy traversal** is the next wall once instance and cluster counts grow: Bevy's flat
  per-cluster dispatch overflowed a 25-bit ID at about 33.5 M clusters and was replaced after 21
  months; metal-mesh and the O3DE PR still cost O(total clusters) per frame by design.
- **Streaming** is absent or opt-in in most projects: Bevy never built it in three years; Carrot,
  nanite-webgpu, cluster-lod-renderer, metal-mesh and lighthugger have none; the working
  implementations (Nyx, Solis, NVIDIA, O3DE PR) all use a GPU request list read back to the CPU and
  group- or page-granular uploads, at least one frame of latency, and an always-resident coarse set.
- **Software rasterization** was built by a minority (Bevy, the Forge, nanite-webgpu,
  cluster-lod-renderer, nanite-moltenvk and the three.js example, plus claims in UNanite); Nyx, Solis,
  Carrot, Wicked, RealEngine and niagara did not, and NVIDIA's version is a stub. Its payoff is
  contested: NVIDIA reports it "not faster" for typical cluster sizes, the Bevy author advises
  skipping it until last, cluster-lod-renderer measured 1.4x only at 10 M visible triangles on an
  RTX 5070 Ti, and nanite-webgpu needed a lossy 32-bit workaround for lack of 64-bit atomics.
- **Materials**: no project implements Nanite-style material bins or classification of the
  hardware kind; Bevy loops over materials with fullscreen triangles, Nyx and Carrot use a single
  ubershader resolve, Wicked uses compute tile classification plus per-shader-type bins (the closest
  analogue). Alpha test and skinning are the first features cut or disabled (Nyx dropped both for
  four to six weeks; Bevy never supported alpha mask or skinning).
- **Maintenance horizon**: lighthugger (one month), nanite-webgpu (finished), Solis (moved to GI after
  11 weeks), Bevy (burnout, then two contributors finished the BVH). Nyx, Carrot, NVIDIA, meshoptimizer
  and Wicked sustain daily or weekly work.
- **Platform ports**: the one engine-level Metal port attempt stalled on 64-bit atomics, a GPU hang
  without a debugger and hour-long builds. Later projects avoid it: they target Apple8/9 and use
  `ulong` atomic max, accept 32-bit packing, or skip software raster and visibility-buffer atomics
  entirely.

### 4.3 Measured numbers

Almost nothing here is comparable across rows: scene, resolution, what was timed and GPU differ,
and every number is self-reported unless marked. Read each row as a single data point.

| Project | Scene | Hardware | Resolution | What was timed | Result | Status |
|---|---|---|---|---|---|---|
| Bevy 0.14 | 3,092 bunnies, about 445 M tris | RTX 3080, base clocks | 2240x1260 | visibility pass, no shading | 2.78 ms | self-reported blog |
| Bevy 0.15 | 3,375 bunnies, about 486 M | same | same | visibility pass | 4.97 ms then 0.93 ms | self-reported blog |
| Bevy 0.15 | 847 cliff instances | same | same | visibility pass | 2.32 ms (cull 1: 1.27 ms) | self-reported blog |
| Bevy 0.17 | 130 K dragons, 115 B tris | "the 4070" | not stated | full render | 3.5 ms | self-reported release notes |
| Bevy 0.17 | over 1 M dragon instances, about 900 B | same | not stated | full render | 4.5 ms | self-reported release notes |
| Bevy 0.17 | 1,300 instances, 0.16 versus 0.17 code | same | not stated | full render | 2.2 ms then 1.3 ms | self-reported release notes |
| zeux on `vk_lod_clusters` | Zorah | RTX 3050 | not stated | frame | raster 16 ms, ray tracing 26 ms | author-reported blog |
| RTXMG README | Zorah, 56 M unique / 778 M instanced in frame | RTX 5090 | 4K DLSS Quality | whole path-traced frame | 15.5 ms | self-reported README; not geometry-only |
| Nyx | Zorah, 1.64 B unique | RTX 4070 Ti SUPER | not stated for the trace (README: 4K for its screenshots) | one Nsight frame | 6.94 ms (5.94 active); main mesh pass 2.88 ms | self-reported; partly reproducible (Windows, prebuilt cache) |
| cluster-lod-renderer | 3 scanned meshes, 131 K instances, 9.5 M drawn of 88.6 B | RTX 5070 Ti, debug layer on | 720p | GPU frame | 0.51 ms | self-reported; script shipped |
| SynapseEngine paper | 1 M instances of 12 meshes | RTX 4060 | not stated | culling, unlit | 124 ms to 0.653 ms | self-reported paper |
| Forge VB2 | not named (note: about 3 M tris) | MacBook M2 | 1080p | triangle processing / bin raster / shading / total async | 11.3 / 10.9 / 1.8 / 13.7 ms | self-reported slide |
| Forge VB2 | same | iPhone 15 Pro | 1704x786 | total async | 19.3 ms | self-reported slide |
| Forge VB2 | same | RX 7600 | 1080p | total async | 3.8 ms | self-reported slide |
| light-system README | Dragon 871 K / city 1 M | Apple M4, MoltenVK | 720p | median frame | 15.4 / 30.6 ms (own logs: 8.3 to 191 ms) | self-reported; inconsistent; CPU-side selection |
| metalrender | about 2,000 Sponzas in view, 90% culled | M4 Pro | "full resolution" | frame (G-buffer + diffuse) | about 19 ms | self-reported README; not reproducible |
| three.js example | 129,600 helmets | M2 Pro / M1 (browser) | not stated | fps | 24 to 92 / 15 to 32 fps | self-reported PR comments |
| UNanite | 82 M / 249 M tris | RTX 3060 | 1080p | GPU frame | 12.9 vs 94 ms; 8.1 vs 13.3 vs 49.5 ms | self-reported |
| CuRast | Venice 400 M / Zorah 13.5 B in view | RTX 5090 | 1080p / 4K | raster | 7.98 / 67.3 ms | self-reported arXiv README; CUDA |
| Carrot | Bistro | unnamed NVIDIA GPU | not stated | frame | about 80 FPS, lighting-bound | self-reported blog |

Builder and memory numbers (more reproducible, from zeux's repos): Zorah 1.64 B triangles
processed in about 3m45s on a 16-core Ryzen 7950X (9m20s a year earlier); geometry residency
996 to 678 MB with error clamping; meshlet codec about 6.5 bits per triangle; NVIDIA's own builder
reports about 6 minutes on a 16-core Ryzen 9. Other builders: Carrot's own simplifier 4 minutes to
40 seconds on the Happy Buddha; metal-mesh 310 ms on the bunny.

### 4.4 What is rare or missing publicly

- **Native Metal measurements.** No project publishes GPU pass times for cluster culling and raster
  with device, scene, resolution and method on a native Metal path. The nearest items are the Forge's M2
  slide (compute-rasterized triangles, no clusters, scene unnamed), metalrender's single M4 Pro frame
  time without resolution or per-pass split, light-system's MoltenVK runs with CPU-side selection,
  and browser numbers for three.js. Wicked Engine, the one mature engine with a Metal 4 mesh-shader
  path, publishes no Metal numbers.
- **Mesh shaders on Apple silicon at scale**: none of the Nanite-like projects runs hierarchical
  traversal plus mesh shaders on Metal. The Metal 4 evidence is Wicked's backend (meshes work,
  indirect-count dispatch missing), Pocketcat's ICB-driven mesh draws (M3+) and metalrender's
  task-shader culling.
- **Persistent-thread traversal on tile-based GPUs**: Nyx and Solis use cross-workgroup queues that
  rely on forward progress; NVIDIA's changelog says its persistent kernel "has off-spec behavior",
  keeps it as the default on NVIDIA hardware only, and added a multi-pass mode on 2026-04-20. No
  project publishes a measurement of such a kernel on Apple GPUs. The one that runs it there,
  nanite-moltenvk, only records precautions in shader comments (section 3.9; read, not run), and
  the Apple-constraints notebook reports that Unreal's Metal RHI turns persistent-thread culling
  off ([apple-metal-geometry-constraints.md](apple-metal-geometry-constraints.md)). Bevy (one
  indirect dispatch per BVH level) and RTXMG (one per tree level, up to the maximum depth) use
  level-synchronous multi-pass traversal.
- **A Slang-to-Metal data point for any of this.** Nyx uses Slang but targets D3D12;
  tomicz's engine uses Slang on Metal 4 but has no clusters. Whether Slang-emitted MSL handles
  64-bit texture atomics, the Apple atomic-synchronization defect or mesh-shader dispatch the way
  the other shader paths do is **[UNVERIFIED]**.
- **Streaming postmortems**: nobody has written one. The designs are described; the latency, stall and
  thrash behavior under a camera cut is not measured (RTXMG documents only that requests over budget are
  dropped and the picture converges over several frames).
- **Material binning**, **frame-time tables with a fixed methodology**, and **independent
  reproductions** of any figure above.
- **Authorship signals**: six 2026 items carry a visible signal of agent assistance, each named in
  its entry: light-system (`AGENTS.md`, an audit register, a `worktree-agent` merge commit),
  metal-mesh and MeshletSandbox (a committed `.claude/skills` folder), the tomicz engine (README
  "AI-native", over 330 commits in five days), the Godot fork PR (branch name and session link) and
  three.js PR #33783 (description introduced as an assistant's). The metalrender README states that
  its engine layer is "heavily AI agent written" after six months of hand-written renderer work.
  None of this says anything about quality; the point is only that a feature list in a days-old
  repository is unverified until a number or a test shows it, which holds for every project here.

### 4.5 Best reading for a Metal 4 plus Slang build

Three codebases and a short stage index:

1. **meshoptimizer `clusterlod.h` with `vk_lod_clusters`** (builder, cut rule, hierarchy, streaming).
   `demo/clusterlod.h` is the builder; `docs/lod_generation.md`, `docs/streaming.md`,
   `docs/raster_twopass_culling.md` and the traversal shaders are the best-documented
   runtime. Take the hole-avoidance rule from `traversal_run_groups.comp.glsl`. Read RTXMG's HLSL
   `traversal_run.hlsl` for the non-persistent multi-pass version, which is the one that ports.
   Caveat: GLSL with NVIDIA-specific extensions in the ray-tracing half.
2. **Nyx** (whole chain in Slang). `MeshletBuilder.cpp`, `DAGCull.slang`, `VBufferMesh.slang`,
   `ResolveVBufferToGBuffer.slang`, `GeometryStreaming.cpp`: the only small project that is
   Slang-based, MIT, active, and covers builder, BVH, two-phase cull, mesh shader, visibility buffer
   and paging. Caveats: persistent-thread queue, 64-bit image atomics from the pixel shader,
   D3D12 root signatures, and a published number that is a scene statistic.
3. **Bevy's meshlet module with the author's blog posts** (design record, culling, resolve, Metal
   pitfalls). Read `cull_bvh.wesl`, `cull_clusters.wesl`, `cull_shared.wesl` (level-synchronous
   traversal, no persistent threads, the occlusion footprint rule), `resolve_render_targets.wesl` and
   `visibility_buffer_resolve.wesl`, and the three virtual-geometry posts for what failed, why, and the
   cost of each stage. It also has the best-documented Metal failures (Apple9 atomics, the
   texture-atomic write-drop defect); Wicked's issues #1479 and #1597 are the other record.

By stage: offline builder, `clusterlod.h` plus the zeux posts plus Bevy's builder posts; two-phase
culling on Metal, metalrender (task shader, Metal IR), metal-mesh (object shader, MSL) and niagara
(canonical); ICB-driven mesh draws, Pocketcat; Metal 4 backend facts, Wicked issue #1479 and wgpu PR
#9640; visibility-buffer shading on Apple GPUs, Wicked's tile classification and the Forge slides;
software-raster tradeoffs, the Forge binning design, cluster-lod-renderer's crossover table and the
Bevy 0.15 post; failure modes of a Metal port, the ue5-nanite-macos README and wgpu PR #9185.

## 5. Corrections to earlier research

The earlier survey ([open-source-references-2024-2026.md](../2026-09-14-roadmap-review/open-source-references-2024-2026.md))
records README claims at one line each. Corrections found by reading the code:

1. **Nyx**: license is MIT (was marked unverified). The "18.9B instanced triangles at 4K/144Hz"
   caption is a scene-graph count equal to NVIDIA's published Zorah figure; the only timing artifact is
   one Nsight frame of 6.94 ms. "Active through 2026-09-14" is accurate only for branch `VSM`; `main`'s
   last commit is 2026-07-16.
2. **Solis**: no license file (was marked unverified). The "10 GB scene" is one Zorah copy as glTF; the
   README puts the baked form at about 70 GB per copy; no timings; Zorah is not in its release assets. The "compute fallback" is compute-written indirect
   draws, not software raster. A Metal 4 backend is a stated plan.
3. **light-system**: described as Nanite-class with GPU hierarchical selection and visibility-buffer
   rasterization; its own status document says selection and culling are on the CPU, the visibility
   buffer is diagnostic, and stock Godot is faster on its city scene. Its README numbers differ from
   its later logs.
4. **SynapseEngine**: LOD is a discrete per-mesh ladder; "1M+ entities" means instances of 12 meshes;
   "virtualized shadow mapping" is a 4K atlas; license is AGPL-3.0 plus commercial.
5. **vk_lod_clusters and RTXMG**: RTXMG is the HLSL port of `vk_lod_clusters`, not its companion; its
   cluster LOD arrived in v2.0.0 (2026-08-28), is ray tracing only, and its license is the NVIDIA RTX
   SDKs License (source-available), not open source. The 15.5 ms figure is a whole path-traced frame.
   `vk_lod_clusters` runs meshoptimizer's `clusterlod.h` directly; the NVIDIA library repositories are
   archived or gone.
6. **niagara**: not a cluster-LOD implementation; discrete LOD chain of up to 8 meshlet meshes. It remains
   the reference for two-phase culling.
7. **meshoptimizer**: the latest release is v1.3 (2026-09-25), not v1.2 (the 2026-09-14 survey row).
   The release dates in
   [pipeline-state-of-the-art-m7-m11.md](../2026-09-14-roadmap-review/pipeline-state-of-the-art-m7-m11.md)
   (v0.24 2023-06-12, v1.0 2023-12-08, v1.1 2024-04-02, v1.2 2024-06-30) are each exactly two years too
   early; the releases API gives v0.24 2025-06-12, v1.0 2025-12-08, v1.1 2026-04-02, v1.2 2026-06-30.
   `meshopt_partitionClusters` first appeared (experimental) in v0.23 (2025-03-14), not v0.24; v0.24
   added `meshopt_buildMeshletsSpatial` and position input for partitioning.
8. **Bevy**: the "Metal limited or no support" row came from the multi-draw-indirect notes; for meshlets
   the specific state is Apple9-class 64-bit texture atomics (per wgpu), an M1 startup panic, and a 2026
   Apple atomic-synchronization workaround. The "0.17: 2.2 ms to 1.3 ms" figure compares 0.16 and 0.17 on
   a 1,300-instance scene. The 0.19.0 tag is dated 2026-06-18 in the releases API (the earlier note said
   06-19).
9. **Wicked Engine**: Metal is Metal 4 only and recent (macOS merged 2026-01-11, iOS 2026-06-03); the
   mesh-shader path is off by default and meshlets plus the visibility buffer are an ID-packing and
   culling scheme over discrete LODs.
10. **The Forge**: "active, Release 1.63" is stale. GitHub has had only README commits since 2025-03-19;
    Release 1.64 (2026-08-12) exists only on Codeberg and is DX12-only.
11. **ue5-nanite-macos**: last push 2023-05-24; the 2024-08-16 date is unconfirmed (probably the archive
    date). The 32-bit atomics replacement was never integrated into the engine; the debug views showed a
    sphere, not correct rendering, and then froze the machine.
12. **RealEngine**: now has an active Metal backend (not mentioned earlier). **RavEngine**: no change; idle
    since 2025-07.
13. **O3DE**: "no meshlet or virtual-geometry evidence" is wrong: `Gems/Meshlets` has existed since 2022 and
    draft PR #20064 (2026-08-31) adds cluster DAG, two-phase HZB and paging. **Godot**: "no mesh-shader
    support found" is wrong: PR #88934 has been open since 2024-02-27; the ray-tracing plumbing PR #99119
    merged 2026-01-27 as stated.
14. **Apple's "Adjusting the level of detail using Metal mesh shaders"** sample is a WWDC22 discrete-LOD
    sample (Apple7/Mac2 and later), not a cluster hierarchy or a Metal 4 reference.
15. **Standing claim that native-Metal GPU-driven numbers are close to absent** stands, with the
    amendments listed in section 4.4.

## 6. Implications for Luminex

Facts first, then options. The recorded device is an M3 Max (Apple9 family).

### 6.1 Apple facts that bound the stage choices

- Apple's feature tables (dated 2026-05-21) map M1 to Apple7, M2 to Apple8, M3 and M4 to Apple9, M5 to
  Apple10. Mesh shading starts at Apple7. "Indirect mesh draw arguments" and "indirect command buffers
  containing mesh draws" start at Apple9. "64-bit atomics" are listed at Apple9, with footnote 7:
  "GPU devices in the Apple8 family support 64-bit atomic minimum and maximum using ulong, on both
  buffers and textures, only on macOS. The full set of 64-bit atomic operations is supported on all
  platforms starting with Apple9." The phrase "the full set" must not be read as add, exchange or
  compare-exchange: the Apple-constraints notebook compiled probes on an Apple9 machine (M3 Max) and
  found that the Metal Shading Language defines only minimum and maximum for 64-bit atomics on
  every family, so what Apple9 adds over Apple8 is the same two operations on all platforms instead
  of macOS only. `RG32Uint` can be a `ulong` atomic texture only on
  a GPU that supports the 64-bit atomics feature. Maximum threadgroups per mesh-shader grid is 1,024 on
  Apple7 and Apple8, 1,048,575 on Apple9 and 4,194,303 on Apple10; mesh payload is 16 KB on all.
  *(Apple, Metal Feature Set Tables PDF, 2026-05-21; extracted and read 2026-10-01.)* The other
  notebook ([apple-metal-geometry-constraints.md](apple-metal-geometry-constraints.md)) owns the
  full constraint analysis and local probes.
- Consequences: a visibility buffer that needs 64-bit atomic max (the pattern in Bevy, Nyx, Carrot,
  the Forge) has a hardware floor of M2 on macOS by Apple's table (min/max on buffers and textures);
  Apple9 extends the same min/max to Apple's other platforms and adds no further 64-bit operation. An
  atomic max is all that pattern needs. wgpu gates its 64-bit texture-atomic feature at
  Apple9, stricter than the table. M1 has none, which is the generation that defeated the 2022
  Unreal port and the generation on which Turner's tests failed. The 1,024-threadgroup mesh-grid
  limit on Apple7 and Apple8 bounds how many mesh threadgroups one grid launches; in an
  object-plus-mesh design that caps the clusters one object threadgroup can spawn (metal-mesh hit
  this as a cap on meshlets per model), and how it applies to mesh-only indirect draws is for the
  Apple-constraints notebook. GPU-written mesh draw arguments and ICB mesh draws are an
  M3-and-later feature, which fits M9's "optional mesh-shader execution on M3/A17 Pro and later"
  wording.

### 6.2 What the survey says about the accepted M9 scope

- The M9 text (offline cluster LOD through a maintained library, GPU culling, ordinary indirect cluster
  raster over the M7 path, optional mesh shaders, compare visibility-buffer and deferred, publish a
  Metal measurement table; defer streaming and software raster) agrees with the survey on the builder
  and the deferrals and differs from it on three points of order. Agreement: Nyx, Solis, Carrot,
  cluster-lod-renderer and metal-mesh all reached a working cluster cut with hardware raster before
  any software raster, and only cluster-lod-renderer of those five went on to build one; Bevy, Carrot,
  cluster-lod-renderer and metal-mesh never built streaming, although Nyx and Solis added it 10 to
  11 weeks after their first meshlets and NVIDIA shipped it at the start. Differences (section 4.2):
  (1) M9 makes compute-written indirect draws the primary raster path and mesh shaders optional,
  whereas mesh shaders are the primary path in Nyx, Carrot, NVIDIA's sample, RealEngine and
  metalrender, and indirect draws are primary only in Bevy and light-system and a fallback in Solis
  and niagara; (2) M9 treats the visibility buffer as a later comparison, whereas Carrot built it
  before clusters, Bevy's prototype had it a month before LOD, cluster-lod-renderer built it with
  the DAG and Nyx added it within six weeks of its DAG; (3) M9 does not name two-phase occlusion,
  whereas Bevy and metal-mesh had it before their DAG, cluster-lod-renderer with it and Nyx a month
  after. No project measured one order against another, so this shows what others did, not that
  M9's order is wrong.
- The library question is largely settled in the open corpus: meshoptimizer's `clusterlod.h` (MIT) is
  what Solis, Carrot, light-system, nanite-moltenvk, cluster-lod-renderer and NVIDIA's sample use, and
  on the three timelines that can be read (Solis, cluster-lod-renderer, Carrot's swap) integration
  took days, not months. The effort sits elsewhere: Bevy's 21 months to hierarchical culling,
  nanite-webgpu's remark that culling and meshlets absorb the time, and Carrot's three and a half
  months from its first visibility-buffer commit to GPU-side selection (one month of that from CPU
  to GPU selection) point at the runtime and culling as the cost.
- The Luminex M7 visibility path is a single-phase previous-frame HZB with no same-frame recovery. The
  survey shows the typical next steps are second-phase recovery (among Metal-native or Metal-adjacent
  projects with depth-pyramid occlusion, metalrender, metal-mesh, the tomicz engine, nanite-moltenvk
  and RealEngine use two phases; Wicked's optional mesh-shader path tests against the previous
  frame only) and a hierarchy over groups once cluster counts grow (flat scans were the first thing
  Bevy and Nyx replaced). Both are separable slices.
- The visibility buffer comparison in M9 has public prior art on Metal only in fragments: Wicked
  (32-bit ID, compute tile classification, no published numbers), the Forge (64-bit buffer, M2
  numbers), Pocketcat (rg32Uint target with mesh shaders). A paired VB versus deferred measurement on
  native Metal would be new public data.
- Foliage: the San Miguel alpha-masked foliage is the likeliest place for cluster simplification to
  visibly fail (NVIDIA's Zorah vegetation note, UNanite keeping artist LODs, zeux's border dilation in
  v1.3 for exactly this). Alpha test is also where others cut scope: Nyx disabled it for about six
  weeks and Bevy's meshlet path supports opaque materials only. A slice should
  decide a foliage policy before claiming parity.

### 6.3 Options and tradeoffs (not a plan)

- Slice shape suggested by how others sequenced the same work: (a) offline builder with a
  monotonicity self-check and a CPU reference cut (cluster-lod-renderer and metal-mesh use zoom
  sequences and pixel-identity tests as pass criteria); (b) GPU flat cut with indirect raster over the
  existing path; (c) second-phase occlusion; (d) hierarchy traversal; (e) visibility-buffer comparison;
  (f) mesh-shader variant on Apple9; (g) streaming and software raster deferred as listed.
- Traversal style: persistent-thread queues (Nyx, Solis, NVIDIA's default on its own hardware) rely on
  forward progress that NVIDIA's changelog marks off-spec and nobody has measured on Apple GPUs;
  level-synchronous multi-pass traversal (Bevy, RTXMG, NVIDIA's multi-pass mode) costs one dispatch
  per tree level but is the safer first port.
- Software raster: evidence for a benefit is thin and mixed (NVIDIA "not faster", Bevy "skip until
  last", 1.4x only at micro-polygon density on one GPU), and the Metal prerequisite is Apple8 macOS or
  Apple9. Keeping it deferred is defensible; if a slice is added, the Forge's binned design has the only
  Apple timing.
- Metal-specific validation items borrowed from others' failures: the fragment-shader texture-atomic
  write-drop defect (wgpu PR #9185, Blender; reproduced by wgpu with a 32-bit atomic texture, affected
  GPUs and OS versions not stated) for any design that writes visibility with texture atomics from
  hardware raster; the missing
  mesh-shader-without-pixel-shader and `SV_PrimitiveID` limits through the shader converter (Wicked); the
  texture-atomic format requirement (`RG32Uint` as a `ulong` texture); the indirect-command-buffer
  multi-draw cost crossover between 256 and 1,024 draws (wgpu #9640, open, self-reported).
- A fixed measurement methodology (device, scene, resolution, pass list, warm-up, paired runs) is
  already a Luminex strength and is exactly what the survey finds missing.

## 7. Open questions and what could not be confirmed

- No number in this notebook was reproduced; no project was built or run. Every performance figure is
  self-reported or author-reported, and Bevy's 0.17 resolution and GPU variant, light-system's idle reruns
  and Nyx's resolution and camera path are unstated.
- The Apple texture-atomic defect: neither wgpu PR #9185 nor the Blender commit names the GPU models,
  GPU families or OS versions affected, and neither explains the cause; "tile-based" is the wgpu
  author's attribution. Which hardware and macOS versions show it, whether a later OS fixes it, whether
  it is limited to fragment shaders (Blender's comment says any shader), and whether Slang-generated
  MSL needs the same dead-write workaround are all unconfirmed.
- Bevy's first-commit date (2023-09-30) is the author's statement, not an API date; the durations
  measured from it inherit that. The Nyx hierarchy-cull commit carries two dates (authored
  2025-12-27, committed 2026-01-07). Solis's 2026-04-24 commit is titled "New Two Pass occlusion
  culling", so an earlier two-pass form may predate it.
- The Bevy author's "M2 or newer" for meshlets versus wgpu's Apple9 gating for `TEXTURE_INT64_ATOMIC`:
  not tested on either generation; Apple's table says Apple8 has 64-bit min/max on macOS only.
- niagara on macOS, RealEngine's mesh-shader path on Metal, and nanite-webgpu on Safari or macOS Chrome
  are unverified. nanite-moltenvk's 64-bit `atomic_max` path was read, not run.
- The M2 configuration (core count, memory) behind the Forge slide, and the scene behind it, are not
  stated.
- Archive date of ue5-nanite-macos; peer-review status of the SynapseEngine paper; Magnum, sokol and
  Unigine proposal searches (rate limited, so unconfirmed rather than negative).
- How Epic handles M1's missing 64-bit atomics on the Metal SM6 path was not traced
  ([nanite-system-integration.md](nanite-system-integration.md)).
- Reddit and Hacker News threads from Metal developers about cluster-LOD builds in 2025 and 2026 were
  searched for but none was found; Reddit pages were not fetched directly.
- I did not watch the niagara stream videos; any on-stream frame times are not included.

## 8. Sources

All fetched or read 2026-10-01.

**Repositories (GitHub API reads of README, docs, source, commits, releases):**
`bevyengine/bevy` (`crates/bevy_pbr/src/meshlet/`, issue #11518, PRs #10164, #12755, #12898, #14623,
#16947, #17765, #19318, #21301, #22603, #23307, #25796, #25801, #25928, #25953), `gfx-rs/wgpu`
(`wgpu-hal/src/metal/adapter.rs` on `trunk`, PRs #9185, #9640), `nvpro-samples/vk_lod_clusters`,
`nvpro-samples/nv_cluster_builder`, `nvpro-samples/nv_cluster_lod_builder`, `NVIDIA-RTX/RTXMG`,
`zeux/meshoptimizer` (`demo/clusterlod.h`, `demo/nanite.cpp`, releases, discussion #750),
`zeux/niagara`, `moonlovelj/Nyx`, `Vovan675/Solis`, `usestemframework/light-system`,
`expenses/lighthugger`, `Scthe/nanite-webgpu`, `Scthe/nanite-simplification-tests`, `jglrxavpok/Carrot`,
`TamasPetii/SynapseEngine`, `turanszkij/WickedEngine` (issues #1479, #1597, PRs #923, #1237, #1639),
`ConfettiFX/The-Forge`, `zhaijialong/RealEngine`, `RavEngine/RavEngine`, `philipturner/ue5-nanite-macos`,
`tonadr1022/metalrender`, `AmelieHeinrich/Pocketcat`, `AmelieHeinrich/agfx`, `ximhear/metal-mesh`,
`Hopelimb/MeshletSandbox`, `wukangmh2022-cmyk/nanite-moltenvk`, `tomicz/tomicz-ai-game-engine`,
`mogmog-0110/cluster-lod-renderer`, `anomal3/UNanite-Demo`, `m-schuetz/CuRast`,
`mrdoob/three.js` (PRs #33605, #33783), `o3de/o3de` (PRs #9832, #20064, issue #19238),
`godotengine/godot` (PR #88934, #99119), `godotengine/godot-proposals` (#2793, #6109, #6822).

**Pages and documents:**
- Apple, Metal Feature Set Tables (PDF dated May 21, 2026): https://developer.apple.com/metal/Metal-Feature-Set-Tables.pdf
- Bevy 0.17 release notes: https://bevy.org/news/bevy-0-17/ ; 0.14 notes: https://bevy.org/news/bevy-0-14/
- Bevy author's blog (site https://jms55.github.io/; repository `jms55/jms55.github.io`, `content/posts/`): posts dated 2024-06-09, 2024-08-30, 2024-11-14, 2025-03-27, 2025-09-03 and 2026-08-29
- zeux: https://zeux.io/2025/09/30/billions-of-triangles-in-minutes/ and https://zeux.io/2026/09/30/billions-of-triangles-redux/
- Carrot blog (repository `jglrxavpok/jglrxavpok.github.io`, `_posts/`): https://jglrxavpok.github.io/2024/04/02/recreating-nanite-runtime-lod-selection.html , https://blog.jglrxavpok.eu/2024/05/13/recreating-nanite-mesh-shader-time.html , https://blog.jglrxavpok.eu/2024/08/21/recreating-nanite-raytracing.html
- Wicked Engine retrospective: https://turanszkij.wordpress.com/2024/12/10/wicked-engines-graphics-in-2024/
- The Forge I3D 2024 slides (pptx, text extracted): http://www.conffx.com/I3D-VisibilityBuffer2.pptx ; Codeberg: https://codeberg.org/The-Forge/The-Forge
- Scthe materials notes: https://www.sctheblog.com/blog/nanite-materials-notes/
- SynapseEngine paper (Git LFS, repository path `Docs/Papers/`); DELTation Virtual Mesh posts: https://deltation.com/blog/virtual-mesh-00 (and -01, -02)
- Unity Mesh LOD manual: https://docs.unity3d.com/6000.3/Documentation/Manual/lod/mesh-lod-introduction.html
- Apple sample code: https://developer.apple.com/documentation/metal/adjusting-the-level-of-detail-using-metal-mesh-shaders ; https://developer.apple.com/documentation/metal/encoding-indirect-command-buffers-on-the-gpu
- Unreal Engine source (Unreal Engine EULA; mechanisms only): `Engine/Config/Mac/DataDrivenPlatformInfo.ini` on branches `release` and `ue5-main`, read 2026-10-01
- Hacker News item 42975705 (Bevy author on WebGPU limits)
