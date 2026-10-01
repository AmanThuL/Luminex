# Geometry Compression and Streaming

**Status**: Frozen — non-normative  
**Research date:** 2026-10-01

Evidence notebook for the [geometry direction review](../2026-10-01-geometry-direction-review.md),
covering how cluster geometry is encoded, stored, streamed and kept resident, and which of those
choices a first cluster-geometry milestone has to fix in its baked format so that the roadmap's
content-residency work (M11) is not blocked. Collected on 2026-10-01 by fetching vendor pages,
papers, slide decks and public source, reading the Unreal Engine source on the `ue5-main` branch
for mechanisms only, and running one scratch measurement program (described in "Memory
arithmetic") on the locally fetched Sponza and San Miguel scenes. Items marked **[UNVERIFIED]**
were not confirmed against a primary source; recheck them before a plan depends on them.

Credit: Nanite and the other Unreal Engine systems described here are the work of Epic Games.
Unreal Engine source is copyright Epic Games, Inc. and was read under the Unreal Engine EULA; this
notebook describes mechanisms in its own words and reproduces no engine code, shader text or
comments. Unreal, Unreal Engine and Nanite are trademarks of Epic Games, Inc.

## What matters most

1. **The position grid is the format decision that cannot be deferred; topology and entropy codecs
   can.** Nanite, DGF, DXR2's `Compressed1` and meshoptimizer's recommended scheme put positions on
   one grid per mesh with a per-cluster anchor and bit width (vk_lod_clusters gets the same
   property from a deterministic float truncation). The systems that stream (Nanite,
   vk_lod_clusters, Nyx) treat strip coding, byte-plane splitting and LZ4 as an install-time
   transcode. A per-cluster grid cracks the boundaries between clusters and between levels.
2. **Every streaming system read here streams groups or fixed pages of groups, keeps the coarsest
   level resident, and records residency in mutable data (a table, or patched records in Nanite).**
   The unit is the simplification group (so that a group's boundaries stay seamless), packed into
   128 KB (Nanite), 256 KB (Nyx) or whole-group blobs (vk_lod_clusters). Missing data never stalls a
   frame; the coarser clusters keep drawing.
3. **Built on this repository's two largest scenes, the whole LOD hierarchy holds 1.98x to 1.99x
   the source triangles (measured). A size model on top of that build puts its bytes at about 2.1x
   the leaf level, a fully packed leaf level at 4.3x to 4.6x smaller than the current
   48-byte-vertex indexed format, and the whole packed hierarchy at 2.0x to 2.2x smaller.** The
   model uses measured topology and position bits but an assumed 8 B of attributes per vertex and
   assumed header sizes, which together are about two thirds of the packed leaf figure. By that
   model Sponza needs about 6 MB and San Miguel about 160 MB as packed hierarchies. Nothing the
   project owns needs streaming for capacity; streaming is a design-for-later item, not an M9
   deliverable.
4. **No hardware or API help exists on Apple platforms.** AMD's Dense Geometry Format has no
   shipping hardware decode, NVIDIA's micro-mesh SDK is archived, and Metal offers only general-purpose
   codecs through the IO queue. Compression for Luminex is shader-side decoding plus CPU
   transcoding with the MIT-licensed meshoptimizer.
5. **Recommendation:** M9 ships resident-only but page-able (fixed-size pages, group addressing,
   one residency table, a coarse level flagged for pinning, a shared grid), with a forced
   non-resident test mode so that the no-holes behavior is verified without any I/O. Streaming
   itself follows texture-mip streaming in M11 on shared request, install and budget plumbing.

## Encodings

### Summary table

| Format | Published size | Decoded where | License / status |
|---|---|---|---|
| meshoptimizer index codec | 1 to 1.2 B/triangle typical, 1 B best case; about 2 B per index if references are sparse | CPU at load, 3 to 6 GB/s | MIT |
| meshoptimizer vertex codec | 2x to 4x smaller than already-quantized, packed data | CPU at load, 3 to 6 GB/s | MIT |
| meshoptimizer meshlet codec | 5 to 7 bits/triangle for topology; 9 to 12 bits/triangle with vertex references; 5 to 8 with a general-purpose compressor on top | CPU, 7 to 10 GB/s, into 3 or 4 B/triangle and 2 or 4 B/reference; GPU compute example reported at 150+ GB/s on an RTX 5070 (v1.1 release notes) | MIT; experimental in v1.1, stable in v1.2 |
| AMD DGF | 128 B block of up to 64 triangles and 64 vertices; 2.74 to 5.42 B/triangle for position plus topology at 11 to 16 offset bits (up to 6.95 at 24) | HLSL helpers in any shader; hardware decode only on "future" AMD GPUs; SuperCompression decodes to plain meshlets on older hardware | SDK source under an MIT-style notice |
| Kuth et al. / GPUOpen meshlet compression | 9.5 or 5.9 bits/triangle topology (versus 96 for the vertex pipeline, 24 for plain meshlets); 16-bit fixed attributes | Mesh shader, every frame | Paper and sample code |
| Mlakar et al. "laced wires" | about 16 bits/triangle connectivity; decoded per frame | Mesh shader | Paper |
| Capcom RE Engine | Sample asset 75.8 MB to 56.3 MB (meshlets) to 40.3 MB (plus attribute compaction); no per-triangle figure | Mesh, vertex or compute shaders | Closed; REAC 2025 talk |
| NVIDIA displaced micro-meshes | 0.81 to 2.38 B/triangle, as tabulated in the DGF paper | Ray-tracing hardware (Ada generation, **Reported**) | SDK and toolkit repositories archived (last push 2025-02-13); Vulkan extension withdrawn |
| Nanite | On disk after LZ: 14.4 B average per input triangle (Epic documentation, Valley of the Ancients sample); 11.4 B per input triangle, 5.6 B per stored triangle (2021 deck, slide 144). In memory: about 8.7 B per stored triangle (same slide, 7.67 GB for 882 M triangles) | Disk pages transcoded by a compute shader on install; resident bit-packed form decoded in the cluster shaders | Source-available under the Unreal EULA |
| vk_lod_clusters (NVIDIA sample) | Topology about 3x smaller with the meshlet codec; positions and UVs bit-packed per cluster; the processing cache itself is uncompressed | CPU decode in the streaming path, then upload | Apache-2.0 |

### meshoptimizer

The library is MIT licensed (copyright notice requirement only). The release history that matters
here, from the GitHub releases API on 2026-10-01:

| Release | Date | Relevant change |
|---|---|---|
| v0.7 | 2018-01-11 | Index codec (`meshopt_encodeIndexBuffer`); the vertex codec is in use by v0.9 (2018-11-12) |
| v0.15 | 2020-10-23 | The codecs become the glTF extension `EXT_meshopt_compression` |
| v0.23 | 2025-03-14 | Vertex codec version 1 (5 to 10% smaller, up to 10% faster decode); 20% faster decode on Apple Silicon; `meshopt_buildMeshletsFlex`, `meshopt_partitionClusters` (experimental) |
| v0.24 | 2025-06-12 | `meshopt_buildMeshletsSpatial`; partitioner accepts positions |
| v1.0 | 2025-12-08 | `clusterlod.h` added to the repository; vertex codec v1 becomes the default; `KHR_meshopt_compression` in gltfpack |
| v1.1 | 2026-04-02 | Meshlet codec (`meshopt_encodeMeshlet`, `meshopt_decodeMeshlet`), `meshopt_optimizeMeshletLevel`, opacity micromap rasterization |
| v1.2 | 2026-06-30 | Meshlet codec functions stable; `meshopt_computePositionExponent`; `clodBuildHierarchy` in `clusterlod.h`; MikkTSpace-class `meshopt_generateTangents` |
| v1.3 | 2026-09-25 | `meshopt_computePositionExponent` stable; voxel remesher; `meshopt_generateNormals` |

Sources: [releases](https://github.com/zeux/meshoptimizer/releases) read through the API for exact
dates, and the [README](https://github.com/zeux/meshoptimizer) for the figures in the table
above (the GPU decode rate is from the v1.1 release notes). The vertex and index codecs target whole buffers and need data optimized for vertex fetch
and cache; the meshlet codec encodes each meshlet independently, which is what makes it usable as a
per-cluster page payload. Its README says triangle data decodes into the common runtime forms (3
or 4 B/triangle) and that decoders can write directly into write-combined memory. It also states
that vertex references need a high degree of locality, which the encoder assumes after
`meshopt_optimizeMeshletLevel` at level 1 or higher (level 3 recommended).

The README section "Cluster position quantization" describes the exact scheme this notebook
recommends in the quantization section below: one exponent shared by every cluster, positions as
integers on that grid, a per-cluster anchor, and a per-cluster bit count per axis. It notes that
all clusters at all levels of detail must share the exponent and that Nanite uses up to 21 bits per
axis. It also notes that a shader can decode the bit-packed deltas only with unaligned bitstream
reads, and offers 16-bit aligned deltas as the simpler alternative.

### AMD Dense Geometry Format

DGF is a block format: each block is 128 bytes and holds at most 64 triangles and 64 vertices
([DGF.h](https://github.com/GPUOpen-LibrariesAndSDKs/DGF-SDK) constants, and the
[HPG 2024 paper](https://gpuopen.com/download/publications/DGF.pdf), PACMCGIT vol. 7 no. 3). The
first 20 bytes are a header with 24-bit signed anchors per axis and an 8-bit exponent; vertices
are unsigned offsets of 1 to 16 bits per axis (sum a multiple of 4) so that
`position = (anchor + offset) * 2^(exponent - 127)`, which converts to float exactly. Topology is a
generalized triangle strip with a 2-bit control field per triangle (restart, reuse edge 1, reuse
edge 2, backtrack) plus a first-use bit and a 3 to 6 bit re-use index. An optional geometry-ID
palette and 32-bit user word ride in the block.

| Measure (paper) | Value |
|---|---|
| Position plus topology, offset bits b = 11 to 16 | 2.74 to 5.42 B/triangle (b = 14: 2.87 to 4.37); b = 24: 3.68 to 6.95 (Table 1) |
| Same, eight benchmark meshes at b = 14 | 2.87 to 3.29 B/triangle (Table 3) |
| Scenes at b = 16 including geometry IDs | Sponza 4.84, San Miguel 3.47, Powerplant 3.05 B/triangle (Table 2) |
| Vertex duplication caused by 64-vertex blocks | 1.49 to 2.16 |
| Compressed meshlets of Kuth et al. in the same comparison | about 4.0 B/triangle, duplication 1.22 to 1.24 |
| Locked-width blocks that allow animation | 2x to 4x less dense |

DGF stores position and topology only. Attributes are application sideband data. The SDK
(`DGFLib`, `DGFBaker`, samples for D3D12 and Vulkan; releases v1.0.0 2025-02-06, v1.1.0
2025-09-23, v1.2.0 2026-05-07) includes HLSL functions that fetch triangle `i` of a block with a
strip scan (`DGFLoadBlockInfo`, `DGFGetTriangle_BitScan_Wave`, `DGFGetVertex`). It accepts clusters
from meshoptimizer through a pre-clustered path. DGF SuperCompression reduces storage up to 22%
relative to compressing DGF blocks directly and offers "the ability to quickly decode to an
uncompressed meshlet on legacy hardware, with no additional storage".

Hardware: the README says DGF "will be directly supported by future AMD GPU Architectures". The
only shipping integration is the provisional `VK_AMDX_dense_geometry_format` extension (GPUOpen
post of 2025-08-05, preview driver 25.10.25.02), which consumes DGF only as input to
ray-tracing acceleration-structure builds, not to rasterization. Press coverage ties the hardware
to RDNA 5 (**Reported**, search snippets only, for example VideoCardz); I did not fetch a primary
statement of an architecture or date. There is no Apple, NVIDIA, Intel or Metal support. DGF is
therefore a reference design for the block idea, not something Luminex can use.

### GPUOpen meshlet compression and related papers

The GPUOpen article ([Meshlet compression](https://gpuopen.com/learn/mesh_shaders/mesh_shaders-meshlet_compression/),
2024-11-01) is the practitioner write-up of Kuth et al., ["Towards Practical Meshlet
Compression"](https://arxiv.org/pdf/2404.06359) (VMV 2024, arXiv v2 of 2024-08-13, authors from
Coburg University and AMD). I read the paper. It orders each meshlet's triangles as an optimal
generalized triangle strip found with a mixed-integer program, then packs vertex re-use. Results
(Table 2): index data falls from 96 bits per triangle (vertex pipeline) and 24 bits (plain
meshlets) to 9.5 bits (strip) or 5.9 bits (strip plus re-use). Vertices stay self-contained, so the
meshlet duplicates about 20% of its boundary vertices; attributes are 16-bit fixed point, 8
attributes giving 16 B/vertex versus 32 B in floats. Decode runs in the mesh shader every frame
and was faster than the plain vertex pipeline on an RX 7900 XTX (15.5 M triangles in 0.59 ms) as
long as the frame is not overdraw-bound.

Mlakar, Steinberger and Schmalstieg, ["End-to-End Compressed Meshlet
Rendering"](https://doi.org/10.1111/cgf.15002) (Computer Graphics Forum 43(1), 2024; I read the
open PDF), keeps the *same* compressed format on disk and in GPU memory and decodes in the mesh
shader. Its Table 6 classifies Nanite as a decode-on-the-fly system with about 17 bits per triangle
of connectivity and observes that GPU decompression schemes "store the decompressed data in GPU
memory in addition to the compressed data", which raises memory pressure. That sentence matters
for Luminex because it names the choice between one shader-ready resident format and a separate
transport format (see the recommendation).

### Capcom RE Engine

REAC 2025 talk ["RE ENGINE Meshlet Rendering Pipeline"](https://enginearchitecture.org/downloads/REAC_2025_Capcom.pdf)
(71 slides, read in full). Meshlets are baked offline: up to 128 vertices and 128 triangles (Dragon's
Dogma 2 and Monster Hunter Wilds both use 128/128; the size of one asset falls from
39,763 KB at 32/32 to 33,231 KB at 128/128). A mesh is one byte-address buffer with a 256-byte
header listing up to 8 LODs with per-LOD meshlet offsets and LOD factors. Each meshlet has a 32-byte
header with bounds and per-attribute compression flags. Vertex data lives directly inside the
cluster, split into index and vertex attribute sections. Two compression ideas are used: quantized
vertices at 16, 10 or 8 bits, and constant-attribute compaction (if every vertex of a meshlet has the
same normal flip sign, vertex color and so on, one value is stored, with a bitmask only for mixed
cases); indices are 8-bit triangle lists or strips. One engine sample went from 75,791,760 bytes
(plain meshes) to 56,275,516 (meshlets) to 40,309,144 (meshlets plus attribute compaction),
about 47% smaller overall.

Two cautions. RE Engine uses *discrete* LODs (up to eight, "stored from least to most detailed"),
not a continuous cluster DAG; the slides list "automatic seamless LODs" under next steps. And its
streaming support is described in one line: LODs are ordered coarse-first and "use ReservedResource
(TiledResource) for memory management". The talk gives no request, priority or eviction details, so
nothing below about streaming should be attributed to RE Engine beyond that.

### NVIDIA micro-meshes

The [Displacement MicroMap Toolkit](https://github.com/NVIDIAGameWorks/Displacement-MicroMap-Toolkit)
is archived (last push 2025-02-13), its README states that `VK_NV_displacement_micromap` "is no
longer available", and it points to RTX Mega Geometry (NVIDIA-RTX/RTXMG, still active with a
push on 2026-09-04) as the recommended path. The companion SDK repository is also archived. The DGF
paper tabulates displaced micro-meshes at 0.81 to 2.38 B/triangle (derived from Maggiordomo et al.
2023), about 3x denser than DGF but limited to displaced base meshes, with error that varies up to
10x. Opacity micromaps, a separate technology, survive and meshoptimizer v1.1 can generate them. For
Luminex micro-meshes are historical context only.

### Nanite

Concept level only; the mechanism details are in `nanite-core-architecture.md`. What I could
establish from primary sources:

- The 2021 SIGGRAPH deck ([Karis, Stubbe, Wihlidal](https://advances.realtimerendering.com/s2021/Karis_Nanite_SIGGRAPH_Advances_2021_final.pdf),
  155 pages with speaker notes) covers streaming on slides 121 to 127 and compression on slides
  128 to 144. Streaming: the unit is the group, because a group is only a complete replacement for
  its parents when all of its clusters are present; groups go into fixed-size pages to avoid
  fragmentation; the root page and the whole culling hierarchy stay resident, so traversal can
  look past the loaded cut and request every level a new object needs at once; requests are page
  ranges with a priority from the LOD error; the CPU adds missing dependencies, evicts
  low-priority pages, installs pages and patches pointers and leaf marks. Compression: two
  formats, a bit-packed random-access memory format (triangles at about 17 bits each as a base
  index plus two 5-bit offsets, tangents implicit in 2021) and a disk format transcoded on the GPU
  at install and assumed to pass through a byte-based LZ codec (strip topology at about 5 bits
  per triangle, and "~30% of vertices coded as references" to the same or a parent page).
- The deck's results slide (144, "Lumen in the Land of Nanite") gives 433 M input triangles and
  882 M stored triangles (2.04x), 25.90 GB raw, 7.67 GB in the memory format, and 4.61 GB in the
  compressed disk format: "5.6 bytes per Nanite triangle" and "11.4 bytes per input triangle".
  The memory format is therefore about 8.7 B per stored triangle and 17.7 B per input triangle.
  The DGF paper's "roughly 9 B/tri in memory and 5.6 B/tri on disk" matches that slide; both of
  its figures are per stored triangle, hierarchy included, and the disk figure is after LZ.
- Epic's [Nanite technical details](https://dev.epicgames.com/documentation/unreal-engine/nanite-technical-details?lang=en-US)
  states that Nanite meshes in the Valley of the Ancients sample "average 14.4 bytes per input
  triangle" on disk (about 13.8 MB for a
  million-triangle mesh; a 1.5 M triangle example is 19.64 MB, 7.6x smaller than the standard
  static-mesh format at 148.95 MB), that position quantization is "a form of lossy compression" with a
  power-of-two step chosen through a Position Precision property, and that quantization happens in
  unnormalized object coordinates around the mesh origin to avoid cracks where meshes share
  boundaries. With the Streaming Geometry show flag off, meshes render "at the quality level that
  is always resident in memory".
- `Engine/Shaders/Shared/NaniteDefinitions.h` on `ue5-main` (read 2026-10-01) fixes: streaming
  pages of 2^17 = 128 KB, root pages of 2^15 = 32 KB, at most 256 and 64 clusters respectively,
  position quantization up to 21 bits per axis, normals up to 15 bits per octahedral component,
  tangents up to 12 bits of angle plus a sign bit, UVs as a 5-exponent, 14-mantissa float format. Cluster flags
  named for a root leaf, a streaming leaf and a full leaf encode which clusters count as leaves for the
  current residency state.
- `NaniteStreamingManager.cpp` (same branch) defaults: a 512 MB streaming pool plus separate root
  pages (2048 initially, growable), at most 128 pending pages and 128 page installs per frame, 3
  I/O retries, and a quality scale that cuts quality (raises the allowed screen error) when the
  distinct pages requested in a frame exceed 85% of the pool and restores it below 70%, with a
  floor of 0.3.

## Quantization practice

### Positions: one shared grid

The invariant for watertightness is that the quantized value of a shared vertex is a pure function
of its position and a mesh-level setting, never of the cluster that happens to hold it. Four
independent sources agree:

- meshoptimizer README: a shared exponent "to avoid quantizing shared vertices differently between
  clusters"; every cluster at every level must share it "to eliminate gaps between adjacent clusters
  at different resolutions".
- Kuth et al. section 4.4: "duplicate vertices along a meshlet boundary map to the same values.
  Otherwise, we would get unwanted cracks"; their fix is a global anisotropic grid with spacing from the largest
  meshlet extent, and a per-meshlet lowest value so that b bits (they use 16) suffice.
- Epic documentation and `NaniteEncode.cpp` (per the sibling notebook): one grid per mesh in
  object-origin-centered coordinates, per-cluster integer origin and bit count per axis, at most 21
  bits. The 2021 deck (slide 132) adds that the step is an absolute power of two centered on the
  object origin and deliberately not normalized to the bounds, so that separately built objects
  land on the same grid, and that only the leaf level aligns perfectly between objects, because
  each object simplifies and selects LOD on its own.
- DXR2's `Compressed1` position encoding ([DirectX-Specs Raytracing2](https://microsoft.github.io/DirectX-Specs/d3d/Raytracing2.html);
  the spec says release "likely starts with a ~late summer 2026 preview" and that the features are
  "aligned across hardware vendors") is a 12-byte header with a 24-bit anchor per axis, an exponent
  and 1 to 16 bit deltas, with `position = (anchor + offset) * 2^(exponent-127)`. DGF has the same
  shape. A cross-vendor ray-tracing API standardizing this layout is strong evidence that it is the
  stable choice.

What breaks if each cluster quantizes independently (own origin and step): a vertex on the boundary
of clusters A and B decodes to two different floats, separated by up to half a step of each grid.
Within one LOD level this gives hairline cracks. Across levels it is worse,
because the boundary between two groups is shared at different resolutions by construction (the
simplifier locks group boundaries so both sides keep the same vertices); an off-grid mismatch
reopens exactly the seam the crack-avoidance design closed. The fix is not "more bits", it is a
shared function. What may vary per cluster, without loss, is the anchor and the bit count per axis,
because those only re-express integers that already lie on the shared grid.

A related design appears in NVIDIA's vk_lod_clusters: it does not use an integer grid. It drops a
fixed number of mantissa bits from each float (`quantizeFloat`, adapted from
`meshopt_quantizeFloat`), which is also a pure function of the value and therefore crack-free, and
then bit-packs the resulting integer bit patterns per cluster with a lowest value, a trailing-zero
shift and a bit width per axis. Precision is relative to magnitude instead of uniform, so near the
origin it is higher than necessary. For a shader that decodes every frame, an integer grid is the
simpler contract.

Two practical rules follow for a bake pipeline (my reasoning, consistent with the sources):

1. Snap the source positions to the grid before building the hierarchy, or re-snap after any
   simplification step that moves vertices. Locked group boundaries are unmoved, so they stay
   identical; interior vertices that are shared by clusters inside one group must be quantized
   once, before the group is split into clusters.
2. Choose the grid so integer coordinates fit in 24 bits. DGF notes that this makes the conversion
   to float exact: a 32-bit float has a 24-bit significand, and the multiplication by a power of
   two that follows only changes the exponent. Then a reference path that draws the dequantized
   float mesh and the cluster path that decodes the pages produce the same object-space positions
   bit for bit, which is a precondition for comparing them with exact-image gates (the transform
   arithmetic after the decode has to match as well). With wider integers the conversion rounds,
   and identical rounding on the CPU and in the shader is no longer guaranteed.

The same invariant has a scope limit. It holds inside one hierarchy. The program in "Memory
arithmetic" builds one hierarchy per glTF primitive, and primitives that share a surface but
differ in material are then separate objects in the deck's sense: they need grids with the same
step and origin to match at the leaf level, and their coarser levels are not guaranteed to match
along the shared border.

A consequence for the offset width: in a cluster hierarchy the coarsest cluster of a mesh spans the
whole mesh, so a fixed 16-bit offset only works if the mesh grid has at most 65,536 steps across the
mesh (about 0.5 mm over a 30 m scene, 1.5 cm over a kilometer). Finer grids need a per-cluster axis
width. Byte-aligned widths of one to three bytes keep the decode trivial and cost, in the program
described below, 5.9 B per vertex on Sponza and 4.8 on San Miguel at a 16-bit grid (7.4 and 6.4 at
a 21-bit grid), against 4.3 and 3.4 B for fully bit-packed offsets. Nanite's 21-bit ceiling and the
meshoptimizer remark that hierarchical LOD may need more than 16 bits are the same point.

### Normals, tangents and UVs

| Attribute | Practice | Evidence |
|---|---|---|
| Normal | Octahedral, 10 to 16 bits per component | meshoptimizer README recommends octahedral at 10 to 12 bits and ships `meshopt_encodeFilterOct` (2 to 16 bits, 4 or 8 B); Nanite up to 15 bits/component, default 8 per the sibling notebook; vk_lod_clusters 22 bits (11 + 11) in one 32-bit word; Kuth et al. 16-bit fixed |
| Tangent | Angle around the normal plus a sign bit, using a deterministic reference frame derived from the normal | Nanite: up to 12 bits of angle; vk_lod_clusters: 10 bits in the remaining word, based on "3 byte tangent frames"; `meshopt_encodeFilterQuat` is the 8-byte quaternion alternative; Nanite also has a fallback that derives a tangent from the normal alone when tangents are absent |
| UV | Fixed point on a per-mesh range, or a small float; range must cover tiling | Nanite 5-exponent, 14-mantissa float, then per-cluster minimum and bit count; vk_lod_clusters 16-bit pairs; Kuth et al. 16-bit fixed |
| Material section | Per-cluster material ranges or a small palette | Nanite packs up to three materials into 32 bits, otherwise a table of up to 64; vk_lod_clusters uses a per-cluster palette because a cluster rarely mixes more than two or three values |

Normals tolerate per-cluster bit counts better than positions because quantization error is far
below shading noise once at least about 12 bits per component are kept; Kuth et al. found that a
global grid adds no precision for normals, since one cluster with normals spread over the sphere
cancels the benefit. A shared tangent reference-frame function must be identical in the bake and in
the shader; otherwise the tangent angle decodes to a rotated frame. meshoptimizer 1.2 can produce
per-corner MikkTSpace-class tangents, and its documentation warns that applying them to an indexed
mesh may require splitting vertices, so tangent generation belongs before cluster building. UV
precision is set by texture size, not by mesh size: a 16-bit fixed range of 0 to 8 for a tiled
material gives a step of 1/8192, half a texel at 4096. That is a bake setting, not a format change,
as long as the per-mesh range is stored.

## Streaming architectures

### Anatomy shared by the systems read

Where they document it, the systems below answer the same seven questions.

| Question | Nanite (source, 2021 deck) | vk_lod_clusters | Nyx | Solis, light-system, RE Engine |
|---|---|---|---|---|
| Unit of streaming | Fixed 128 KB page; groups sorted coarse level first, then Morton order, appended to pages; a group may be split into parts across pages | One group (up to 128 clusters, variable size) | Fixed 256 KB page of groups; group to page map; LZ4 per page | Solis: groups ("groups that haven't been used in a while get evicted"). light-system: `.vgeo` pages with a page table. RE Engine: tiled reserved resource, coarse-first LODs |
| Always resident | Root pages (32 KB) with the coarsest clusters, and the whole culling hierarchy | Lowest-LOD group of every geometry ("persistent"), outside the memory budget | Root pages pinned, up to 6,144 pages; sorted by root count | Solis: "a coarse root group per mesh stays pinned". light-system: pages start all resident by default |
| How a miss is detected | Cluster culling writes a request record (resource, page range key, priority) | Hierarchy traversal finds a non-resident group and appends it to a load list | Culling sets one bit per group in a request mask | Not described |
| GPU to CPU | Request buffer read back to the CPU; most processing on a worker thread (`r.Nanite.Streaming.Async`) | Request download at the end of the frame; handled after the fence | Triple-buffered readback of the mask | Not described |
| Priority | Float priority bits with a 2-bit category; CPU keeps the maximum per page; pages a request depends on are requested at priority plus one and a max-heap picks the most urgent, so parents install first | None; first come within a per-frame cap. The documentation expects distance sorting to help | None; ordered by page index | Not described |
| Eviction | LRU list; pool pressure (distinct pages requested versus pool size) scales quality instead of failing | GPU age filter appends groups not touched for N frames (default 16) | Anything unused for 512 frames, regardless of pressure; no budget-driven eviction | Solis: "evicted"; others not described |
| Behavior when missing | Cluster flagged as a leaf in the current state keeps drawing; fixups patch flags at install and removal | Traversal does not descend into the missing group; a cluster whose generating group is absent draws unconditionally | Table entry set invalid on eviction; pinned roots are the fallback (shader side not read) | Solis: fallback is the pinned root |

Evidence per column: Nanite from `NaniteDefinitions.h`, `NaniteEncodePageAssignment.cpp`,
`NaniteStreamingManager.cpp`, `NaniteStreaming.ush` and `NaniteTranscode.usf` on `ue5-main`, read
2026-10-01, plus the 2021 deck for the concept. vk_lod_clusters from `docs/streaming.md`, the README,
`shaders/traversal_run.comp.glsl` and `traversal_run_groups.comp.glsl`
([repository](https://github.com/nvpro-samples/vk_lod_clusters), Apache-2.0). Nyx from the README and
`MiniEngine/Model/GeometryStreaming.cpp` ([repository](https://github.com/moonlovelj/Nyx), MIT).
Solis from its README ([repository](https://github.com/Vovan675/Solis), which declares no license).
light-system from its README and `schemas/PAGE_LAYOUT_SCHEMA.md`
([repository](https://github.com/usestemframework/light-system), MIT).

### Mechanisms worth reimplementing

**The residency table is the whole interface between traversal and storage.** In vk_lod_clusters
each geometry owns an array of 64-bit group addresses. A valid value is a device address. A value
with the top bit set means "not resident", and its low 63 bits record the frame index of the last
request, so a shader can use one atomic max both to detect "already requested this frame" and to
deduplicate. Requests are appended to a device list with a per-frame cap. Nyx uses the same shape
with a chunk index and byte offset per group, invalidated on eviction. Nanite keeps the equivalent
inside the hierarchy and cluster records and patches them (fixups) on install and removal.

**Hole prevention is a data property, not a runtime heuristic.** Two designs:

- *Dynamic leaf flags (Nanite).* Parent clusters carry state that says whether their children are
  resident; installing or removing a page patches the flags of the parents and the child pointers
  in the hierarchy. Cost: mutable page contents, fixup bookkeeping and dependency ordering.
- *Residency lookup at selection time (vk_lod_clusters).* Each cluster stores the id of its
  generating group, the finer group it was simplified from. When a cluster passes the "coarse
  enough" test, the shader asks whether that generating group is resident. If it is, the group's
  own metric decides; if not, the cluster is drawn because nothing finer exists. Pages are
  immutable after install and only the table changes. Cost: one dependent read per cluster.

The second design needs one bake-time fact: a per-cluster generating-group reference. Nanite stores
the same fact (`GeneratingGroupIndex` in the builder's page assignment), so it is a requirement of
any cut-based scheme. Nanite also guarantees that a page's dependencies cannot be cyclic by sorting
groups by LOD level before packing.

**Parent-before-child ordering decides install order.** Nanite requests the pages that a requested
page depends on at one priority step higher and selects requests from a max-heap, so parents
install first. The reason is in the
transcode shader: a cluster on disk may reference vertices stored in a parent page (pass two of the
transcode copies those vertices from already-installed pages and re-expresses their positions
relative to the child cluster's own origin). Nanite's disk format therefore trades a smaller size
for a hard install-order constraint.

**Request volume is small.** Nyx uses one bit per group. For the two scenes I measured, groups
come to roughly 1,500 to 1,650 per million source triangles (see below), so a one-bit mask for a
billion triangles is about 190 to 210 KB, a group address table at 8 B per group about 12 to 13
MB, and a capped request list of a few thousand 8-byte entries is a few tens of KB. The
readback is not a bandwidth problem; it is a latency problem (two to three frames of delay with
triple buffering), which is why a coarse resident fallback exists.

**Budget pressure can degrade quality instead of failing.** Nanite measures the distinct pages
requested in a frame against the pool size; above 85% it raises the allowed screen error, and it
restores it below 70%. This reuses the LOD-error knob and is the geometry analogue of the
dynamic-resolution controller the project already has.

**Page granularity is a bin-packing choice.** Nanite fills each page cluster by cluster and splits
groups into parts when a page runs out of room; the 2021 deck (slide 125) says whole groups of 8
to 32 clusters left significant slack and that splitting brought it to about 1% of a 128 KB page.
Nyx and vk_lod_clusters avoid splitting by making a whole group (or several whole groups) the
unit. The model below, with 12 to 14 clusters per group, fills 128 KB pages 99.5% or better with
whole groups only when groups are sorted by size, which ignores level order and locality. Keeping
a coarse-first order drops the fill to 92 to 97% at 128 KB and 83 to 92% at 64 KB, and giving each
primitive its own pages drops it to about 76 to 81%. Group parts are therefore optional at 128 KB
if a few percent of slack is acceptable, not unnecessary.

### Nanite concept from the 2021 deck

At runtime a cut of the cluster tree is chosen by projected screen-space error; a parent draws
instead of its children when the difference is imperceptible. Streaming exploits the same cut:
"mark any cut of the tree as leaves and toss the rest", request children only when needed, evict
children not drawn for a while. The deck explains that the previous-frame visible set cannot drive
occlusion culling because "visible clusters from last frame might not even be in memory anymore due
to streaming", so Nanite instead tests the current selection against last frame's HZB with last
frame's transforms. That is a coupling between streaming and the two-phase occlusion design that
other notebooks cover.

## Apple specifics

This section is the architectural picture; exact API limits (maximum buffer length, argument
buffer limits, heap alignment) belong to `apple-metal-geometry-constraints.md`.

**Memory model.** Apple GPUs share system memory with the CPU. Apple's
[storage-mode guidance](https://developer.apple.com/documentation/metal/choosing-a-resource-storage-mode-for-apple-gpus)
(read through the documentation JSON endpoint, because the page body is script-rendered) says shared
mode is for data "populate and update on the CPU" and private mode is for data the GPU populates,
"common for render targets, intermediary resources, or texture streaming". There is no PCIe
upload step. A streamed page can be decoded by the CPU straight into a shared buffer, or loaded into
a private buffer by the IO queue or a blit. The project's RHI already offers checked host writes
(`BufferDesc::cpuWrite`) that are legal only after all GPU use of the range has retired, which is
the same rule a page installer needs. **[UNVERIFIED]** whether decoders writing to a shared Metal
buffer behave like writes to write-combined memory on Apple Silicon.

**Fast resource loading.** `MTLIOCommandQueue` (macOS 13.0+, iOS 16.0+; the
[class page](https://developer.apple.com/documentation/metal/mtliocommandqueue) describes it as a
queue that "schedules input/output commands for reading files in the file system, and writing to GPU
resources") loads files directly into buffers and textures. From WWDC22 session
[Load resources faster with Metal 3](https://developer.apple.com/videos/play/wwdc2022/10104/)
(read as a fetched transcript summary): an `MTLIOFileHandle` opens the file; the queue has a type
(concurrent or serial) and a priority (high, normal, low); an `MTLIOCommandBuffer` encodes
`loadBuffer`, `loadTexture` and `loadBytes`; commands complete out of order; command buffers can
wait on and signal shared events so the render queue can synchronize with them; cancellation works
at command-buffer granularity; and compressed files get inline decompression "by translating the
offsets to a list of chunks it needs to decompress". The
[`MTLIOCompressionMethod`](https://developer.apple.com/documentation/metal/mtliocompressionmethod)
enumeration has five codecs: zlib, lzfse, lz4, lzma and lzBitmap (the same five are in the
installed macOS SDK header), and the session's example creates a compression context with a 64 KB chunk
size. `MTLIOCompressionContextDefaultChunkSize()` returns 65,536 on the recorded machine. The
queue descriptor exposes two app-set bounds, the maximum number of command buffers and of
commands in flight.

Consequences for a page format: pages should be multiples of the compression chunk size so one page
is a whole number of chunks; a page file can be compressed per page or per run of pages with a
built-in codec at no new dependency; the meshopt meshlet codec cannot be an MTLIO codec and would be
a CPU or compute transcode step after the load. **[UNVERIFIED]** whether the decompression runs on
GPU hardware or CPU on current Apple Silicon (the session text says "inline"), and whether
`loadBuffer` can target a buffer placed in a heap or a placement-sparse buffer.

**Sparse and placement heaps.** `MTLSparsePageSize` has three sizes, 16, 64 and 256 KB
([reference](https://developer.apple.com/documentation/metal/mtlsparsepagesize)). Metal 4 adds
placement sparse *buffers* and textures: resources created without pages, backed by tiles of a
placement heap that the app maps in byte ranges (`newBufferWithLength:options:placementSparsePageSize:`
in the SDK header; `MTLDevice.supportsPlacementSparse` reports true on the recorded machine).
Mapping operations go through `MTL4CommandQueue` (`updateBufferMappings`, with copy variants), and WWDC25
sessions [Discover Metal 4](https://developer.apple.com/videos/play/wwdc2025/205/) and
[Explore Metal 4 games](https://developer.apple.com/videos/play/wwdc2025/254/) show the event
handshake: one signal unblocks the Metal 4 queue to remap, a second signals the render work to
continue. A sparse geometry buffer would give every page a stable virtual offset and remove the
need to patch addresses, which is what Nanite's optional `r.Nanite.Streaming.ReservedResources` and
RE Engine's tiled reserved resource do on other APIs. The project's RHI has placement heaps but no
sparse resources, and the roadmap already says sparse pages need evidence that ordinary streaming is
insufficient; the pool-plus-table design above needs none of this.

**Residency.** Metal 4 makes resources visible to the GPU through residency sets, and Apple's
guidance (Explore Metal 4 games) is "prefer having fewer residency sets with more resources each".
That favors a few large page-pool buffers over one buffer per page or many small chunk buffers
(Nyx's up to 32 chunks of 256 MB, bound bindlessly, would not fit the RHI's current 16-buffer
argument tables without a bindless path). The RHI already has a residency set.

**What is not there.** No hardware geometry decompression (DGF, micro-meshes), no cluster
acceleration structure equivalent of NVIDIA's CLAS in anything I read, and only general-purpose
codecs in the IO path. All geometry-specific decoding is shader or CPU work.

## Memory arithmetic

### Method

I built a scratch measurement program (not committed) around meshoptimizer v1.3 (tag commit
`9e1f07b`, 2026-09-25) and its `clusterlod.h`, ran it over the glTF files that `xmake setup`
fetches (Crytek Sponza and the San Miguel courtyard), and added a byte model on top. Settings are
`clodDefaultConfig(128)`: up to 128 triangles and 128 vertices per cluster, partition target 16
clusters per group, simplification ratio 0.5, normals as attributes with weight 0.5 and UV seams
protected, plain `meshopt_simplify` (no vertex position update). The hierarchy is built per glTF
primitive (25 for Sponza, 281 for San Miguel). Byte model per cluster, in a self-contained cluster
page: 32 B cluster record (assumed); 12 B position header (assumed); positions as bit-packed
integer offsets on a 16-bit grid per primitive, normalized to that primitive's bounds, with a
per-cluster anchor and per-axis bit counts (bit widths counted from the real coordinates; no
bitstream was written);
8 B attributes per vertex (a 32-bit word with 22-bit octahedral normal and 10-bit tangent angle,
plus two 16-bit UVs; assumed, nothing was encoded); topology as `meshopt_encodeMeshlet` output
without vertex references after `meshopt_optimizeMeshletLevel` level 3 (measured encoder output);
32 B per group (assumed); 32 B per hierarchy node from `clodBuildHierarchy` (width 4; node count
measured, node size assumed). Not included: material tables, entropy or
page-level compression, texture data, and tangents beyond the 10-bit angle. The plain indexed
baseline is the project's current layout: a 48-byte vertex (position, normal, tangent with sign,
UV) and 32-bit indices.

Three kinds of number follow, and the tables say which is which. *Measured*: counts from the
build (triangles, vertices, clusters, groups, nodes), encoder output sizes, and position bit
widths. *Derived*: arithmetic on those counts with the current vertex layout, which covers the
plain indexed and 48-byte-vertex cluster rows. *Modeled*: every "compressed" figure, per-group
size, page count and fill rate, because each adds the assumed attribute and header bytes to the
measured parts. The assumed attributes are 59% (Sponza) and 65% (San Miguel) of the compressed
leaf figure and the assumed headers about 3.5%, so roughly two thirds of each compressed figure
is an assumption, not a measurement.

Scene facts counted from the same files: Sponza has 262,267 triangles and 184,406 vertices
(vertex/triangle ratio 0.703); San Miguel has 5,617,451 triangles and 5,861,789 vertices (1.043,
because the converted OBJ has many hard edges and UV seams; welding bit-identical vertices changed
the count by 0.1%). The ratio matters because the plain format spends 48 B per vertex.

| Measured (the two position rows include the assumed 12 B header) | Sponza | San Miguel |
|---|---|---|
| Groups, clusters, all levels | 395, 4,863 | 9,281, 129,338 |
| Triangles over all levels / leaf triangles | 1.991 | 1.981 |
| Average triangles and vertices per cluster | 107.4, 90.5 | 86.1, 98.4 |
| Clusters per group | 12.3 | 13.9 |
| Hierarchy nodes per group | 1.27 | 1.32 |
| Topology, `encodeMeshlet`, no references | 0.80 B/tri (6.4 bits) | 0.83 B/tri (6.6 bits) |
| Topology with vertex references | 1.89 B/tri (15.1 bits) | 2.24 B/tri (17.9 bits) |
| Position bits per vertex on a 16-bit mesh grid, bit-packed, including header | 34.4 | 27.2 |
| Position bytes per vertex with per-cluster byte-aligned axis widths (1 to 3 B per axis), 16-bit / 21-bit mesh grid, including header | 5.9 / 7.4 | 4.8 / 6.4 |

The with-references figures exceed the README's 9 to 12 bits because I did not reorder vertex
references for fetch locality, which the README says the encoder assumes. Treat them as an upper
bound.

### Bytes per source triangle

"Source triangle" means a triangle of the original mesh, so hierarchy rows include the coarser
levels.

| Representation | Kind | Sponza | San Miguel |
|---|---|---|---|
| Plain indexed, 48 B vertex + 12 B/tri index (the current layout) | Derived from counts | 45.8 | 62.1 |
| Self-contained clusters, leaf level only, 48 B vertices, 3 B/tri | Derived from counts, plus the assumed cluster record | 41.5 | 55.7 |
| Same, whole hierarchy | Derived, as above | 87.1 | 115.4 |
| Compressed cluster pages, leaf level only | Modeled | 10.7 | 13.4 |
| Compressed cluster pages, whole hierarchy | Modeled | 22.9 | 28.2 |
| Whole hierarchy plus hierarchy nodes | Modeled | 22.9 (+0.06) | 28.3 (+0.07) |
| Whole hierarchy with shared parent vertices (see below) | Modeled, upper bound on the saving | about 13.6 | about 17.1 |

Meshletization alone does not shrink anything: 41.5 against 45.8. All the saving comes from
quantization (a 384-bit vertex becomes about 91 to 98 bits, of which 64 are the assumed attribute
bits) and from topology (24 bits per triangle becomes about 6.5). The model also gives a sanity
check against published figures. Per stored triangle the model is 11.5 B (Sponza) and 14.3 B
(San Miguel), against about 8.7 B for Nanite's 2021 memory format, which is also bit-packed and
not LZ-compressed but stored no tangents and came from different assets. Nanite's on-disk figures
(11.4 B per input triangle in the 2021 demo, 14.4 B in Epic's documentation) are after LZ
compression and vertex references, so they are not comparable with a model that has neither; the
closeness of 14.4 B to the 13.6 to 17.1 B row is not evidence for that row. NVIDIA's uncompressed
processing cache for the 1.64 billion triangle Zorah scene is
62 GB, about 38 B per source triangle
([blog](https://zeux.io/2025/09/30/billions-of-triangles-in-minutes/), "Render cache 62 GB" for
"1.64B triangles"), which is the same order as an uncompressed hierarchy with 24-byte vertices
(float position, 4 B packed normal and tangent, 8 B UV, per that sample's cluster layout), roughly
33 to 47 B per source triangle by my model across the two scenes' vertex ratios. The comparison is
loose: the blog notes that most Zorah meshes carry positions only. Solis reports one
copy of Zorah at about 10 GB as glTF and about 70 GB baked into meshlets and LODs, a factor of
seven.

### Worked sizes

Decimal units; one million source triangles is 10^6. Compressed hierarchy rows use the model above.

| Source triangles | Plain indexed (Sponza-like / San Miguel-like) | Compressed leaf only | Compressed whole hierarchy | Hierarchy with shared vertices (model) |
|---|---|---|---|---|
| 1 M | 45.8 / 62.1 MB | 10.7 / 13.4 MB | 22.9 / 28.2 MB | 13.6 / 17.1 MB |
| 100 M | 4.6 / 6.2 GB | 1.1 / 1.3 GB | 2.3 / 2.8 GB | 1.4 / 1.7 GB |
| 1 B | 45.8 / 62.1 GB | 10.7 / 13.4 GB | 22.9 / 28.2 GB | 13.6 / 17.1 GB |

Uncompressed clusters for the whole hierarchy would be 87 / 115 GB at one billion. At 128 KB per page
the compressed hierarchy is about 175 to 215 pages per million source triangles, roughly 17,500 to
21,500 pages at 100 M and 175,000 to 215,000 at one billion; a 4-byte page-table entry per page is under 1 MB
even at the top.

The project's own content is far below any of these limits: by the model, Sponza's whole
compressed hierarchy is about 6.0 MB (46 pages of 128 KB at best-case packing) and San Miguel's
about 159 MB (1,211 pages), against a derived 12 MB and 349 MB in the current plain format. Baked
mipmapped textures on disk are 176 MB for Sponza and 478 MB for San Miguel (file sizes summed over
the `Baked/` directories of the fetched assets), so for the existing scenes textures already
outweigh geometry by about 29x and 3x respectively on a modeled compressed-geometry basis, and by
about 15x and 1.4x against the plain format. That is local evidence for sequencing texture-mip
streaming before geometry streaming.

### Hierarchy overhead

Each level keeps about half the triangles of the one below (ratio 0.5), so the geometric series
gives 2.0x triangles in the limit, and the program measures 1.98 to 1.99x (Epic's 2021 demo
figures give 2.04x: 882 M stored for 433 M input triangles). Modeled bytes grow slightly
more, about 2.1x, because coarser clusters cover more space and cost more per triangle: on Sponza
the compressed price rises from 10.7 B per stored triangle at the leaf to 13.3 at level 3, 17.4 at
level 7 and 36.7 at level 10; San Miguel rises from 13.4 to 21.5. The hierarchy over groups
(`clodBuildHierarchy`, node width 4) is 1.3 nodes per group, about 0.06 to 0.07 B per source
triangle, negligible. Dropping the coarse levels from the file is not an option for a runtime that
wants to stream, because the coarse levels are what must stay resident; rebuilding them at load
time would trade the file size for build time.

### The resident coarse level

Level 0 is the original geometry, and levels count upward from it, so primitives of different
depth end at different levels. Cumulative share of the whole compressed hierarchy (model bytes) at
level k or coarser: k = 4, 7.1% (Sponza) and 7.6% (San Miguel); k = 6, 1.9% and 1.9%; k = 8, 0.4% and 0.4%.
The terminal group of each mesh (the single root cluster the simplifier ends on) is only 0.16 to
0.32% of the bytes (19 KB of 6.0 MB; 251 KB of 159 MB). Scaled to a billion triangles a pinned
level-4-and-coarser set is about 1.6 to 2.2 GB, level 6 and coarser about 0.4 to 0.55 GB, and
level 8 and coarser about 0.08 to 0.12 GB. How deep to pin is therefore a runtime budget, but it is
only possible if the bake tags every group with its level and orders pages coarse-first.

### Page packing

First-fit-decreasing packing of whole groups into fixed pages, using the modeled group sizes. The
packer sorts every group of the scene by size and mixes levels and primitives freely, so these
fills are a best case:

| Page size | Sponza pages, fill | San Miguel pages, fill |
|---|---|---|
| 32 KB | 202, 90.6% | 5,374, 89.1% (51 groups too large) |
| 64 KB | 92, 99.5% | 2,421, 100% |
| 128 KB | 46, 99.5% | 1,211, 99.9% |
| 256 KB | 23, 99.5% | 606, 99.9% |

A page file that keeps the coarse-first order recommended below cannot sort by size. Packing the
same groups in coarse-first order gives, for Sponza and San Miguel:

| Page size | Strictly sequential | At most four pages open | One page set per primitive |
|---|---|---|---|
| 64 KB | 83.2%, 84.0% | 90.6%, 91.8% | 76.9%, 80.1% |
| 128 KB | 91.5%, 92.1% | 95.3%, 97.1% | 76.3%, 81.3% |
| 256 KB | 95.3%, 96.1% | 95.3%, 98.7% | 57.2%, 73.5% |

Modeled compressed groups have a median of 16.7 to 18.0 KB and a maximum of 31 to 38 KB. A 64 KB
page is the smallest that fits every group without splitting at these settings; 128 KB holds about
8 groups, roughly 105 clusters. Cluster size changes modeled totals only a little: with 64-triangle clusters the Sponza hierarchy
is 24.7 B per source triangle, with 128 it is 22.9, with 256 it is 21.9; per-cluster headers and
duplicated boundary vertices (1.21x of the source vertices at 64 triangles, 1.08x at 256) account
for the difference. The grid width matters more: moving Sponza from a 12-bit to a 21-bit mesh grid
moves the hierarchy from 20.4 to 26.0 B per source triangle.

### Cross-level vertex sharing

Because plain simplification keeps a subset of the original vertices, vertices recur at every
level. In the program, 52 to 54% of vertex entries in a cluster at level L also appear in some
cluster at level L+1 of the same primitive (measured). A format that stores such a vertex once, in
the coarser cluster, and lets the finer cluster reference it (Nanite's disk format does this, to
the same page or a parent page; the 2021 deck, slide 140, reports "~30% of vertices coded as
references") would cut the hierarchy to about 13.6 B per source triangle on Sponza and 17.1 on San
Miguel if each reference costs 2 B and replaces the whole vertex. That 40% saving is a model value
and an upper bound: it counts a match in any cluster of the next level, not only in clusters the
finer page could depend on, it ignores the cost of the indirection table, and it assumes no vertex
moves during simplification, which Nanite's simplifier does. At the 30% reference rate Epic
reports, the same arithmetic gives about 17.7 and 21.9 B per source triangle, a saving of about
23%. The structural cost is the one described above: a child page cannot be transcoded before
its parent pages are resident, so the page file needs a dependency list and a parent-first
install order.

### Streaming working set, back of envelope

This one is my estimate, not a measurement. At one triangle per pixel on a 1920 by 1080 view there
are about 2.1 M visible triangles. At the leaf price of about 11 to 14 B per triangle that is 23 to
30 MB, and with the coarser levels that the cut also needs, at most about 50 to 60 MB per view. A
90 degree per second turn replaces the working set in about a second, so sustained demand is of the
order of 50 MB/s, far below an SSD. The Nanite default pool of 512 MB is consistent with that. The
limiting factors are request latency and install bookkeeping, not bandwidth.

## Corrections to earlier research

1. **meshoptimizer dates are two years early** in
   [pipeline-state-of-the-art-m7-m11.md](../2026-09-14-roadmap-review/pipeline-state-of-the-art-m7-m11.md)
   (section 6, "Geometry / LOD", including its "Area take", and the M9 paragraph of the verdict,
   which repeats "Dec 2023"). It places `clusterlod.h` at v1.0 on 2023-12-08, v0.24
   on 2023-06-12, the meshlet codec at v1.1 on 2024-04-02 and `clodBuildHierarchy` at v1.2 on
   2024-06-30. The GitHub releases API gives v0.24 = 2025-06-12, v1.0 = 2025-12-08,
   v1.1 = 2026-04-02 and v1.2 = 2026-06-30, and the current release is v1.3 (2026-09-25).
   `meshopt_partitionClusters` first appeared in v0.23 (2025-03-14), not v0.24. The quoted
   sequence "v0.19 to v1.2 confirmed" in the same row is stale. The
   [open-source references](../2026-09-14-roadmap-review/open-source-references-2024-2026.md)
   row that lists meshoptimizer as "v1.2" is one release behind. The sibling notebook on cluster LOD
   construction independently reached the same dates.
2. **`MTLIOCommandQueue` is not an empty documentation page.** The earlier notebook marked it
   "empty body, [UNVERIFIED live]". Apple's documentation JSON endpoint returns the content: macOS
   13.0+, iOS 16.0+, five compression codecs, and the API shape above.

## Implications for Luminex

### Facts that constrain the design

- The cluster path needs its own geometry store. The current scene builds one immutable rebased
  vertex and index pool with stable generational ids and paced table buffers; a page-addressed
  pool, which must be mutable once streaming exists, is a second, separate store that rows in the
  existing mesh table point into.
- The RHI has argument tables of 16 buffers, a residency set, checked host writes, placement heaps
  and 3 frames in flight, and no sparse resources. A pool of one to a few large buffers plus a
  group-address table fits all of that; many chunk buffers do not.
- Derived artifacts are regenerated from fetched sources by the setup step (the texture bake
  writes DDS plus a manifest pinned by hash). A geometry bake can follow the same pattern. This
  changes what "expensive to change later" means: changing a payload layout costs a re-bake, not a
  data migration, but it also invalidates shader decode paths, recorded exact-image evidence and
  any test fixtures built against the old representation.
- Current content does not need streaming for capacity (6 MB and 159 MB compressed hierarchies by
  the model; 12 MB and 349 MB in the current format).
  The case for baking page-able data in M9 is the cost of later change, not a present need.
- M11 already orders residency as Metal fast resource loading, ordinary loading and mip streaming
  with a renderer-written tile-feedback buffer first, then geometry, and states that geometry
  streaming needs M9's coarse assets
  ([M11 text](../../roadmap/gpu-driven-hybrid-rendering.md)).

### Decide now or later

Judged by coupling to shader code, evidence and the bake tool, not by data size. Each "Now" reason
is tagged. *Evidence* means a source or measurement in this notebook supports deciding it now.
*Model* means the reason rests on the size model. *Judgment* means it is the author's design
preference: the sources show the choice works, but nothing here shows that the alternative fails
or that deciding later costs more.

| Decision | Now or later | Reason and evidence |
|---|---|---|
| Group as the addressable unit, with a record per group (bounds sphere, simplified error, level, first cluster, cluster count) and a per-cluster generating-group reference | **Now** | Evidence. Needed by any cut selection with missing data; same fact exists in `clusterlod.h` (`refined`), vk_lod_clusters and Nanite, and the 2021 deck gives the reason for group granularity. Adding it later changes traversal and every record |
| Fixed-size pages in the baked file, every internal offset page-relative | **Now** | Judgment, with precedent. Nanite and Nyx use fixed pages (the deck's reason is fragmentation); vk_lod_clusters streams variable-size group blobs through an allocator and also works. Page-relative offsets make a page relocatable without patching. Retrofitting means rewriting all addressing |
| Groups not straddling pages | **Now**, with a reserved field for group parts | Judgment, resting on the model. Fill is 99.5% only with size-sorted packing; in coarse-first order it is 92 to 97% at 128 KB and 83 to 92% at 64 KB, and Epic split groups to get from "significant slack" to about 1% |
| Page size a multiple of 64 KB, sub-sections 16-byte aligned | **Now** (value can be a manifest constant) | Evidence for the multiple: it matches the IO compression chunk (65,536 B default on the recorded machine) and sparse page sizes (16, 64, 256 KB). Model for the minimum: 64 KB is the smallest page that holds the largest modeled group at 128-triangle clusters |
| One residency table indexed by group id with a "not resident" sentinel, read by traversal even when everything is resident | **Now** | Judgment, following vk_lod_clusters. Streaming then reduces to table writes; avoids a second traversal variant. Cost is one dependent read per cluster, not measured on Apple GPUs |
| Coarse-first ordering of pages (level first, then Morton order, alternating direction) | **Now** | Evidence. Nanite sorts by level so dependencies cannot be cyclic, and every system read keeps the coarse end resident |
| Page dependency list | **Now** (lists may be empty at first) | Judgment. It is a reservation for vertex sharing and parent-first install; the lookup design recommended here does not need it, and vk_lod_clusters has none |
| Level tag on every group and a way to name the pinned set (terminal groups at least, deeper levels by budget) | **Now** | Evidence for the tag (all systems pin a coarse set); model for the sizes. The coarse level is 0.2% (terminal) to about 7% (level 4 and coarser) of the modeled hierarchy bytes; pinning depth is then a runtime choice |
| Shared integer position grid with a power-of-two step, anchored at the object origin and not normalized to bounds, exponent in the mesh header, coordinates within 24 bits, per-cluster anchor and per-axis bit count | **Now** | Evidence. Independent grids crack boundaries (four sources); changing the grid after the fact changes every vertex, the decode shader and the parity baselines. Primitives that share a surface need the same step and origin |
| Source positions snapped to the grid before hierarchy build (or re-snapped after any vertex-moving simplification) | **Now** | Judgment (the author's inference from the sources, not something a source states). Gives bit-identical boundary vertices by construction and lets the float reference path draw the same snapped mesh for exact parity |
| Parent and child error and bounds in the mesh's units with a monotonic parent convention | **Now** | Evidence. Traversal and streaming priority both consume them; in `clusterlod.h` cluster bounds are for culling only, group `simplified` bounds carry the monotonic error |
| Format version, page size, exponent, cluster limits, attribute-format flags and hash recorded in a manifest, plus per-cluster attribute-format bits | **Now** | Judgment, by project precedent (the texture bake); makes later attribute changes a flag, not a layout change |
| Cluster size (triangle and vertex limits) | **Before the first bake**, owned by the mesh-shader and culling work | Model. About 12% effect on modeled bytes between 64 and 256 triangles (Sponza only) but it fixes kernel shapes; parameterize the limit up to the library's 256 |
| Per-cluster material identity (palette of source material indices, remapped at load) | **Now**, coordinated with the surface-path work | Evidence only if primitives are merged into one hierarchy, which is what keeps coarse levels crack-free across material borders (Nanite deck, slide 136; vk_lod_clusters palette). The measurements here build one hierarchy per glTF primitive, where every cluster has one material and no palette is needed. Which of the two the bake does is the decision to make now |
| Attribute encodings (octahedral normal bits, tangent angle, UV grid) | Now for the contract, bit counts tunable later | Judgment. Bit counts are per-mesh settings stored in the header; the reference-frame function for tangents must be fixed up front |
| Streaming manager, request path, priority, eviction, bandwidth caps | Later (M11) | Policy, not format; vk_lod_clusters describes its own as "rather basic" and untuned |
| Page file codec (LZ4, zlib, lzfse via MTLIO) and page-level entropy coding | Later | Pages are opaque blobs to the bake; apply per page when streaming exists |
| Meshlet topology codec and byte-plane splitting | Later (install-time transcode) | README decode rates (7 to 10 GB/s meshlets, 3 to 6 GB/s vertex data) put a 128 KB page at tens of microseconds |
| Cross-level vertex references | Later, but reserve the dependency list | Saves at most about 40% in my model and about 23% at the reference rate Epic reports, before LZ; forces parent-first install; additive if the page header already has a flags field and dependency list |
| Sparse buffers and placement mapping | Later, only with evidence | The roadmap already requires evidence that ordinary streaming is insufficient |
| GPU transcode shaders | Later | CPU transcode suffices at these volumes |
| Dynamic leaf flags (Nanite style) | Avoid initially | Mutable page contents; the lookup design keeps pages immutable |

### Options

| Option | What M9 ships | What M11 must then do | Judgment |
|---|---|---|---|
| A. Flat resident arrays | Global cluster and group arrays, absolute indices, float or ad hoc payload | Rewrite bake, addressing and traversal data access; re-run all cluster evidence | Cheapest now, most expensive later |
| B. Resident-only, page-able (recommended) | The decide-now list above; whole file loaded at scene load into a pool, table fully resident | IO, install, requests, eviction, budgets; no format change | Small extra work now (page headers, one table, one indirection) |
| C. Option B plus byte-aligned quantized payload | B with grid offsets of 1 to 3 bytes per axis per cluster, 16-bit octahedral normal, 16-bit tangent angle, 16-bit UV, raw 8-bit micro-indices | Add install-time transcoders (topology codec, bit-packing, entropy) as transport only | Recommended refinement of B (model: about 16 to 21 B per leaf triangle, 2.5x to 2.9x smaller than float clusters) |
| D. Full streaming in M9 | Request path, install, eviction in the first cluster milestone | Tuning | Contradicts the roadmap's own deferral and no content needs it |

### Recommendation

Take option C. The reasoning, in the order it matters:

1. **Quantize in M9, but do not bit-pack or entropy-code in M9.** The grid, the octahedral and
   tangent contracts and the UV range are format-level decisions with visible quality effects, so
   the M9 image gates should run on the representation that will ship. Byte-aligned fields decode
   with no bitstream reader. My model puts this at 15.8 to 16.9 B per leaf triangle on Sponza
   and 19.2 to 21.0 on San Miguel at a 16-bit and a 21-bit grid (per leaf cluster: byte-aligned
   positions at the counted widths with a 12 B header, 10 B of assumed attributes per vertex, 3 B
   per triangle of raw micro-indices, a 32-byte cluster record), against a modeled 10.7
   and 13.4 for the fully packed form and 41.5 and 55.7 for float clusters. The difference between
   the packed and the byte-aligned forms is exactly the part that is safe to defer. About half
   of each byte-aligned figure is the assumed attribute size.
2. **Make the resident format shader-ready and the transport format free.** This is the choice
   Mlakar et al. and Nanite sit on opposite sides of. With a fixed-width resident format, anything
   that compresses better (the meshlet topology codec, bit-packing, byte-plane splitting, a built-in
   MTLIO codec per page) becomes an install-time transcode that runs on the CPU or a small compute
   kernel and never touches the cluster shaders again. If the resident form is the packed form, the
   decode shaders carry the format forever.
3. **Verify missing-page behavior without I/O.** Add a debug residency mask that marks groups
   non-resident in M9. The no-holes property (every region still draws, error bounded by the
   coarser level) and the determinism of the images can then be checked with the repository's
   existing exact-image machinery before any file is streamed. It also exercises the lookup design
   (generating-group reference) that streaming needs.
4. **Do texture-mip streaming first, then geometry, on one substrate.** Both need the same parts: a
   GPU-written request buffer, a readback ring matched to the three frames in flight, a request
   manager with a byte budget and an age or LRU rule, an IO scheduler over `MTLIOCommandQueue`, an
   install step ordered by events, and a retirement rule so a page is not overwritten while a
   frame in flight still reads it (the scene tables already retire old buffers three frames
   later). What differs is dependency (geometry pages have parent-before-child order; mip levels are
   naturally coarse-first) and fallback (a resident coarse level for both). Build the substrate
   generically over fixed-size pages with dependency lists and priorities, prove it on texture
   tiles, then attach geometry pages. Local evidence supports this order: the baked textures of the
   existing scenes already outweigh the geometry (176 MB against 12 MB in the current format for
   Sponza, and against a modeled 6 MB compressed).
5. **Treat Nanite-style vertex sharing, group parts and dynamic leaf flags as optional later
   features.** Each saves bytes or memory at the price of mutable pages or install-order
   constraints, and none is needed for correctness.

Where this leaves the roadmap text: M9's "Defer: Nanite-class virtualized geometry streaming"
remains right, but the M9 deliverable should add "a page-able baked cluster format with a group
residency table and a forced non-resident test" to its list, and M11's residency item should be
split into a substrate-plus-texture slice and a geometry-page slice. That is a suggestion for the
coordinator, not a plan.

## Open questions and what could not be confirmed

- No cluster page was actually encoded. Every compressed byte figure, group size, page count and
  fill rate is a model in which the 8 B per vertex of attributes and the header sizes are
  assumptions; only topology sizes and position bit widths come from real data. A bake that writes
  real pages is needed before any of these figures is quoted as a measurement.
- How much cross-level vertex sharing would save on this content. The model's 40% assumes 52 to
  54% of vertex entries become references; Epic reports about 30%, and its figure includes
  references inside one page, which the model does not count.
- Whether whole-group pages are good enough. Fill depends on the page order, and no order that
  also respects spatial locality or per-mesh page sets was tuned.
- Whether hierarchies should be built per glTF primitive (one material per cluster, coarse levels
  not guaranteed to match along borders between primitives) or per merged object (per-triangle
  material palette). The program did the former and did not look for cracks.
- The cost of the per-cluster residency lookup on Apple GPUs.
- When the two NVIDIA micro-mesh repositories were archived. The GitHub API reports only the
  archived flag and the last push (2025-02-13).
- Where MTLIO decompression executes on Apple Silicon, whether `loadBuffer` can target heap-placed
  or placement-sparse buffers, and the maximum single-buffer length and recommended working-set
  size on the recorded machine. The last two belong to the Metal constraints notebook; I did not
  measure them.
- Whether Apple Silicon treats CPU writes to shared buffers as write-combined (affects whether the
  meshopt decoders' write-combined-memory advantage applies).
- AMD's RDNA 5 DGF timing: only press coverage was seen; no primary architecture statement.
- The program uses plain `meshopt_simplify` without vertex position update. With
  `simplifyWithUpdate`, the shared-vertex savings fall and the bytes per level change; I did not
  measure that case. The attribute model (8 B per vertex) is not measured, and Sponza has no tangents
  in its glTF, so the tangent cost is a model value there.
- Solis' streaming is documented by one README paragraph; I did not read its source.
  light-system's `.vgeo` page layout is a design document (dated 2026-03-23) with open questions,
  not a finished format. Treat both as pattern evidence, not measurements.
- The unified memory size and Metal working-set limits of the recorded machine were not examined,
  so the worked sizes (2.3 to 2.8 GB modeled at 100 M triangles, 23 to 28 GB at one billion) are
  not checked against what fits.

## Sources

Primary sources read in full or in the parts cited, on 2026-10-01 unless noted.

meshoptimizer and cluster tooling:
- [meshoptimizer repository and README](https://github.com/zeux/meshoptimizer) (MIT; v1.3 tag commit
  `9e1f07b`), [releases](https://github.com/zeux/meshoptimizer/releases), `demo/clusterlod.h`,
  `demo/nanite.cpp`, `src/meshoptimizer.h`
- [Billions of triangles in minutes](https://zeux.io/2025/09/30/billions-of-triangles-in-minutes/)
  (2025-09-30)

AMD:
- [DGF-SDK](https://github.com/GPUOpen-LibrariesAndSDKs/DGF-SDK) (README, `DGFLib/DGF.h`, releases
  v1.0.0, v1.1.0, v1.2.0; SDK license text is an MIT-style notice, GitHub reports no SPDX id)
- [DGF paper, HPG 2024](https://gpuopen.com/download/publications/DGF.pdf) (Barczak, Benthin,
  McAllister; PACMCGIT 7(3), July 2024)
- [DGF Vulkan extension post](https://gpuopen.com/learn/dense-geometry-format-amd-vulkan-extension/)
  (2025-08-05)
- [AMD DGF overview](https://gpuopen.com/learn/amd-dgf-an-open-geometry-compression-standard/)
  (fetched, contained little technical detail)
- [Meshlet compression, GPUOpen](https://gpuopen.com/learn/mesh_shaders/mesh_shaders-meshlet_compression/)
  (2024-11-01)
- Reported only, not fetched: press coverage of DGF and RDNA 5, for example
  [VideoCardz](https://videocardz.com/newz/amd-details-dense-geometry-format-dgf-with-hardware-acceleration-support-for-upcoming-rdna5-gpus)

Papers and talks:
- Kuth et al., [Towards Practical Meshlet Compression](https://arxiv.org/pdf/2404.06359) (VMV 2024,
  arXiv:2404.06359v2)
- Mlakar, Steinberger, Schmalstieg, [End-to-End Compressed Meshlet Rendering](https://doi.org/10.1111/cgf.15002)
  (CGF 43(1), e15002, 2024; read from the open PDF at
  https://diglib.eg.org/bitstreams/eabce3df-023a-40a5-beeb-4c1743606691/download)
- Capcom, [RE ENGINE Meshlet Rendering Pipeline](https://enginearchitecture.org/downloads/REAC_2025_Capcom.pdf)
  (REAC 2025)
- Karis, Stubbe, Wihlidal, [A Deep Dive into Nanite Virtualized Geometry](https://advances.realtimerendering.com/s2021/Karis_Nanite_SIGGRAPH_Advances_2021_final.pdf)
  (SIGGRAPH 2021 Advances in Real-Time Rendering; slides 121 to 127 streaming, 128 to 144
  compression and results)
- [DirectX-Specs, Raytracing2](https://microsoft.github.io/DirectX-Specs/d3d/Raytracing2.html)
  (DXR2 clustered geometry and Compressed1 position encoding; spec in development)

Epic and Unreal Engine:
- [Nanite technical details](https://dev.epicgames.com/documentation/unreal-engine/nanite-technical-details?lang=en-US)
- Unreal Engine source, branch `ue5-main`, read 2026-10-01 for mechanisms only (licensed under the
  Unreal Engine EULA; nothing copied): `Engine/Shaders/Shared/NaniteDefinitions.h`,
  `Engine/Source/Runtime/Engine/Private/Rendering/NaniteStreamingManager.cpp`,
  `Engine/Source/Developer/NaniteBuilder/Private/Encode/NaniteEncodePageAssignment.cpp`,
  `Engine/Shaders/Private/Nanite/NaniteTranscode.usf`, `NaniteStreaming.ush`,
  `NaniteAttributeDecode.ush`

NVIDIA and open implementations:
- [vk_lod_clusters](https://github.com/nvpro-samples/vk_lod_clusters) (Apache-2.0): `README.md`,
  `docs/streaming.md`, `src/scene_cluster_compression.cpp`, `src/scene_streaming.hpp`,
  `shaders/shaderio_scene.h`, `shaders/traversal_run.comp.glsl`, `shaders/traversal_run_groups.comp.glsl`
- [Displacement MicroMap Toolkit](https://github.com/NVIDIAGameWorks/Displacement-MicroMap-Toolkit)
  (archived), [RTXMG](https://github.com/NVIDIA-RTX/RTXMG) (repository metadata only)
- [Nyx](https://github.com/moonlovelj/Nyx) (MIT): README, `MiniEngine/Model/GeometryStreaming.cpp`
- [Solis](https://github.com/Vovan675/Solis) (no license declared): README
- [light-system](https://github.com/usestemframework/light-system) (MIT): README,
  `schemas/PAGE_LAYOUT_SCHEMA.md`

Apple:
- [MTLIOCommandQueue](https://developer.apple.com/documentation/metal/mtliocommandqueue),
  [MTLIOCompressionMethod](https://developer.apple.com/documentation/metal/mtliocompressionmethod),
  [MTLSparsePageSize](https://developer.apple.com/documentation/metal/mtlsparsepagesize),
  [MTL4CommandQueue](https://developer.apple.com/documentation/metal/mtl4commandqueue),
  [Choosing a resource storage mode for Apple GPUs](https://developer.apple.com/documentation/metal/choosing-a-resource-storage-mode-for-apple-gpus)
  (all read through Apple's documentation JSON endpoint); the Metal headers of the installed macOS
  SDK (`MTLDevice.h`, `MTLIOCommandQueue.h`, `MTLIOCommandBuffer.h`, `MTLIOCompressor.h`,
  `MTLResource.h`, `MTL4CommandQueue.h`) and a two-line local query on macOS 26.7
- WWDC22 [Load resources faster with Metal 3](https://developer.apple.com/videos/play/wwdc2022/10104/);
  WWDC25 [Discover Metal 4](https://developer.apple.com/videos/play/wwdc2025/205/) and
  [Explore Metal 4 games](https://developer.apple.com/videos/play/wwdc2025/254/) (fetched as
  summaries; quotes are the summarizer's extracts of the session text)

Repository documents:
- [GPU-driven hybrid rendering roadmap](../../roadmap/gpu-driven-hybrid-rendering.md) (M9, M11)
- [Pipeline state of the art, M7 to M11](../2026-09-14-roadmap-review/pipeline-state-of-the-art-m7-m11.md),
  [open-source references](../2026-09-14-roadmap-review/open-source-references-2024-2026.md)
- Sibling notebooks in this review: `nanite-core-architecture.md`, `cluster-lod-construction.md`,
  `apple-metal-geometry-constraints.md`

Local measurements and model: the scratch program output for Sponza and San Miguel (settings and
the measured, derived and modeled split in "Method"), and file sizes of the fetched scenes. The
program is not part of the repository.
