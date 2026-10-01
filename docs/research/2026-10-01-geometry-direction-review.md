# Geometry direction review: Nanite in current Unreal source and a cluster-geometry path on Metal

**Status**: Frozen — non-normative  
**Research date:** 2026-10-01

This review answers three questions the owner raised while UX5 was in progress: how close the
accepted M9, "Geometry LOD and surface-path experiments" in
[GPU-Driven Hybrid Rendering](../roadmap/gpu-driven-hybrid-rendering.md), is to the Nanite system
in the latest Unreal branch; what the essence of the geometry state of the art is; and how the
roadmap should change, including whether M8 and M9 should be renumbered. It consumes twelve
evidence notebooks under [`2026-10-01-geometry-review/`](2026-10-01-geometry-review/) and extends
the [2026-09-14 direction review](2026-09-14-rendering-direction-review.md). The
[roadmap](../roadmap.md) owns milestone identifiers, boundaries and order; section 9 below is a
proposal for that document, not a decision.

## Evidence standard

- **Primary**: a vendor, engine, standards, paper, slide or source-code page read during this
  review. **Reported**: press, forum or blog coverage without primary confirmation.
  **[UNVERIFIED]**: carried without a confirming source; recheck before a plan depends on it.
- **Unreal source.** Three notebooks read `EpicGames/UnrealEngine` on branches `ue5-main`,
  `ue6-main` and `5.8` (release 5.8.3) on 2026-10-01. That source is licensed under the Unreal
  Engine EULA. The notebooks describe mechanisms in their own words and cite paths and branches;
  they reproduce no engine code, shader text or comments.
- **Credit.** Nanite, Virtual Shadow Maps and Lumen are the work of Epic Games. Unreal Engine
  source is copyright Epic Games, Inc.; Unreal, Unreal Engine and Nanite are its trademarks. What
  this review and its notebooks know about Nanite's current design comes from that source and
  from Epic's talks and documentation.
- **Local probes.** Three sets of measurements ran on the recorded machine (Apple M3 Max, macOS
  26.7, Xcode 26.6): a cluster-hierarchy build over the repository's Sponza and San Miguel, Metal
  and Slang capability probes, and a synthetic raster micro-probe. Each is labeled a local probe
  where it is quoted. None is a renderer benchmark.
- **Checking.** Every notebook was fact-checked against its sources by a second pass after it was
  written, the local probes were re-run, and corrections were made in the notebooks. Numbers that
  come from a size model and not from a measurement are labeled as models.

Absence from the notebooks is not evidence of absence. Where notebooks disagreed, this review
states the reconciled reading.

## Summary

1. **M9 is a valid subset of Nanite and is not close to it.** M9 as accepted covers a library-built
   cluster hierarchy, a flat GPU cull and hardware raster, which is comparable to running Unreal
   with its compute rasterizer off and every mesh fully resident. It omits or defers hierarchy
   traversal, same-frame occlusion recovery, the software rasterizer, the visibility buffer as the
   shading input, multi-view culling and streaming, which are the parts that make Nanite
   "virtualized".
2. **Nanite's core invariant is small and can be checked on the CPU.** One error value and one
   sphere per cluster, one per group, and two comparisons at run time give crack-free cuts. A
   maintained open library builds that hierarchy: meshoptimizer's `clusterlod.h` built San Miguel
   (5.6 M triangles) in about 7 s on one core with bit-identical output across runs (local probe).
3. **The 2021 talk no longer describes the shipped defaults.** Persistent-thread traversal has
   been off by default since 5.4, the full-screen per-material draws were deleted in 5.5 in favor
   of compute shade binning, and the fallback for platforms without 64-bit atomics was removed in
   2022. The constants (128 triangles per cluster, groups of 8 to 32) are unchanged since 5.0.
4. **`ue6-main` adds no new Nanite primitive, hierarchy format or rasterizer.** No Epic page uses
   the term "Nanite v2", which the previous review attributed to the Unreal Engine 6 announcement.
5. **The accepted roadmap's hardware floor is wrong as worded.** Mesh shaders and ray tracing are
   available on every Metal 4 device. M2 adds the 64-bit atomic min and max a software rasterizer
   needs; M3 adds hardware acceleration and GPU-sized mesh draws.
6. **"Ordinary indirect cluster raster" needs a definition.** Metal has no
   multi-draw-indirect-count, and an indirect command buffer costs about 692 bytes per indexed
   command (local probe). Shipped engines draw visible clusters with one draw per pipeline state,
   through fixed-topology instancing or a GPU-compacted index buffer; the RHI supports both today.
7. **On Apple GPUs a forward pass already shades opaque pixels about once.** Apple describes
   hidden surface removal as a per-pixel visibility buffer. A software visibility buffer can still
   pay for pixel-scale triangles and alpha-tested foliage, and no public measurement of either
   exists on an Apple GPU. M9's "classification" experiment has no content for a renderer with one
   material model; two cheap probes should come first.
8. **Triangle cluster LOD does not solve foliage.** The meshoptimizer author, Epic's 2021 slides
   and Bevy's author all say so, and Epic answered it in 5.7 with a voxel representation that
   needs the software rasterizer. San Miguel's alpha-masked geometry is 26% of its triangles.
9. **No current scene is geometry-bound.** Timed GPU passes total about 0.8 ms for Sponza and 4.4
   to 4.7 ms for San Miguel at 1280×720 (M7.3 validation record). Cluster geometry needs dense
   content before it can show anything; two CC0 scans of 1.3 M and 1.5 M triangles plus a seeded
   instanced field would supply it.
10. **Native-Metal numbers for any of this are nearly absent in public.** One 2021 vendor blog on an
    M1, one forum thread and one 2024 slide with M2 compute-raster times are nearly the whole
    corpus. A measured table from the recorded machine remains the undersupplied contribution the
    previous review identified.

## 1. Nanite in current source

Details and sources: [core architecture](2026-10-01-geometry-review/nanite-core-architecture.md),
[extensions](2026-10-01-geometry-review/nanite-geometry-extensions.md) and
[system integration](2026-10-01-geometry-review/nanite-system-integration.md).

### 1.1 The static opaque pipeline

| Stage | What `ue5-main` does |
|---|---|
| Offline build | Triangles are partitioned into clusters of at most 128 triangles and 256 vertices (METIS). Clusters are grouped 8 to 32 at a time by shared boundary; each group is merged, simplified to about half with its boundary vertices locked, and split into new clusters; this repeats to a single root. Epic's own quadric simplifier does the reduction |
| Stored invariant | Each cluster stores its own error and a sphere; each group stores a parent error and sphere. Every parent made from one group carries the same values, error never decreases toward the root and spheres nest |
| Encoding | One position grid per mesh, bit-packed attributes, about 17 bits per triangle in memory and 5 on disk; 32 KB root pages are always resident, 128 KB pages stream; a compute pass transcodes pages on install |
| Culling | Instances are culled in chunks, then a 4-ary hierarchy over groups is traversed with one indirect dispatch per level, then clusters are culled; every record carries a view index, up to 4096 views per pass |
| Occlusion | Everything is tested against the previous frame's depth pyramid with previous transforms and drawn if it passes; what failed is recorded, a new pyramid is built, and only the recorded items are tested again and drawn |
| Raster | A cluster whose projected edges are under 32 pixels goes to a compute rasterizer that writes depth and identity with one 64-bit atomic max; the rest goes to hardware raster, which has a vertex-shader variant that needs no mesh shaders |
| Visibility buffer | 32 bits of depth, a 24-bit visible-cluster index and an 8-bit triangle index |
| Shading | Pixels are binned by material in compute and each material shades its pixels in one indirect compute dispatch into a G-buffer, with attributes and texture gradients reconstructed analytically |

Because every cluster's "good enough" test and its children's "parent not good enough" test use
the same two numbers, exactly one side passes for any view. Each cluster decides alone and in
parallel, and boundaries between drawn clusters match because the only shared edges are locked
ones. That is the whole crack-free argument, and it can be verified offline.

### 1.2 What changed since the 2021 talk

| Topic | 2021 talk | Current source |
|---|---|---|
| Hierarchy traversal | Persistent threads | One dispatch per level by default since 5.4; the persistent mode is optional and Unreal's Metal backend forces it off |
| Material shading | Full-screen draws per material with a material-depth target | Compute shade binning: off in 5.2 and 5.3, default in 5.4 except on Metal and Vulkan, the old path deleted in 5.5 |
| Platforms without 64-bit atomics | A lock-buffer fallback | Removed on 2022-04-20; the shader does not compile without 64-bit image atomics |
| Instance culling | Flat list | A chunked spatial hierarchy first |
| Programmable raster | Absent | Masked, pixel-depth-offset and vertex-offset materials run inside both rasterizers |
| Cluster, group and page constants | 128, 8 to 32, 128 KB | Unchanged |

### 1.3 Beyond static opaque meshes

| Extension | First release | State in 5.8.3 | Needs the compute rasterizer |
|---|---|---|---|
| Vertex offset and masked materials | 5.1 | On | No |
| Spline meshes | 5.3 | On | No |
| Tessellation and displacement | 5.4 | Compiled in, labeled experimental | Yes |
| Skinned meshes | 5.5 | On, never labeled production-ready | No |
| Assemblies and voxel clusters (Nanite Foliage) | 5.7 | Off by default, experimental | Voxels: yes |
| Curves (hair strands) | 5.8 | Off by default | Yes |
| Translucency | 5.8 | Off by default | No, hardware raster into forward translucency |

Epic has labeled none of these production-ready. Tessellation, voxels and curves exist only in
the compute rasterizer with a visibility buffer, so they are out of reach for any renderer that
has neither. `ue6-main` differs from `ue5-main` by a scene-data refactor, ray-tracing work and
skinned-LOD fixes; every extension-specific file has the same size on both.

### 1.4 How Nanite couples to the rest of Unreal

- **One service, many views.** Virtual shadow maps, classic cascades and atlases, Lumen card
  capture and editor selection all call the same cull-and-raster entry with an array of views and
  a depth-only or visibility target.
- **Deferred only.** Opaque Nanite is disabled when forward shading is on. The translucency path
  is the one case where clusters decoded in the vertex stage feed a forward-lit pixel shader.
- **Ray tracing sees a proxy.** The default is a fallback mesh built offline at a fixed error. A
  stream-out of a fixed-error cut is experimental, and cluster acceleration structures work only
  through NVIDIA's API on D3D12.
- **Mac.** Nanite needs macOS 15 and an M2 or later because of 64-bit texture atomics, runs
  hardware raster through the vertex-shader path with mesh shaders switched off, and is Beta.
- **Streaming rests on three format facts**: a hierarchy that is always resident, fixed-size pages
  with dependency lists, and a patchable "leaf of the resident cut" state.

## 2. How close is M9

| Nanite component | In M9 as accepted | Note |
|---|---|---|
| Cluster hierarchy by group, lock, simplify, split | Present in concept | Through a maintained library; the crack-free invariant is not stated as a requirement or a gate |
| Monotonic error and the two-comparison cut | Not mentioned | "Runtime selection/transition" could also mean discrete LOD with blending |
| Per-cluster frustum and occlusion culling | Present | Over the M7 visibility path, which has single-phase, previous-frame occlusion |
| Same-frame occlusion recovery | Not mentioned | M7.4 deferred it beyond M7 |
| Hierarchy traversal and an instance hierarchy | Not mentioned | "GPU cluster culling" reads as a flat pass |
| Multi-view culling | Not mentioned | Shadows follow in M8 |
| Hardware raster by vertex pulling | Present | "Ordinary indirect cluster raster", undefined for Metal |
| Mesh-shader raster | Present, optional | Epic leaves it off on Mac |
| Compute rasterizer and 64-bit atomic visibility writes | Deferred | |
| Visibility buffer as the opaque path, binned shading | An experiment | Compared against compact deferred |
| Encoding, pages, streaming | Deferred | To M11 |
| Fallback mesh and a non-cluster reference | Present | |
| Published measurements | Present | Epic publishes none for Apple hardware |

**Verdict.** M9 delivers the offline half of Nanite's central idea and the simplest runtime that
can consume it. That is a real cluster-LOD renderer. It is not close to Nanite's defining
behavior, which is pixel-scale triangles at a cost independent of scene size: of the six parts
that produce that behavior (hierarchy traversal, two-pass occlusion, the software rasterizer,
visibility-buffer shading, multi-view culling, streaming), M9 names one as an experiment and
defers or omits the rest.

RE Engine is the closest shipped analogue to M9's shape in reverse: it ships meshlets, two-phase
occlusion, a visibility buffer and a software rasterizer in two games with no automatic cluster
LOD. Cluster culling and cluster LOD are separable in practice.

## 3. Building it outside Epic

Details and sources: [construction](2026-10-01-geometry-review/cluster-lod-construction.md),
[culling and rasterization](2026-10-01-geometry-review/cluster-culling-and-rasterization.md) and
the [open implementations survey](2026-10-01-geometry-review/open-implementations-survey.md).

### 3.1 The offline build is a library call

meshoptimizer 1.3 (2026-09-25, MIT) ships `clusterlod.h`, a single header that builds the whole
hierarchy. NVIDIA archived its own two cluster libraries and points to it. Five open renderers
call it, and those with a readable history reached a hierarchy within days of having meshlets.

Local probe, default configuration, one thread:

| | Sponza | San Miguel |
|---|---|---|
| Source triangles | 262,267 | 5,608,441 |
| Build time | 0.32 s | 7.2 to 7.6 s |
| Clusters, groups, deepest level | 4,863 / 395 / 10 | 129,048 / 9,257 / 14 |
| Stored triangles over all levels | 1.99× | 1.98× |
| Triangles in groups that never get a coarser level | 0.44% | 0.22% |
| One-pixel cut at 1080 lines: far, mid, inside | 62 K / 109 K / 226 K | 94 K / 595 K / 2.55 M |

- **Determinism.** Output was bit-identical across runs and optimization levels, and an x86-64
  build matched arm64 once floating-point contraction was disabled. `-ffast-math` changes it. A
  bake can therefore be pinned by hash with one compiler flag and one thread per mesh.
- **Stuck simplification is the real risk.** With the library's permissive mode and sloppy
  fallback both off, 25% of San Miguel's triangles ended in groups that never simplify.
- **The error metric multiplies triangles drawn.** Including normals at weight 0.5 raised San
  Miguel's one-pixel cut by 1.5 to 5 times against a position-only error. Epic reported about a
  person-year on its metric and called the weights a heuristic.
- **Levels are not complete by construction.** A group that keeps more than 85% of its triangles
  stops at its own depth, so "level k" is that depth's groups plus all shallower stopped groups.
  A bake that hands closed per-level meshes to a later ray-tracing milestone has to construct and
  verify them.
- **Current content cannot show the technique.** From inside Sponza a one-pixel cut keeps 86% of
  the triangles.

### 3.2 The runtime is where the effort goes

Bevy took 21 months from first commit to hierarchical traversal and has no streaming after three
years. The typical order in the surveyed projects was instance culling and indirect draws,
meshlets, the hierarchy, previous-frame then two-phase occlusion, hierarchy traversal, streaming,
and software raster last or never. Several built a visibility buffer and two-phase occlusion
before or alongside their hierarchy, and most use mesh shaders as their primary raster path, which
the Metal evidence in section 4 does not support here.

- **Flat cluster culling is enough at this scale.** The per-cluster test is independent, so a
  hierarchy is an accelerator. Bevy's flat pass ran about 2 to 3 × 10¹⁰ tests per second on an RTX
  3080 and broke between 10 and 30 million candidate cluster-instances per view. San Miguel has
  129 thousand clusters in one instance. When a hierarchy is needed, per-level dispatch is the
  portable form and is Unreal's and Bevy's default.
- **Per-cluster occlusion needs a second phase.** Every surveyed system that occludes clusters
  against previous-frame depth either pairs it with same-frame recovery, at 5 to 20% of the
  visibility pass, or calls its single-phase mode a defect. With clusters, camera motion opens holes
  inside continuous surfaces on most frames. Frustum and LOD tests have no temporal input and do not
  force it.
- **Raster back ends.** One draw per visible cluster needs a CPU command per cluster on Metal.
  Frostbite, id Tech 7 and Activision compact surviving indices into one buffer and issue one
  draw; RedLynx and Bevy draw a fixed vertex count per cluster with instancing. Mesh-shader
  evidence is mixed: Alan Wake 2 gains 1.4 ms on an RTX 4090, Wicked Engine found them slower in
  real scenes, and a 2022 report on an M1 measured a twofold loss.
- **Software raster.** Nanite reports about three times the hardware rate on small triangles and
  Capcom 2.4 times. Alan Wake 2 and id Tech 8 list it as future work, NVIDIA's sample calls its
  own version no faster for typical cluster sizes, and Bevy's author advises leaving it until
  last. It pays when triangles approach one pixel.

## 4. What Metal 4, Apple GPUs and Slang allow

Details, probe sources and commands: the
[Metal constraints notebook](2026-10-01-geometry-review/apple-metal-geometry-constraints.md).

| Capability | M1 | M2 | M3 and later | From Slang at the pinned compiler |
|---|---|---|---|---|
| Mesh and object shaders | Yes | Yes | Yes, hardware-accelerated | Yes, with authoring rules below |
| GPU-sized (indirect) mesh draws, mesh grid above 1,024 threadgroups | No | No | Yes | Not applicable |
| 64-bit atomic min and max, buffers and textures | No | Yes on macOS | Yes | Only through an intrinsic workaround |
| Primitive ID and barycentric fragment inputs | Yes | Yes | Yes | Yes |
| Ray tracing API | Yes | Yes | Yes, hardware intersector | Inline queries compile |
| Multi-draw-indirect with a GPU count | No | No | No | Not applicable |
| Cluster-level acceleration structures | No | No | No | Not applicable |
| Framebuffer fetch, memoryless targets | Yes | Yes | Yes | Fetch yes; tile shaders and imageblocks no |
| Placement sparse buffers, fast file-to-buffer loading | Yes | Yes | Yes | Not applicable |

- **Slang traps (local probes, reproduced on re-run).** `InterlockedMax` on a 64-bit value
  compiles in Slang and fails in the Metal compiler; an eight-line target intrinsic works and
  gives correct results at run time. `SV_CullPrimitive` is silently dropped. A varying struct
  shared by a mesh and a fragment entry point in one invocation, which is the repository's build
  shape, fails to link. The pinned compiler miscompiles compound mesh primitive indices; the
  release that fixes it also changes `SV_InstanceID` to subtract the base instance, which the
  scene shaders and an RHI test rely on.
- **Silent failures.** A mesh grid dimension of 65,536, or a total of 1,048,576 threadgroups,
  draws nothing with no validation message.
- **A driver defect to probe first.** wgpu and Blender report fragment-shader texture-atomic
  writes being dropped on Apple GPUs. Any design in which hardware-rasterized fragments write a
  visibility target by atomics needs its own check.
- **Mesh shading and ray tracing do not share a pipeline.** Metal's feature tables say ray tracing
  in a render pipeline is not compatible with mesh shading.
- **Raster micro-probe (synthetic, trivially shaded, one draw, 1920×1080).** A mesh shader cost
  about the same as one indexed draw. A naive compute rasterizer with 64-bit atomic max was 1.7 to
  2.6 times faster than hardware raster at or below about 1.3 pixels per triangle and slower at
  about 10. This says the technique is feasible here and where its crossover lies; it is not a
  renderer result.

## 5. The surface path on a tile-based GPU

Details and sources: [visibility buffer and surface
paths](2026-10-01-geometry-review/visibility-buffer-and-surface-paths.md).

- **Apple's hidden surface removal is a hardware visibility buffer.** Apple's own session uses
  those words, calls a performance-only depth pre-pass redundant on its GPUs, and in 2019
  positioned the software visibility buffer as the technique for hardware without tiling.
- **Two costs survive it.** Fragments are still shaded in 2×2 quads per triangle, so small
  triangles run helper threads; Apple's shading-language specification documents the mechanism,
  and its cost on Apple GPUs is unmeasured. Alpha-tested fragments must run their shader before
  visibility is known, and Luminex's masked shader does the discard and the full lighting in one
  function.
- **Desktop and console evidence agrees on the shape.** A visibility buffer gives nothing on large
  triangles, 20 to 27% on dense scenes (id Tech 8) and about a factor of two at one pixel per
  triangle (Hable). Decima applied it to foliage only and cut pixel-shading work to 40% and 25% of
  its earlier depth-equal method.
- **Most shipped systems resolve into a G-buffer** (Nanite, Decima, id Tech 8, RE Engine) and
  stay hybrids. Indiana Jones stayed Forward+ and writes a small G-buffer from the forward pass.
- **For Luminex the blocker is texture access, not classification.** With one material model
  there is nothing to classify. A single resolve needs every material's textures reachable from
  one invocation; the RHI binds five textures per draw through 16 slots.
- **The reconstruction module is not optional work**, by the notebook's inference. A ray hit arrives
  as an instance, a primitive and barycentrics, and shading it needs the same attribute
  interpolation, explicit texture gradients and indexed material textures as a visibility resolve.
  M10 needs it whichever opaque path wins.
- **Guides for M8.3 can be an extra attachment of the forward pass.** One of the three extra color
  targets is free. M8 does not have to wait for a surface-path decision.

The cheap evidence comes first: a timing sweep of the existing forward pass over triangle size
(full against trivial fragment shader at fixed covered pixels), and an alpha-only depth control
for masked materials on San Miguel. Neither needs a new path. The Metal API on the recorded
machine exposes only timestamp counters, so the sweep measures time, not invocation counts.

## 6. Coupling, extensions and adjacent techniques

Details and sources: [system integration](2026-10-01-geometry-review/nanite-system-integration.md),
[compression and streaming](2026-10-01-geometry-review/geometry-compression-and-streaming.md) and
[adjacent representations](2026-10-01-geometry-review/beyond-triangle-clusters.md).

| Consumer | What it needs from cluster geometry | Shape the first slices? |
|---|---|---|
| Sun and local-light shadows | Many views in one cull, a depth-only output, LOD in shadow texels, a rectangle per view | Yes: a view index in every record and a depth-only mode, since adding them later changes record layouts |
| A page-cached shadow atlas | Cluster-granular culling against a page mask | Only a static/dynamic instance class and a reserved per-cluster rectangle field; the page logic belongs after cluster culling, since Epic's own path for ordinary geometry is the costly case |
| Ray queries | A conventional proxy mesh per asset at a stated error, and a raster-versus-ray LOD mismatch view | Yes, in the bake only |
| Streaming | Group addressing, a residency table read by selection, a resident coarse level, fixed-size pages | Yes, in the file format only |
| Transparency | A hardware path that can run the forward pixel shader | No; translucent clusters are a rule for whichever of M8.4 and cluster culling lands second |
| Motion vectors and TAA | Previous transform per instance | No change |

- **Cascades and an atlas do not need cluster geometry.** M8 and the geometry work can be ordered
  either way. This is the notebook's reading of Unreal's design, not a measured result.
- **Current content needs no streaming for capacity.** The whole hierarchy doubles triangle
  count. By a size model, San Miguel's hierarchy is of the order of 160 MB packed against 349 MB in
  the current plain format. The format decisions that are cheap now are group addressing, a
  residency table with a forced non-resident test mode, and level tags.
- **Foliage has cheaper complements.** Octahedral impostors and merged far-field proxies are
  mature and independent of cluster geometry.
- **Out of scope on Metal today:** cluster acceleration structures, voxel far fields, micro-meshes
  (deprecated by NVIDIA in 2025), and learned geometry (nothing shipped).
- **No owner in the roadmap:** skinning. Skinned clusters would be ordinary on a hardware path,
  and Luminex has no skinning at all.
- **Splats stay independent research,** better split into a splat layer, which needs M8.4's
  transparency contract, and hybrid LOD selection, for which no recipe exists.

## 7. Content and measurement

Details and license texts:
[test content and measurement](2026-10-01-geometry-review/test-content-and-measurement.md).

- **Fixtures.** Two Poly Haven CC0 photogrammetry assets, `coast_rocks_02` (1.26 M triangles, 38.5
  MB) and `coastal_cliff_04` (1.54 M triangles, 46.5 MB), fit the existing pinned-fetch pattern. A
  seeded instanced field of them reaches about a billion nominal source triangles with shared
  storage before a flat cluster pass is expected to saturate (section 9.2). Amazon Lumberyard Bistro
  (CC BY 4.0, 1.44 GB) is the optional many-material scene; San Miguel remains the foliage failure
  case.
- **License traps.** The Stanford scans are research-only with images allowed in scholarly
  publications; Emerald Square is non-commercial share-alike; the Intel Sponza archive carries a
  personal-and-educational header with its CC BY text; Megascans acquired under an Unreal Engine
  plan remain Unreal-only.
- **San Miguel stays the foliage scene under its existing record.** The archive's `info.js` says
  CC BY 3.0 and the enclosed `license.txt` says free for research and educational use with
  attribution. `THIRD_PARTY_NOTICES.md` already preserves both and leaves the question open, so
  this is a known condition on publishing San Miguel images, not a new finding.
- **Counters must be shader-written.** The device exposes no statistic counters through the API,
  so survivors per culling stage are atomic counters read back at retirement, as the visibility
  path already does.
- **LOD output cannot pass an exact-image gate against the ordinary path.** The gates that have held
  in this project are set equality against a CPU mirror, counter reconciliation, an independent ID
  oracle and exact images with temporal reconstruction off. A cluster path keeps all of them and
  adds one exact check: a cut forced to zero error equals the ordinary path (whether that can be
  bit-exact is untested). For coarser cuts the selection rule is itself a tolerance, since a cluster is drawn only when
  its stored error projects under the pixel threshold. That gives three checkable gates: at bake
  time, each group's stored error bounds its measured position deviation from the source surface;
  at selection, the GPU cut equals the CPU mirror; end to end, the rendered cut's screen-space
  geometric deviation from the full-detail reference stays within a declared multiple of the
  threshold. Quadric error is an estimate and not a strict bound, so the factors are empirical and
  must be declared before the scored measurement. Color difference and flicker have no comparable
  bound and are better reported than gated.

## 8. Corrections

### 8.1 To the accepted roadmap text

| Text | Finding |
|---|---|
| "Mesh shaders and hardware ray tracing require Apple M3/A17 Pro or later" (Part II hardware floor and M10, Part V, roadmap entry) | The APIs run on every Metal 4 device. M3 adds hardware acceleration, indirect mesh draws and the large mesh grid; M2 adds 64-bit atomic min and max |
| M9: "ordinary indirect cluster raster" | Undefined on Metal. It should name a single-draw back end chosen by measurement |
| M9: "Extend MipLab" | No MipLab exists in the tree; MaterialLab holds the only mip probe |
| M9: compare "material reconstruction/classification" | Classification has no content for one material model; the measurable questions are small-triangle cost, masked overdraw and reconstruction overhead |
| M7.4: two-phase occlusion "stays beyond M7" | Still true, and per-cluster occlusion requires it, so it needs an owner |
| M8: a page-cached atlas is eligible "since the technique is documented down to Apple M2" | That floor is Nanite's, which Epic rates Beta on Mac; it says nothing about a page cache without cluster culling |
| M10: "Slang's Metal target does not yet generate ray-tracing code" | Inline ray queries compile to Metal at the pinned compiler. Ray-tracing pipeline stages remain unimplemented |

### 8.2 To earlier research

Each notebook lists its own corrections. The ones that changed a conclusion:

- "Unreal Engine 6 with Nanite v2 and Lumen 2.0" has no Epic source. The announcement, its date
  and the end-of-2027 Early Access target are confirmed; the names are not.
- The meshoptimizer release dates in the pipeline notebook are two years early. `clusterlod.h`
  shipped with v1.0 on 2025-12-08.
- Wicked Engine's 2024 retrospective shades tiled forward with its ID buffer feeding guide
  reconstruction; its 2026 source adds compute tile-classified shading from that buffer, so the
  notebooks disagree on whether it is a second visibility-buffer shading design.
- Nanite's two-pass occlusion keeps no list of previously visible objects, and its visibility
  payload is a visible-cluster index and a triangle index, not an instance.
- Space Marine 2 is instance-level culling, not cluster evidence, and Alan Wake 2 culls in compute
  before its mesh shaders.
- Bevy's hierarchical culling shipped in 0.17; its meshlets need 64-bit texture atomics.

## 9. Proposal for the roadmap

These are recommendations for the owner. The roadmap parts change only through their own edits.

### 9.1 Renumbering

No technical reason prevents it. M8 and M9 appear about 170 times on about 110 lines in 21 files;
67 of those are in frozen research, 14 in accepted milestone records and two in an accepted ADR,
none of which may be edited. No tag, plan or source identifier uses either name (one benchmark
comment and two UX4 finding labels aside). The project already renumbered once, UX2 to UX3 on
2026-09-25, with a dated note.

- **Recommended if geometry becomes more than one milestone.** Geometry becomes a sixth roadmap
  part with its own series, G1 to G3. M9 is retired with a dated note saying that documents
  written before the change mean the pre-split geometry milestone. M8, M10 and M11 keep their
  names, so every existing pointer to them stays true, including the accepted M7 records that say
  "per-cascade culling is M8.1 scope", and no identifier runs out of order.
- **Alternative if geometry stays one milestone.** Swap M8 and M9 and add slices to the geometry
  milestone. The cost is a dated note, as for UX2 and UX3, and the M8.1 pointers in two accepted
  M7 records then name a geometry slice.

The case against a new series: it adds a part and milestones where the roadmap asks for a few
slices, and it leaves M9 as a hole in the sequence. It is the better choice only because 9.2
needs three outcomes that one milestone cannot hold. A new part file is needed in either case,
since the Part II file is at 254 of its 300 enforced lines.

### 9.2 Proposed milestones and slices

Each milestone has one outcome. Studies that may end in a recorded negative result are slices
only where a later slice depends on their decision.

| Slice | Outcome | Needs | Gate |
|---|---|---|---|
| **G1 — Cluster geometry** | Dense scenes render through a crack-free cluster hierarchy that is selected on the GPU and drawn by the existing forward path, with a published native-Metal table | M7, M6 | All four slices |
| G1.1 Dense content and forward baselines | Pinned CC0 fixtures, a seeded instanced field, shader-written counters and the measurement schema; the ordinary path's cost on that content, including the small-triangle timing sweep and the alpha-only control for masked materials from section 5 | M7 | Fixtures carry license and provenance; the baseline table and both probe results exist before any cluster code |
| G1.2 Cluster bake and cut oracle | An offline bake over the pinned library with a manifest; group and cluster records with a generating-group reference and level tags; verified complete levels; a fixed-error proxy per mesh; a CPU oracle. No GPU work | G1.1 | Rebuild is bit-identical; random cuts cover every surface exactly once with matching boundaries; error is monotonic; each group's stored error bounds its measured position deviation within a declared factor; the share of triangles in stopped groups is under a declared budget |
| G1.3 GPU cut and single-draw raster | Cluster tables; view records with a view index and a depth-only mode; a flat frustum and LOD cut with a CPU mirror; visible-cluster records indexed per frame; explicit capacities with counted overflow; a residency table with a forced non-resident mode; a single-draw back end chosen by measurement; a cluster and LOD debug view through an ID pass. Opaque only, masked materials keep the ordinary path, and clusters are not occlusion-culled | G1.2 | Visible-cluster set equals the CPU mirror for every view; a zero-error cut matches the ordinary path with temporal off; no holes with groups forced missing; overflow is safe and visible |
| G1.4 LOD quality and published measurements | A tuned error metric, measured screen-space geometric deviation against the full-detail reference, color-difference and flicker measures, and the measurement table, with instance count swept to the flat pass's measured knee | G1.3 | Geometric deviation stays within a declared multiple of each tested threshold; rasterized triangles stay within a stated factor as instance count and source complexity grow, up to the knee; color difference and flicker are reported; the table is published with its method |
| **G2 — Scalable visibility** | Culling cost follows visible detail beyond the flat pass's knee, and occlusion loses nothing within a frame | G1 | Both slices |
| G2.1 Two-phase occlusion | Reject lists for instances and clusters, a mid-frame pyramid and a second draw on the forward path | G1.3, M7.4 | No visible identity missing on the M7.4 motion rails or under a cluster oracle; second-phase and render-pass-split cost reported; a default changes only on paired evidence |
| G2.2 Hierarchical traversal | The bake's group hierarchy traversed with one dispatch per level, with budgets and overflow reporting | G1.3 | Visible set identical to the flat pass; past the knee, eight times the instances costs under 1.5 times the culling time |
| **G3 — Surface path for pixel-scale triangles** | A measured decision on shading and rasterizing pixel-scale triangles on Apple GPUs | G1; the G1.1 probes | Adopt, retain or defer, recorded per workload |
| G3.1 Indexed material textures and reconstruction | An RHI integer target and texture access beyond per-draw slots, and a shared module for barycentrics, attributes and gradients. M10 needs the same module, so whichever of G3 and M10 comes first builds it | G1 | The module agrees with hardware barycentrics and derivatives inside a declared numeric bound; RHI additions land with their conformance cases |
| G3.2 Visibility target and resolve | Hardware-rasterized cluster identity, then a resolve that shades with the clustered lights, in a tile-memory form (unprobed as a design) and a compute form, developed as an experiment. Proceeds only if the G1.1 probes show a cost a resolve could remove | G3.1 | Identity exact against an independent ID render; color difference against forward reported; paired cost on the dense fixtures |
| G3.3 Compute rasterizer study | A triangle-parallel rasterizer with 64-bit atomic depth on M2 and later, chosen per cluster by projected edge length. Color views need G3.2 adopted; a depth-only form for shadow views does not | G3.2 | The texture-atomic defect from section 4 is probed first; coverage matches hardware on a rule set; no cracks at mixed boundaries; a stated speedup on small clusters, or a recorded negative result |

Work this review places outside the series:

- **Shadow views through clusters** become part of M8.1 and M8.2: cascade and atlas views cull and
  draw cluster geometry through G1.3's view records, with the ordinary path as reference.
- **Quantized cluster pages and geometry page streaming** become the geometry slice of M11's
  residency area, after texture mips.
- **Mesh-shader execution** stays independent research after G1, as Part II already frames it. It
  needs a Slang upgrade or the authoring rules in section 4, and the micro-probe predicts a tie.

Design choices recorded for the G1 plan, each with a real alternative:

- **Vertex storage.** Two notebooks disagree. The compression notebook would quantize positions
  onto one grid per mesh in the first bake, because changing the payload later invalidates decode
  shaders and recorded evidence. The construction notebook would keep the scene's shared vertex
  pool and add cluster index data, about a third more than the source buffers. This review
  recommends the shared pool for G1. Quantizing either changes the vertices the ordinary path
  draws for existing scenes, which invalidates their accepted image baselines, or gives up exact
  parity between a zero-error cut and the reference. G1 still fixes the addressing that streaming
  needs: groups, the generating-group reference, level tags and the residency table. The quantized
  payload then lands with geometry residency in M11, which re-runs the cluster evidence.
- **Multi-material meshes.** Build one hierarchy per glTF primitive and lock positions shared
  between primitives. This keeps one material per cluster for the forward path; in the probe it
  roughly doubled the far-view cut on San Miguel and changed the near cut by 1.5%.
- **Alpha-masked geometry.** Excluded from cluster LOD in G1 and stated as a limit.
- **Flat pass first.** One fixture is 20 to 30 thousand hierarchy clusters, so a flat pass reaches
  its knee near a thousand instances, about a billion nominal source triangles. That figure is
  derived from desktop measurements and is unmeasured on Apple GPUs; G1.4 measures it.
- **No speculative format fields.** The manifest carries a format version. Fields for deformation
  wait for a skinning owner.

### 9.3 Reshapes to the remaining milestones

- **M8.1 and M8.2**: add cluster geometry in shadow views as above. Move the page-cached atlas
  from "eligible inside M8.2" to independent research after M8.2.
- **M8.3**: state that guides are extra outputs of the forward pass, and that the AO energy gate
  needs a decision on where indirect light is composited.
- **M8.4**: add that translucent clusters draw through the transparent pass once both exist.
- **M10**: require the per-mesh proxy from the G1 bake and a raster-versus-ray LOD mismatch view;
  build or reuse G3.1's reconstruction module for hit shading; correct the Slang sentence.
- **M11**: split residency into a request, install and budget substrate proven on texture mips,
  then quantized geometry pages.
- **Independent research**: add impostors and merged far-field proxies as the foliage complement,
  compute tessellation and offline displacement baking, and skinning as an unowned prerequisite
  for any deforming geometry. Split the splat item in two.
- **Hardware floor**: reword once and cite it from each part.

### 9.4 Delivery order

- **Recommended: N1 → G1 → G2 → M8 → G3 → M10 → M11.** Each shadow view multiplies the cluster
  candidates a frame must test, so G2's traversal and same-frame occlusion come before M8 adds
  cascades and atlas views. M8.3's guides come from the forward pass and do not wait for G3. G3
  then has M8's forward shading and guides as its comparison target, and its reconstruction module
  is in place for M10.
- **Alternative: N1 → G1 → M8 → G2 → G3 → M10 → M11.** Shadows, ambient occlusion and fog arrive
  one milestone sooner, and M8's shadow views run on the flat cluster pass until G2.

N1 is unchanged by this review.

## 10. Decisions requested from the owner

1. Renumbering: a geometry series G1 to G3 with M9 retired, or a plain swap of M8 and M9.
2. Scope: approve G1 to G3 as sliced in 9.2, or name the slices to cut, merge or move to
   independent research. G2.2 and G3.3 are the candidates if the series should be shorter.
3. Order: G2 before or after M8.
4. Approve the text reshapes in 9.3 and the corrections in 8.1.
5. Content: approve the two CC0 fixtures as the default fetch and Bistro as optional.
6. Publication: confirm that notebooks describing Unreal's source in their own words, with paths
   and no code, may be committed to a public repository, or keep the three Nanite notebooks local.
7. LOD quality evidence: gate on geometric deviation derived from the selection threshold, as
   in section 7, and report color difference and flicker (recommended); or report everything
   without a gate and accept by visual review, as M7.5 did for its temporal cells.

## 11. Sources

Each notebook lists every source it used, with the branch or date it was read:

| Notebook | Scope |
|---|---|
| [nanite-core-architecture.md](2026-10-01-geometry-review/nanite-core-architecture.md) | Nanite's static-mesh pipeline from current source |
| [nanite-geometry-extensions.md](2026-10-01-geometry-review/nanite-geometry-extensions.md) | Tessellation, skinning, assemblies, voxels, curves, translucency; Unreal Engine 6 |
| [nanite-system-integration.md](2026-10-01-geometry-review/nanite-system-integration.md) | Shadows, ray tracing, Lumen, materials, streaming, platforms |
| [cluster-lod-construction.md](2026-10-01-geometry-review/cluster-lod-construction.md) | Building hierarchies with open libraries; the builder probe |
| [cluster-culling-and-rasterization.md](2026-10-01-geometry-review/cluster-culling-and-rasterization.md) | Runtime in shipped engines and open projects |
| [apple-metal-geometry-constraints.md](2026-10-01-geometry-review/apple-metal-geometry-constraints.md) | Metal 4, Apple GPU and Slang limits; the capability and raster probes |
| [visibility-buffer-and-surface-paths.md](2026-10-01-geometry-review/visibility-buffer-and-surface-paths.md) | Visibility buffer against forward and deferred on tile-based GPUs |
| [geometry-compression-and-streaming.md](2026-10-01-geometry-review/geometry-compression-and-streaming.md) | Encoding, pages, streaming and residency |
| [open-implementations-survey.md](2026-10-01-geometry-review/open-implementations-survey.md) | Open Nanite-like implementations |
| [test-content-and-measurement.md](2026-10-01-geometry-review/test-content-and-measurement.md) | Dense content, licenses and benchmarking practice |
| [beyond-triangle-clusters.md](2026-10-01-geometry-review/beyond-triangle-clusters.md) | Tessellation, far fields, splats, cluster ray tracing |
| [luminex-geometry-readiness.md](2026-10-01-geometry-review/luminex-geometry-readiness.md) | What the repository offered and lacked on the research date |

Primary sources the conclusions lean on most:

- Unreal Engine source, `EpicGames/UnrealEngine`, branches `ue5-main`, `ue6-main` and `5.8`, read
  2026-10-01 under the Unreal Engine EULA.
- Karis, Stubbe and Wihlidal, [A Deep Dive into Nanite Virtualized
  Geometry](https://advances.realtimerendering.com/s2021/Karis_Nanite_SIGGRAPH_Advances_2021_final.pdf),
  SIGGRAPH 2021.
- [meshoptimizer](https://github.com/zeux/meshoptimizer) 1.3 and its `clusterlod.h`;
  [vk_lod_clusters](https://github.com/nvpro-samples/vk_lod_clusters).
- Apple: Metal Feature Set Tables (2026-05-21), the Metal Shading Language specification 4.1, the
  macOS 26.5 SDK headers, and the WWDC sessions [Harness Apple GPUs with
  Metal](https://developer.apple.com/videos/play/wwdc2020/10602/) and [Optimize Metal Performance
  for Apple silicon Macs](https://developer.apple.com/videos/play/wwdc2020/10632/).
- [Capcom, RE Engine meshlet rendering, REAC
  2025](https://enginearchitecture.org/downloads/REAC_2025_Capcom.pdf); Remedy, Alan Wake 2 geometry
  talks, REAC 2024 and Digital Dragons 2024; id Software, Doom: The Dark Ages visibility buffer,
  Graphics Programming Conference 2025.
- [Bevy virtual geometry
  posts](https://jms55.github.io/posts/2024-06-09-virtual-geometry-bevy-0-14/) and the Bevy meshlet
  module.
- [Poly Haven license](https://polyhaven.com/license).
