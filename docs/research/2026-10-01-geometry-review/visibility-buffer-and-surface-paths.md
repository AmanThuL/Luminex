# Visibility buffer and surface paths

**Status**: Frozen — non-normative  
**Research date:** 2026-10-01

Evidence notebook for the [geometry direction review](../2026-10-01-geometry-direction-review.md),
covering visibility-buffer shading against forward and compact deferred shading, with the tile-based
Apple GPU case treated as the main question. It was collected in one pass over conference slide
decks, papers, Apple documentation and session transcripts, and a narrow read of Unreal Engine's
Nanite shading sources (branches `ue5-main` and `5.8`, read on 2026-10-01). Slide decks and papers
were downloaded and read as text; a few web pages were read through a fetch summary and are flagged
where a number depends on that. Items marked **[UNVERIFIED]** were not confirmed against a primary
source; recheck them before a plan depends on them. Statements marked *inference* are this
notebook's reasoning from the cited facts, not a source's claim.

Credit: Nanite and the other Unreal Engine systems described here are the work of Epic Games.
Unreal Engine source is copyright Epic Games, Inc. and was read under the Unreal Engine EULA; this
notebook describes mechanisms in its own words and reproduces no engine code, shader text or
comments. Unreal, Unreal Engine and Nanite are trademarks of Epic Games, Inc.

## Summary

- A visibility buffer pays for one thing above all: it moves the expensive shader off the
  rasterizer's 2×2 quads. The adopters that explain themselves state that motive (Nanite, Horizon
  Forbidden West, id Tech 8), and every measurement shows the gain growing as triangles shrink and
  vanishing on large triangles.
- Apple GPUs already resolve opaque visibility per pixel before shading, and Apple's own session
  calls the structure that does it "the Visibility Buffer". Overshading of hidden opaque surfaces,
  the classic argument for deferred paths, is therefore mostly absent in a Luminex forward pass.
  Two costs remain that hidden surface removal does not address: helper lanes on small triangles,
  and alpha-tested geometry.
- No published measurement compares forward, deferred and visibility-buffer shading on an Apple GPU.
  The public numbers are from NVIDIA desktop parts, Xbox Series X and PlayStation. Apple's shading
  language documents helper threads that run the fragment function to evaluate derivatives for
  quad groups of four, so the quad mechanism exists on Apple GPUs; what it costs is not published.
  A timing sweep on the development machine can measure it without building a new path and is the
  cheapest high-value measurement in this area.
- Luminex has one material model and binds five material textures per draw. For it, "material
  classification" is not a shader-permutation problem; it is a texture-access problem. A single
  resolve shader needs material textures reachable from one invocation, which the current argument
  tables (16 texture slots) do not provide.
- Attribute reconstruction from instance, primitive and barycentrics, with explicit texture
  gradients, is also what a ray hit needs. The M10 reflection work will require the same module
  and the same texture access, whichever opaque path wins.
- Recommendation: keep Forward+ and export thin guides as extra attachments of the forward pass
  (not a prepass); run two small probes first; build the visibility-buffer resolve only for cluster
  geometry, gated on the probe result and on content that actually has pixel-scale triangles.

## 1. Designs, what they store, and which shipped

| System | Status | Visibility payload | What shades from it | Dispatch | Source |
|---|---|---|---|---|---|
| Burns and Hunt, Intel Labs (2013) | Research | 4 bytes per sample: triangle index + instance ID; 8 bytes for large or tessellated scenes | One compute kernel per material does vertex load, interpolation, material and lighting, and writes final color | Per-material worklists of 16×8 pixel tiles; building them costs at most 10% of frame time | JCGT 2(2), read |
| Schied and Dachsbacher, "DAIS" (2015) | Research | Reference into a per-frame triangle buffer that stores a sample point plus screen-space derivatives | Deferred shading with known attribute derivatives | Not the focus | HPG 2015, abstract read |
| The Forge, Triangle Visibility Buffer 1.0 | Framework; no shipped title is named in its README | 32 bits: 1 alpha-mask bit, 8 draw ID, 23 triangle ID (2018 layout); later revisions drop the draw ID | One full-screen draw shades everything ("Forward++", tiled light lists) | None: a single bindless shading draw | Engel 2018 blog (via fetch summary); README |
| The Forge, Triangle Visibility Buffer 2.0 | Prototype (2024), "running on all platforms" | Same idea, filled by compute rasterization instead of draws | Same | Same | README release 1.57/1.58 |
| Nanite, UE 5.0 era | Shipped | Depth : instance ID : triangle ID in a 64-bit target | Material pixel shader per pixel writes the G-buffer; UE's deferred lighting follows | Full-screen quad per material, refined to an 8×4 tile grid, with a depth-equal test on a "material depth" target | Karis 2021 slides |
| Nanite, `ue5-main` and `5.8` | Shipped | 64-bit visibility buffer | Material compute shaders write G-buffer targets through unordered-access views | Compute shade binning, one indirect dispatch per shading bin; bindless shading groups under development | UE source (section 3) |
| Decima, Horizon Forbidden West (2022) | Shipped, PS4 and PS5 | 32 bits: 16-bit micro batch, primitive ID, instance ID; top 2 bits reused for shading rate | Compute writes the G-buffer; used for foliage and alpha-tested geometry only | Loose 128×128 tiles, pixels sorted into full 64-thread waves per shader | McLaren, GDC 2022 slides |
| id Tech 8, Doom: The Dark Ages (2025) | Shipped | 64 bits: 32-bit triangle index, 21-bit surface index, winding and instancing flags | Compute "deferred texturing" writes a G-buffer; a G-buffer update pass blends decals; tiled deferred lighting | Per-pixel command lists sorted by shader, tile and material; lighting uses 32×32 tile classification | Lazarek and Hammer, GPC 2025 |
| RE Engine, Dragon's Dogma 2 (2024) and Monster Hunter Wilds (2025) | Shipped | 64-bit atomic or R32G32Uint: signature bit(s), 24-bit culled-meshlet index, 7-bit triangle | A pixel-shader pass writes the G-buffer; deferred lighting follows | About 50 to 60 instanced draws, one per shader, restricted by a 32-bit mask over 8×4 screen areas | Mishima, REAC 2025 slides and Q&A |
| Wicked Engine | Open-source engine | 32 bits: 25-bit meshlet ID, 7-bit primitive ID | Nothing: opaque shading stays tiled forward. The buffer is a prepass output from which compute reconstructs depth, normal, roughness and velocity for effects | None | Turánszki, 2024-12 blog; `features.txt` |
| Hable (2021 blog, SIGGRAPH 2024) | Toy engine and demo | 32 bits: draw-call ID and triangle ID | Compute per material writes G-buffer data; lighting; optional 2× and 4× software shading rate with reconstruction | Count, prefix sum, reorder, then one indirect dispatch per material | Blog (via fetch summary); deck read |
| Bevy meshlets (0.14, 2024) | Experimental feature | 32 bits: 26-bit cluster, 6-bit triangle | Material fragment shader (forward, prepass or deferred flavor) | Full-screen triangle per material with depth-equal test | JMS55 blog (via fetch summary) |
| Call of Duty engine (2021 talk) | Shipped engine; titles not named in the abstract | Not stated in the abstract | "Mixes Forward+ with Visibility Buffer style rendering" | Not stated | Activision abstract only |
| Nanite on Mali, Arm demo (2026-03-18) | Vendor demo | Nanite's, built with 64-bit image atomics | Nanite's compute shading | Nanite's | Arm blog (via fetch summary) |

Three things in the table matter for Luminex.

**Most shipped systems do not shade the final color from the visibility buffer.** Nanite, Decima,
id Tech 8 and RE Engine all resolve the visibility buffer into a G-buffer and light it with the
deferred renderer they already had. Burns and Hunt, The Forge and Wicked's on-demand reconstruction
are the only designs here that go from identity straight to lit color or stay forward. Karis gives
the reason plainly: the material pass writes the G-buffer "to integrate with the rest of our
deferred shading renderer" (Karis 2021). id's slides say the split also keeps each compute shader's
register count low, and that they "had G-Buffer anyway" (Lazarek and Hammer 2025).

**Hybrids are the norm.** id Tech 8 describes its result as "a hybrid rendering approach:
Visibility Buffer / Deferred Rendering / Forward+", with Forward+ kept for transparents and some
opaque geometry. Decima applies the technique to foliage only. RE Engine keeps "special
rasterizers" for some alpha test, decals and two-sided cases. Hable's closing advice is to expect to
maintain the legacy forward or G-buffer path "forever".

**Wicked Engine is not a visibility-buffer shading renderer.** Its main opaque pass is tiled
forward; the ID target exists so that compute can rebuild surface guides without a rasterized
G-buffer prepass (Turánszki 2024). That makes it a reference for "forward plus guides", not for
visibility-buffer material resolve.

### Disclosures from 2025 and 2026

| Disclosure | Date | Relevance |
|---|---|---|
| id Tech 8 visibility buffer and variable-rate compute shaders (two GPC talks) | 2025 | The only shipped forward-to-visibility-buffer migration with a before/after table |
| RE Engine meshlet pipeline (REAC) | 2025 | Visibility buffer to G-buffer in a pixel shader; pre-transformed vertex cache |
| Indiana Jones and the Great Circle (REAC) | 2025 | Stayed Forward+; the opaque pass also writes a "mini g-buffer" (normal, albedo, smoothness) used by GI, SSR and subsurface composites |
| Space Marine 2 (REAC) | 2025 | Moved from Forward+ to deferred; material shader binaries shrank as a side effect |
| Hitman on iPhone (GPC) | 2025 | A deferred renderer shipped at 30 FPS on iPhone 15 Pro (A17 Pro) and newer, using concurrent compute dispatches for deferred shading; no forward/deferred comparison given |
| Nanite bindless shading in `ue5-main` and `5.8` | Commits 2026-01-19 (first commit, described as work in progress and disabled) to 2026-04-09 (cleanup) | One dispatch per shader group rather than per material instance |
| Nanite on Mali (Arm) | 2026-03-18 | First vendor article on visibility-buffer shading on a tile-based mobile GPU |
| SIGGRAPH 2026 Advances course | 2026-07-21 | No talk on surface paths; the program was path tracing, upscaling, variable-rate ray tracing, volumetrics, user-generated worlds and tessellation |

The Graphics Programming Conference 2026 schedule was not published when this was written.

## 2. Reconstruction

### 2.1 Getting the triangle identity

The visibility pass must output a per-triangle value that the rasterizer does not interpolate.
Hable's 2024 deck lists the options: the built-in primitive ID, non-indexed drawing, a "leading
vertex" index buffer in which every triangle's provoking vertex is unique to it, mesh-shader
per-primitive outputs, and a software rasterizer. He reports (quoting measurements by Sebastian
Aaltonen on NVIDIA, AMD and Intel desktop parts) that the built-in primitive ID cost roughly 1.5 to
1.8 times the leading-vertex layout on NVIDIA and AMD and the same on Intel integrated graphics,
while leading vertex stayed within a few percent of a plain indexed pass; he picks leading vertex
as the portable choice. Those are reported third-hand and none are Apple numbers. Decima rejected
the provoking-vertex trick because hardware rotated vertices, and instead passes three per-vertex
values combined with XOR (McLaren 2022). id Tech 8
writes the built-in primitive ID in the depth prepass's pixel shader and found the 64-bit export
free, because the pass was bound by vertex processing; a 128-bit export that added UV derivatives
and a tangent frame was measurably slower and was dropped (Lazarek and Hammer 2025).

On Apple hardware the built-in route exists: the Metal feature set tables list "Primitive ID" and
"Barycentric coordinates" for GPU family Apple7 and later, which is A14 and every M-series chip
(Metal Feature Set Tables, PDF dated 2026-06-02). Its cost on Apple GPUs is unmeasured in public.

### 2.2 Attributes from an ID and barycentrics

The resolve reads the ID, maps it to an instance row and a triangle, loads three indices and three
vertices from the scene's vertex pool, and transforms the three positions to clip space with the
instance's matrix. Everything else follows from those three clip-space positions and the pixel
position. With `c_i = (x_i, y_i, z_i, w_i)` and screen positions `s_i = c_i.xy / w_i`:

1. Screen-linear weights. With `e1 = s_1 - s_0`, `e2 = s_2 - s_0` and `D = e1.x*e2.y - e1.y*e2.x`,
   the weights at pixel `p` are `m_1 = cross(p - s_0, e2) / D`, `m_2 = cross(e1, p - s_0) / D` and
   `m_0 = 1 - m_1 - m_2`. They are affine in `p`, so their gradients are constants of the triangle:
   `grad m_1 = (e2.y, -e2.x) / D`, `grad m_2 = (-e1.y, e1.x) / D`, `grad m_0 = -(grad m_1 + grad m_2)`.
2. Perspective correction. With `n_i = m_i / w_i` and `d = n_0 + n_1 + n_2`, the perspective-correct
   barycentrics are `b_i = n_i / d`. The value `d` is the interpolated `1/w`, so depth comes for
   free.
3. Any attribute is `a = b_0*a_0 + b_1*a_1 + b_2*a_2`.

This is the classic software-rasterizer formulation; id cites Hecker's 1995 articles for it. Hardware
barycentrics, where available, replace steps 1 and 2 for the value but not for the gradient.

### 2.3 Texture-coordinate derivatives

A compute shader has no implicit derivatives, and a full-screen fragment shader has them but across
the wrong neighbors. Both `n_i` and `d` above are affine in screen space, so the quotient rule gives
the exact gradient of each barycentric:

`db_i/dx = ((dm_i/dx) / w_i * d - n_i * dd/dx) / d^2`, with `dd/dx = sum_j (dm_j/dx) / w_j`,

and the same in `y`. The UV gradient is then `duv/dx = sum_i (db_i/dx) * uv_i`, scaled from
normalized device units to one pixel (`2/width`, `2/height`). The texture is sampled with explicit
gradients. The work is one reciprocal per vertex, one 2×2 determinant and a few dozen multiply-adds,
computed once per pixel and reused by every attribute. Hable's code for this was "donated by James
McLaren and Stephen Hill" and follows the DAIS paper; The Forge and Bevy use the same derivation.

Anything between the interpolated UV and the sample must carry its derivative by the chain rule.
Nanite does this in the material translator: gradients are "propagated through the artist created
node graphs automatically using the chain rule", falling back to finite differences where an
operation is not differentiable, at a measured overhead under 2% of the material pass (Karis 2021).
Hable shows it is tractable with generated material code but calls the general case "maybe" and
names parallax occlusion mapping and refraction as hard cases. Capcom reports that game-team shader
code is the unsolved part: triplanar and height-map materials fall back to an explicit mip level,
and "automatic solutions for shader derivative issues" is listed as future work (Mishima 2025).

Variants seen in the sources:

| Variant | Who | Tradeoff |
|---|---|---|
| Finite differences in a full-screen material fragment shader | Nanite's 2021 console path | Free and quads span triangles, but differences across depth edges, UV seams and objects are "nonsense and often huge", so analytic gradients replaced them |
| Analytic gradients recomputed inline | Hable, The Forge, Bevy, Nanite | No storage; a little arithmetic per pixel |
| Barycentrics and their derivatives stored by a separate pass | id Tech 8: RG16F barycentrics, packed RGBA16UI derivatives clamped to (−2, 2), RGBA16F tangent frame | Keeps the texturing shader's register count low; costs about 159 MiB at 4K and 75 MiB at 1440p for the three targets |
| Derivatives exported by the visibility pass ("fat" buffer) | Tested and rejected by id | Export bandwidth became the bottleneck |
| Per-triangle sample point and derivatives in a triangle buffer | DAIS | Compact with multisampling |
| Compute-shader derivative extension | Nanite's quad-mode binning; `VK_KHR_compute_shader_derivatives` on Mali | Restores implicit derivatives in compute by laying threads out as 2×2 quads, with helper lanes again |

For Luminex the chain is short: one UV set, one affine UV transform per material, five texture
reads. The gradient of the transformed UV is the transform's linear part applied to `duv/dx`. There
is no material graph to differentiate.

One cost is platform-dependent and unverified for Apple. Aaltonen states that on mainstream mobile
GPUs explicit-gradient sampling is slow, "⅛ rate or even slower", and that dynamic buffer loads are
slow, "over 20 non-uniform memory loads in the pixel shader" for a visibility-buffer resolve
(Aaltonen 2023). He was describing phone-class Android GPUs, not an M3 Max. **[UNVERIFIED]** for
Apple GPUs in either direction.

### 2.4 Vertex re-transformation and caches

Three vertices are fetched and transformed per pixel. With rigid instances the cost is three matrix
multiplies and is small; Hable: "Matrix mul cost is quite small. Vertex animation quickly adds up."
Engines with expensive vertex shaders cache:

- RE Engine runs a compute pass that pre-transforms the vertices referenced by visible primitive
  IDs, compacted per wave when a wave shares a primitive, before the G-buffer pass. The stated
  reason is that "computing vertex transforms for all three vertices per pixel would be too
  computationally expensive". Worst-case storage is resolution × 3 vertices × vertex size, held in
  aliased transient memory (REAC 2025 Q&A).
- Decima transforms vertices on a second compute pipe into a ring buffer of 12 MB on PS4 and 24 MB
  on PS5, in passes limited to half the ring (McLaren 2022).
- The Forge pre-skins animated vertices into a buffer (README, release 1.54).
- id Tech 8 and Nanite recompute inline or in the attribute-interpolation pass.

Burns and Hunt noted that these loads are "highly coherent in screen-space" and bounded by visible
triangles: two million visible micro-polygons with a 32-byte vertex is about 88 MB of vertex and
index data, still less than a multisampled G-buffer.

### 2.5 Alpha-tested geometry

Coverage must be decided in the visibility pass, because the stored ID has to belong to the surface
that survives the test. Every source handles it the same way: a cheap fragment shader that samples
only the opacity texture and discards.

- Decima built its whole system for this case. The previous game used a depth prepass with an
  alpha-only shader followed by a depth-equal geometry pass; that "transforms all the geometry
  twice" and "suffers from major issues with quad overdraw". With deferred texturing the count of
  pixel-shading waves on PS5 at 4K fell to 40% (forest) and 25% (tall grass) of the depth-equal
  approach (McLaren 2022).
- The Forge reserves a visibility-buffer bit for alpha-masked geometry and keeps separate indirect
  draw sets for opaque and masked geometry (Engel 2018).
- Monster Hunter Wilds merged alpha testing and vertex-transform variants into the hardware
  visibility rasterizers, sorted by shader ID (Mishima 2025).
- Nanite evaluates masked materials in programmable raster bins; `FNaniteRasterPipelines::AllocateBin`
  takes a per-pixel-evaluation flag (`NaniteShading.cpp`, `ue5-main`).

### 2.6 MSAA, motion vectors, skinning

**MSAA.** Burns and Hunt's design is multisample-first: iterate the samples of a pixel, shade one
sample per unique triangle, weight by sample count. The Forge ships a "programmable MSAA" variant
(release 1.63). No game in the table uses MSAA with its visibility buffer; all rely on temporal
reconstruction. Luminex is temporal as well, so this is a door that closes without cost today.

**Motion vectors.** Nanite's depth-export pixel shader computes velocity from the visibility buffer
alongside depth and the shading mask (`EmitSceneDepthPS` in `NaniteExportGBuffer.usf`). RE Engine
stores the previous-minus-current homogeneous position (10/10/12 bits) in the pre-transformed vertex
record. For a rigid instance the resolve can interpolate the object-space position once and
transform it by the current and previous matrices, which is what a Luminex resolve would do with
the instance row's `model` and `previousModel`.

**Skinned geometry.** The resolve needs the current and previous skinned positions of three
vertices. All sources that mention it pre-skin into a buffer. A ray-tracing acceleration structure
needs the same buffer, so this cost is shared with M10 rather than specific to the surface path.
Luminex has only rigid animation today.

## 3. Material dispatch

A resolve must run the right code with the right resources for each pixel. Five schemes appear.

| Scheme | Mechanism | Reported cost or limit | Who |
|---|---|---|---|
| Full-screen pass per material, depth-equal | Write a material ID into a second depth target; draw one full-screen primitive per material at that depth with an equal test | Draws are issued for every material whether visible or not. id measured no gain over Forward+ on an RTX 5080 at 4K (14.26 ms against 14.3 ms) because hierarchical depth cannot reject an unordered ID field and fine rasterization became the bottleneck. Bevy: "each fullscreen triangle will cost an entire screen's worth of depth comparisons" | Dawn Engine, Nanite 2021, Bevy, id's first attempt |
| The same, restricted by a coarse screen mask | A 32-bit mask over an 8×4 grid (or 64×64 without wave operations) built with the material depth; the vertex shader kills unmarked tiles | Console path of the 2021 Nanite demos; RE Engine uses the same mask idea with 50 to 60 draws and reports a compute version "still couldn't beat the pixel shader's speed" | Nanite 2021, RE Engine |
| Tile classification | Per-tile bitmask of features present; one indirect dispatch per feature combination over the tiles that need it | Fits per-pixel variation in shader features, not in resources. Burns and Hunt used 16×8 tiles per material; id uses 32×32 tiles for lighting, G-buffer update and subsurface workloads | Burns and Hunt, id Tech 8 lighting |
| Compute binning by material | Count pixels per bin, prefix-sum to offsets, scatter pixel coordinates, then one indirect dispatch per bin; pad or sort so that each hardware wave is uniform in material | id: 0.6 ms to build, 10.4 ms total against 14.3 ms for Forward+ in the same scene. Decima sorts into full 64-thread waves over 128×128 tiles after 8×8 tiles "suffered from unfilled tiles" | Hable 2021, Decima, id Tech 8 texturing, Nanite today |
| Single bindless resolve | One shader reads material parameters and textures through indices; no classification | Requires every material to share shader code, or dynamic branching; divergent texture access inside a wave | The Forge; Nanite's bindless shading groups approach it per shader |

**Nanite's current stage, from source.** In `ue5-main` and `5.8` a full-screen pixel shader reads
the 64-bit visibility buffer and writes scene depth, velocity and a per-pixel "shading mask" that
carries the shading bin (`NaniteExportGBuffer.usf`). `NaniteShadeBinning.usf` then runs as a count
pass, a reserve pass and a scatter pass over 2×2 pixel quads, writing packed coordinates into a
per-bin range of one data buffer and filling indirect dispatch arguments. Elements are whole quads
when the material's compute shader uses derivative operations and single pixels when it does not;
`r.Nanite.ShadeBinningMode` forces either, and the source tracks a helper-lane count for quads.
Fully covered tiles and loose elements fill a bin's range from opposite ends. A shading-rate image
can merge pixels (`r.Nanite.SoftwareVRS`, default on). Each bin is shaded by an indirect compute
dispatch that writes the G-buffer targets through unordered-access views
(`DispatchIndirectComputeShader` in `NaniteShading.cpp`), with thread groups of 32 or 64. An optional
"shader bundle" path batches the dispatches (`r.Nanite.Bundle.Shading`, default off). None of the
fetched shading files reference the 2021 material-depth target, so the depth-equal path described in
the 2021 talk is gone from this stage; the 2021 slides had already said the area "will likely
switch to completely compute". A `BINDLESS` permutation issues one command per pipeline group
instead of per pipeline; its first commit message describes it as work in progress and disabled.

**Wave uniformity is the hidden requirement.** id's slides explain why binning must keep each wave
uniform in material: in a forward draw, material constants and textures are uniform across a
pixel-shader wave and the compiler exploits that; a compute resolve loses it unless the dispatch
preserves it. They additionally compile each texturing shader twice, for uniform and divergent mesh
data within a wave, and select with a subgroup test.

### What each scheme does to Luminex's pipeline variants

Luminex today has four mirrored scene shader files (opaque and masked, each with a manual-exposure
and an auto-exposure twin), each with a plain and a motion entry point, and builds its scene
pipelines over double-sided, exposure, motion and wireframe axes per alpha mode. Material textures
are a per-draw set of five; the vertex shader already pulls vertices from one pooled buffer by
index. The RHI has no stencil attachment support.

| Scheme | Shading variants | New variants | Blocking requirement |
|---|---|---|---|
| Keep forward | Unchanged | None | None |
| Per-material full-screen pass | One resolve shader, drawn once per visible material with that material's five textures bound. Masked and opaque share it, because coverage was decided earlier. Motion needs no twin: the resolve always knows both transforms | Two ID-only visibility shaders (opaque; masked with an opacity read), over cull mode | A material-depth target (stencil is unavailable and too narrow for San Miguel's 281 materials). 25 draws for Sponza, 281 for San Miguel |
| Tile classification | No use: one BRDF and one feature set | None | None |
| Compute binning by material | One resolve kernel, dispatched per material | The same two visibility shaders, plus count, prefix and scatter kernels | Analytic gradients; texture binding per dispatch; kernels that cannot keep data in tile memory |
| Single bindless resolve | One resolve shader, one draw or dispatch | The same two visibility shaders | Material textures addressable by index from one invocation. The RHI's argument tables expose 16 texture slots and no texture arrays, so this needs new RHI surface |

Two consequences. First, a visibility-buffer resolve collapses the opaque/masked and motion/plain
twins into one shading function by construction, which is the deduplication the deferred R4
experiment was after. Second, that only reduces the total if the forward shaders can be retired,
and they cannot: the roadmap retains Forward+ as the oracle, and M8's sorted transparency shades
forward. While both exist the variant count goes up, not down.

## 4. Forward from the visibility buffer, or visibility buffer to G-buffer to deferred

| Question | Shade to final color from the visibility buffer | Resolve to a G-buffer, then deferred lighting |
|---|---|---|
| Who does it | Burns and Hunt; The Forge ("Forward++") | Nanite, Decima, id Tech 8, RE Engine, Hable's demo |
| Shader size | One large shader: reconstruction, material and lighting | Two or three smaller ones; id split for register pressure and compile quality |
| Storage | Visibility target only | Visibility target plus G-buffer; id reports 268 MiB at 4K and 115 MiB at 1440p for the deferred path's buffers |
| Decals and surface modification | Must be clustered and evaluated inside the shader, as Doom Eternal did under Forward+ | Blend into the G-buffer between resolve and lighting, as id Tech 8 does for geometry decals, projected decals, blood and wetness |
| Guides for AO, SSR, GI | Emitted as extra outputs of the resolve | Already present |
| Light-loop compatibility with a Forward+ renderer | Reuses the clustered light loop unchanged | Also reuses it; id: "same clusters and very similar shader code as F+" |
| Capcom's answer when asked why not shade directly | "No, we haven't tried that yet" | Current path |

For a renderer that is Forward+ today, shading to final color from the visibility buffer keeps one
lighting implementation and one set of outputs, and it is the smaller step. What it gives up is the
G-buffer as a place to blend decals and as a boundary that keeps the resolve shader small. On an
Apple GPU the shader-size concern is unmeasured; id's slides express it in vector registers per
workload (78 to 122), a measure specific to the GPU architecture they profiled.

Indiana Jones is the instructive counter-case: a recent title that weighed deferred and stayed
Forward+ because of "a few different BRDFs, as well as some shaders doing light calculations in
custom ways", while writing a mini G-buffer from the forward pass for everything downstream
(de Vahl, REAC 2025).

## 5. The tile-based GPU question

### 5.1 What Apple documents

**Hidden surface removal is a hardware visibility buffer.** Apple's WWDC20 session describes it in
those words: during rasterization "each pixel will keep the depth and primitive ID for the frontmost
primitive", fragment shaders do not run until that is settled, and at the end "the remaining pixels
in the Visibility Buffer" are flushed to the shader cores. It is "both pixel perfect and submission
order independent" for opaque geometry (Harness Apple GPUs with Metal, 2020). Burns and Hunt made
the same observation from the other side in 2013: PowerVR hardware resolves visibility into a
tile-sized tag buffer, and their technique "can be viewed as a full-screen application-layer
implementation of this strategy".

**A depth prepass for performance is redundant.** "If you only perform a depth pre-pass for
performance, then hidden surface removal serves the same purpose on Apple GPUs", without processing
geometry twice and without needing the depth attachment to be stored (Optimize Metal Performance for
Apple silicon Macs, 2020). The same session keeps the prepass legitimate when a technique needs
visibility information before the main pass.

**Alpha test defeats it.** Fragments from functions that discard or write depth are "feedback
fragments"; "alpha tested foliage falls into this category". The recommended order is opaque, then
feedback, then translucent, and draw order matters within the feedback group. Also: "if we discard
or update the depth, we will need to go back to HSR". Two further efficiency traps are named:
fragment functions that write buffers or textures other than attachments are executed even when
occluded unless marked for early fragment tests, and any opaque draw that leaves an attachment
unwritten forces underlying fragments to be shaded (so every opaque shader in a pass should write
every attachment).

**Apple's recommended deferred form is single-pass.** Because fragment functions can read attached
render targets from tile memory ("programmable blending"), the G-buffer and the lighting can share
one render pass, the G-buffer textures can be memoryless, and nothing is stored or reloaded. Apple
calls classic deferred with programmable blending "a good default choice for your games" (Modern
Rendering with Metal, 2019). Tile shaders extend this to work that needs a whole tile at once, such
as building light lists mid-pass, and can read imageblocks, threadgroup memory and device memory.
The feature tables list programmable blending and memoryless targets from Apple2, and tile shaders
and imageblocks from Apple4.

**Apple positioned the visibility buffer as the answer for GPUs without tiling.** The 2019 session
introduces it as tackling G-buffer overhead "in a different way, making it more suitable for old
hardware that does not support hardware tiling", and says "the visibility buffer technique is only
supported by the Mac family", which in 2019 meant Intel-based Macs. The support statement is
out of date (primitive ID and barycentrics are now Apple7 and later), but the positioning is the
only guidance Apple has published on the technique, and it points away from it on Apple GPUs.

### 5.2 What hidden surface removal does not remove

**Helper lanes.** Derivatives require fragments to be shaded in 2×2 quads belonging to one
primitive. A triangle that covers one pixel of a quad still runs four lanes. id measured this as
the main loss in its Forward+ renderer: an example triangle with 14 active and 18 helper threads is
43% utilized, a smaller one 25%. Hable's figures are 83% to 32% for triangles of about ten pixels
depending on shape, and 25% at one pixel, which he describes as "similar to overdraw". Hidden
surface removal decides which primitive owns each pixel; it does not change the fact that each
owning primitive is then shaded in quads. A triangle with one surviving pixel is a worse case than
on an immediate-mode GPU, not a better one (*inference*).

Arm documents this for its own tile-based GPUs: "On Mali, as on most GPUs, fragments are shaded in
quads", and "Recent Mali GPUs mitigate the problem with Hidden Surface Removal" (Arm's 2026 Nanite
article, via fetch summary); its performance-counter guides, seen only as search excerpts, add that
lanes outside the triangle still consume shader resources and that micro-triangles are
disproportionately expensive. Apple documents the mechanism in its shading language rather than in
a performance guide: fragment function helper threads "may be created to help evaluate derivatives
(explicit or implicit)", they "execute the same code as the other fragment threads" without side
effects on render targets or memory, and they "only execute to compute gradients for quad-groups in
a fragment shader", a quad-group being a SIMD-group with an execution width of 4;
`quad_is_helper_thread()` identifies them (Metal Shading Language Specification 4.1, sections
6.10.3 and 6.11.1). So partially covered quads do carry helper lanes on Apple GPUs whenever the
fragment function takes derivatives, which every texture sample with implicit gradients does. What
Apple does not publish is what those lanes cost relative to covered pixels, or whether the hardware
omits them for functions that take no derivatives; that part is **[UNVERIFIED]** and is what
stage 0 in section 6 measures.

**Masked overdraw.** A masked fragment that passes the depth test must run its fragment function to
learn its coverage. Luminex's masked scene shader performs the discard and the full lighting in the
same function, so every masked layer that is not behind already-resolved opaque geometry is fully
shaded (*inference* from the shader and Apple's feedback-fragment description). San Miguel is the
exposed workload: in the locally converted asset, 97 of 281 materials are masked and they carry
1,461,755 of 5,617,451 triangles, 26%.

**Tiling cost.** All vertex work happens in the tiling phase and its output goes to the tiled vertex
buffer in system memory; overflowing it forces a partial render (Harness Apple GPUs with Metal).
Dense geometry stresses that phase whatever the fragment path is. A visibility buffer does not help
here; a two-pass scheme makes it worse.

### 5.3 Measurements found

There is no forward-versus-visibility-buffer comparison on any Apple GPU, or on any tile-based
deferred GPU, in the public sources this review reached. What exists:

| Measurement | Platform | Result | Bearing |
|---|---|---|---|
| Hable 2021 synthetic, as recapped in the 2024 deck | RTX 3070, 1080p, prepass on | Large triangles: forward 2.19 ms, deferred 2.41, visibility 2.95. One-pixel triangles: forward 10.55, deferred 7.01, visibility 4.72. Material cost ratio tiny/large: forward 5.60×, deferred 4.35×, visibility 1.52×. The blog's own tables (via fetch summary) give different totals in the same order: 2.38/2.57/3.01 ms for large triangles, 5.15/4.97/4.15 at 8 to 10 pixels per triangle, 15.0/11.2/8.85 at one pixel, with a fixed visibility cost of 0.32 to 0.34 ms | Defines the crossover: visibility loses on large triangles, is already ahead near ten pixels per triangle in the blog's middle case, and wins by about 2× at one pixel per triangle |
| Hable 2024 "March of the Monkeys" | RTX 3070, 1080p, trivial material, prepass always on | Forward 2.47 ms, deferred 1.94, visibility 1.67 at 1×, 1.44 at 2×, 1.35 at 4× shading rate | Dense scene with LODs; visibility wins at 1× |
| id Tech 8, dense scene | RTX 5080, 4K | Forward+ 14.3 ms, quad dispatch 14.26, compute dispatch 10.4 | −27% with compute dispatch; none with full-screen quads |
| id Tech 8, large ground triangles | Same | Forward+ 2.75 ms, compute dispatch 2.7 | No gain on large triangles, "but also not worse" |
| id Tech 8 "Siege" scene | Xbox Series X, 2560×1440 | Forward+ opaque 9.43 ms; deferred 7.5 (−20%); with variable-rate compute 7.0 (−25%). At half the pixel count: forward 6.4, deferred 3.9 | Deferred scales with resolution better (−48% against −33%) |
| Decima foliage | PS5, 4K | Pixel-shading waves at 40% (forest) and 25% (tall grass) of the depth-equal method | Masked foliage is where it pays most |
| RE Engine | PS5, 1664p | Visibility pass 1.7 ms and G-buffer pass 8.0 ms (Dragon's Dogma 2); 3.1 and 5.9 (Monster Hunter Wilds) | Absolute costs; no forward baseline |
| Nanite 2021 demo | PS5, about 1400p | About 2.5 ms to draw the visibility buffer, about 2 ms for the material pass | Absolute costs |
| Burns and Hunt | 2013 Intel, AMD and NVIDIA parts, 1080p | "Little to no net benefit" at one sample per pixel; gains grow with samples per pixel, most on bandwidth-limited integrated GPUs with large caches | The original result was a bandwidth result, not a quad result |
| Nanite on Mali | Mali, unnamed device | 634K-triangle mesh: 199.36 ms without Nanite, 26.16 ms with (via fetch summary) | Shows the whole Nanite pipeline is viable on a tile-based GPU; does not isolate the surface path |
| Tile-shader texture encode | iPhone 8 to M4 MacBook Pro | Tile-shader path 1.21× to 1.65× faster than the compute path; memory traffic 3 bytes per pixel against 11 (Castaño 2026, via fetch summary) | Only Apple-GPU numbers found on keeping work in tile memory rather than compute |

Aaltonen's 2023 assessment is the strongest published argument against the technique on mobile-class
tile-based GPUs: slow dynamic buffer loads, slow explicit-gradient sampling, no 64-bit atomics, and
"no current mobile GPU supports framebuffer compression for compute shader writes", so a compute
material pass "wastes a lot of bandwidth". Arm adds that moving work to compute reduces the overlap
of fragment and non-fragment work that a tile-based GPU otherwise gets. Each point is about phone
hardware; an M3 Max (Apple9) has 64-bit atomic minimum and maximum, the one operation a software
rasterizer needs (feature tables, footnote 7; the Apple notebook's probe found no other 64-bit
atomic in MSL), and far more bandwidth, and nobody has published how the rest transfers.

### 5.4 When a visibility buffer should still pay on an Apple GPU

This subsection is *inference* from the facts above.

On an immediate-mode GPU a visibility buffer removes three costs: shading of later-occluded
fragments, a second geometry pass (the prepass), and helper lanes on the expensive shader. On an
Apple GPU a single forward pass already avoids the first two for opaque geometry. What a visibility
buffer can still buy is:

1. **Helper lanes**, at whatever cost stage 0 finds they carry on this hardware. The gain scales with
   `1 / quad utilization`, so it needs triangles of a few pixels or less. At 2560×1440 Sponza's
   262K triangles are far too coarse. San Miguel's 5.6M triangles are not: a view that keeps a
   third of them puts the average near two pixels per triangle. Cluster LOD tuned to pixel-scale
   error makes this the normal case.
2. **Masked geometry**, by reducing the feedback shader to an opacity read and shading the survivor
   once. A cheaper control exists (an alpha-only pass followed by depth-equal forward shading, which
   is what Horizon Zero Dawn did), so the visibility buffer has to beat that, not the current path.
3. **Expensive shading at a reduced rate.** Apple GPUs have rasterization rate maps but no
   screen-space shading-rate image [UNVERIFIED in detail], so software variable-rate shading is a
   compute-side technique and needs a deferred or visibility path.

What it costs on an Apple GPU that it does not cost elsewhere:

1. If the resolve is a compute pass, the visibility target and every output leave tile memory, and
   the pass cannot overlap the way fragment work does.
2. Three vertex loads and an instance-row load per pixel, plus explicit-gradient sampling, at
   unknown relative cost.

An Apple-native formulation avoids the first cost and has not been described in any source found:
rasterize the ID into a memoryless attachment, then draw one full-screen primitive in the same
render pass whose fragment function reads the ID from tile memory through programmable blending and
shades. It has the shape of Apple's own single-pass deferred (G-buffer draws, then a full-screen
lighting draw that reads the attachments in the same encoder) with a 4- or 8-byte ID in place of
the G-buffer. The cheap ID shader absorbs the helper lanes, the full-screen primitive shades in
complete quads, and the visibility target never reaches system memory. The
[Apple notebook](apple-metal-geometry-constraints.md) settles part of it: Slang's `SubpassInput`
lowers to a `[[color(n)]]` framebuffer-fetch parameter and compiles at the pinned compiler; tile
shaders and imageblocks have no Slang stage; and a pass cannot sample a texture it rendered earlier
in the same pass, so the read must be a framebuffer fetch. Not probed: reading an integer attachment
that way, and the resolve's texture access, which in one full-screen draw is the single-bindless
case of section 3 and needs new RHI surface before it can shade more than one material. It is the
variant most worth testing, and it remains a hypothesis, **[UNVERIFIED]** as a working design.

## 6. What a fair opaque-path experiment measures

The roadmap asks to compare "compact/tile-local deferred and visibility-buffer material
reconstruction/classification". With one material model there is nothing to classify. The
experiment that the evidence supports is: *where does the quad and coverage cost land on an Apple
GPU, and what does reconstruction cost in return.* It decomposes into stages of rising cost, each
able to stop the next.

**Stage 0, quad-cost probe (no new path).** Sweep triangle size in a generated lab from about 100
pixels down to below one at a fixed count of covered pixels, opaque only, and time the existing
forward pass twice per size: with the full shader and with a trivial fragment function. The
difference is fragment-shading cost, which is how Hable and id isolated their material columns. If
it stays flat down to one-pixel triangles, Apple hardware does not pay for helper lanes and the main
argument for a visibility buffer is gone; if it rises toward four times, their premise holds on this
hardware. The per-pass timing exists already. Two supporting instruments: Apple's GPU counters
report pixels rasterized, fragment shader invocations, pixels stored and pre-Z test failures, and
Apple defines overdraw as invocations over pixels stored (Optimize Metal apps and games with GPU
counters, 2020), but on the development machine the Metal API exposes only the timestamp counter
set, sampled at stage boundaries (local probe of the device's counter sets), so they are Xcode
capture evidence, and the session does not say whether invocations include helper threads. A lab
shader variant can instead count the lanes for which `quad_is_helper_thread()` is true and
accumulate the count from an active lane, since helper-thread stores have no effect (Metal Shading
Language Specification 4.1, section 6.11.1). A manual capture is acceptable evidence for a
go/no-go.

**Stage 1, masked control.** Add an alpha-only depth pass for masked materials followed by
depth-equal shading with the opaque pipeline, and compare it with today's single-pass discard on
San Miguel. This isolates how much of that scene's cost is masked overdraw, with no reconstruction
code.

**Stage 2, visibility resolve for opaque geometry**, in the tile-local form of section 5.4 and in
compute form, sharing the forward path's shading functions.

**Stage 3, compact tile-local deferred**, only if stage 2's breakdown shows lighting rather than
material evaluation dominating.

Variables, each varied alone against a fixed base:

| Variable | Range | Why |
|---|---|---|
| Triangle density | Pixels per triangle from about 100 to 0.5 at fixed resolution | The one variable every source says decides the outcome |
| Masked fraction | 0; San Miguel's 26% of triangles; a foliage-only lab | Decima's whole case |
| Material count | 1; 25 (Sponza); 281 (San Miguel) | Cost of per-material dispatch; texture-access divergence in a single resolve |
| Light count per froxel | 0; Sponza's authored 16; LightLab's pile up to the 128-per-froxel cap | Shader cost multiplies helper-lane waste |
| Resolution and render scale | 0.5 to 1.0 render scale at two output sizes | id found deferred scales with pixel count better than forward |
| Depth complexity | VisibilityLab occluder count | Confirms hidden surface removal keeps forward flat; a control |

What to time: for forward, the scene pass. For the visibility path, separately: visibility raster,
any vertex cache, any binning, resolve, and guide export. For compact deferred: G-buffer fill and
lighting. Then report **whole-frame GPU time and frame interval** beside the per-pass sums. Apple
GPUs overlap the tiling of one pass with the rendering of another and run independent compute
alongside; a path that moves work into compute changes that overlap, so a sum of pass times can
rank the paths differently from the frame. id reports the same caveat for async compute and derives
its headline from whole-frame differences.

Memory and bandwidth: bytes per pixel stored to system memory for each path (forward today stores
half-float color, RG16Float motion, R8Unorm reactive and depth), attachments that can be memoryless,
transient buffers, and the visibility target's size (4 or 8 bytes per pixel). id's table is the
template: every buffer listed, at two resolutions.

Image agreement between paths cannot be byte equality, and the gate should say so in advance.
Analytic and hardware gradients select slightly different mip footprints, and manual interpolation
rounds differently from the rasterizer's. A criterion that can pass honestly, with temporal
reconstruction off and jitter fixed:

1. Identity is exact: the visibility target equals an independent ID render pixel for pixel. The
   occlusion work already has an ID oracle to build this from.
2. Coverage is exact: masked coverage in the visibility pass equals the forward pass's, since the
   same opacity read and cutoff decide both.
3. Reconstruction is bounded: reconstructed barycentrics against hardware barycentrics, depth against
   the depth attachment, and UV gradients against hardware derivatives on triangle interiors, each
   within a declared tolerance, plus a histogram of selected mip level differences.
4. Color is bounded, not equal: a perceptual difference map and a maximum absolute difference
   against the forward reference on the material and mip lab scenes, with thresholds declared before
   the run.

## 7. Downstream consumers

| Consumer | Needs | Forward plus guides | Visibility buffer | Compact G-buffer |
|---|---|---|---|---|
| M8.3 GTAO | Depth, normal | Normal as one extra attachment of the forward pass | Reconstructed in the resolve or, as Wicked does, by a compute pass from the ID target | Present |
| M8.3 SSR | Depth, normal, roughness, previous color | Roughness packed with the normal | Same as above | Present, if stored rather than memoryless |
| AO that must not darken direct light | Indirect and direct separated, or AO known at shading time | Either a prepass (so AO precedes shading), last frame's AO reprojected, or a deferred indirect composite that needs albedo in the guides | Same choices; the resolve is a natural place to split outputs | Natural: lighting terms are separate passes |
| M10 reflections | A ray origin and direction from guides; material evaluation at a hit from instance, primitive and barycentrics, with an explicit mip choice | Guides suffice for launch. Hit shading needs the attribute-reconstruction module and index-addressable material textures regardless | Shares that module and that texture access with the resolve | Same as forward for hits |
| Decals | Somewhere to blend | Clustered decals in the forward shader, as Doom Eternal shipped | Clustered in the resolve, or blended if a G-buffer exists | G-buffer blending, as id Tech 8 does |
| Variable-rate or decoupled shading | A shading step that is not the rasterizer | Not available; rasterization rate maps are the Apple alternative | Software rate in the resolve: id reports about 10% of GPU time (1 to 2 ms); Hable 1.67 to 1.35 ms from 1× to 4× | Software rate in lighting only |
| MSAA | Per-sample visibility | Works, and Apple's implementation is efficient in tile memory | Custom per-sample resolve | Custom |

Three points follow.

**Guides belong in the forward pass, not in a prepass.** On an Apple GPU a depth or normal prepass
duplicates geometry work that hidden surface removal makes unnecessary. Writing a packed
normal-and-roughness target from the forward pass costs four bytes per pixel of store bandwidth and
no geometry. The RHI allows three extra color attachments and the scene pass uses two (motion and
reactive), so exactly one slot is free; and per Apple's write-mask caveat, every opaque shader in
the pass, including the sky, must write it. A prepass becomes justified only when something needs
depth or normals before shading: same-frame occlusion culling, or AO applied inside the forward
shader.

**"Forward plus thin guides" does not close a door for M8.3 or M10, but the guides will not stay
thin.** The AO exit gate ("does not double-darken indirect energy") forces a decision that the
Indiana Jones renderer shows concretely: its forward pass writes normal, albedo and smoothness, and
GI, SSR and subsurface are applied in a deferred composite. Once albedo is in the guide set to
support an indirect composite, the forward path is a compact G-buffer in all but the location of
direct lighting. That is a fine place to end up; it should be chosen rather than drifted into, and
it exceeds the RHI's current attachment count.

**The reconstruction module is not optional work.** A reflection ray's hit arrives as an instance,
a primitive index and barycentrics. Shading it means loading three vertices, interpolating
attributes, choosing a mip level without screen-space derivatives, and reading whichever material's
textures the ray happened to hit. That is the visibility-buffer resolve with a different source of
barycentrics and gradients, and it has the same blocking requirement: material textures addressable
by index. Building that once, as a shared module with its own tests against the forward shader, is
work M10 needs even if the opaque path never changes (*inference*).

## 8. Options for Luminex

| | (a) Forward+ everywhere, guides as extra forward outputs | (b) Visibility buffer for cluster geometry only | (c) Visibility buffer for all opaque geometry | (d) Compact tile-local G-buffer |
|---|---|---|---|---|
| Shipped analog | Indiana Jones; Wicked Engine (with reconstructed guides) | Nanite; RE Engine; Decima (foliage only) | id Tech 8, with Forward+ still kept for transparents and some opaque | Apple's deferred-lighting sample; the pattern Apple recommends |
| New code | One packed guide attachment; all opaque shaders write it | ID-only cluster raster; reconstruction module; resolve; texture access by index | Same as (b) plus masked ID shaders, per-mesh triangle addressing and wider IDs | G-buffer layout and encode/decode; lighting pass; programmable blending in the RHI and shader toolchain |
| What it buys on an Apple GPU | Everything M8.3 and M10 launch need, at one extra store | Removes helper-lane cost exactly where triangles are small; a prerequisite for any later compute rasterizer, which has no fragment stage to shade in | The same on ordinary geometry, where triangles are mostly large; masked foliage relief | Lighting once per pixel in full quads; material fill still pays helper lanes (Hable: deferred material cost 4.35× at one pixel) |
| What it costs | Nothing structural | Two opaque paths that must agree; bindless-style texture access | Forward demoted to oracle and transparency path; every material feature needs analytic gradients; hardware MSAA closed | A fixed surface layout; guides must be stored, not memoryless, if later passes read them |
| Pipeline variants | Unchanged | Adds two to three; removes none | Adds the same; removes none while the oracle stays | Adds a G-buffer twin of each scene shader and a lighting shader |
| Main risk | Guide set grows into a G-buffer unplanned | Resolve slower than forward on this hardware for reasons no source has measured | Large rewrite justified by desktop numbers | Toolchain support for tile-memory reads |

**What to test first.** Option (a), because M8.3 needs its outputs whatever happens and nothing in
the evidence argues against it on this hardware; together with stage 0 and stage 1 of section 6,
which need no new path. Then option (b), inside the cluster-geometry work and after dense content
exists, with the tile-local resolve as the first variant. Options (c) and (d) should be reached only
through a measured result, not planned.

**What would change that.**

- Stage 0 shows fragment-shading cost per covered pixel flat down to one-pixel triangles: helper
  lanes are not a cost on this GPU, (b) loses its main justification, and the surface-path
  experiment shrinks to the masked control.
- Stage 0's helper-lane count on San Miguel at native resolution, not only in a synthetic lab, is
  half or more of all fragment lanes: ordinary content already pays, and (c) moves ahead of (b).
- Stage 1 shows the alpha-only control recovers most of San Miguel's masked cost: foliage is not an
  argument for a visibility buffer here.
- Explicit-gradient sampling or per-pixel vertex loads prove slow on the M3 Max: the resolve's fixed
  overhead (Hable measured 0.34 ms on an RTX 3070, via fetch summary) is larger here, and the
  crossover moves toward smaller triangles.
- A compute rasterizer enters scope: a visibility buffer becomes mandatory for whatever it draws.
- M10 lands first: reconstruction and indexed texture access already exist, and the marginal cost of
  (b) drops to the ID pass and the dispatch.

## Corrections to earlier research

- **The id Tech 8 slide URL was wrong.** The
  [studio disclosures notebook](../2026-09-14-roadmap-review/studio-and-engine-disclosures-2023-2026.md)
  and the [direction review](../2026-09-14-rendering-direction-review.md) cite a path under
  `/public/2025/slides/`. That path returns HTTP 404. The deck is under `/public/2025/talks/`; see
  Sources.
- **Wicked Engine is not a second shipped visibility-buffer shading design.** The direction review's
  trend table says the visibility buffer was "adopted by id Tech 8 and Wicked Engine" and that M9's
  experiment "has two shipped reference designs", and the
  [open-source notebook](../2026-09-14-roadmap-review/open-source-references-2024-2026.md) calls
  Wicked a "visibility-buffer renderer" with "on-demand shading". Wicked's opaque shading is tiled
  forward; its ID target feeds guide reconstruction. The shipped shading designs are id Tech 8,
  Nanite, RE Engine and Decima.
- **The presenter's name.** [validated-techniques.md](../validated-techniques.md) attributes the
  2024 talk to "Stephen Hable". The deck's title slide reads John Hable. The same notebook's summary
  of his conclusion is accurate.
- **Apple guidance was recorded as unverified and paraphrased loosely.** The
  [pipeline notebook](../2026-09-14-roadmap-review/pipeline-state-of-the-art-m7-m11.md) says
  "standard guidance favors Forward+/tile-based culling or a thin visibility buffer over a fat
  G-buffer" and marks it unverified. Apple's published guidance is different: single-pass deferred
  with programmable blending and memoryless attachments is called "a good default choice", a
  performance-only depth prepass is called redundant, and the visibility buffer is presented as the
  technique for hardware without tiling. Section 5.1 gives the sources.
- **Burns and Hunt is now confirmed.** The same notebook lists the paper's content as "unconfirmed
  live" after an empty fetch. The PDF was read for this notebook; its headline result is a bandwidth
  and sample-count result, with "little to no net benefit" at one sample per pixel.
- **"Bindless" applies to vertices only.** [production-engines.md](../production-engines.md) says
  the existing bindless vertex-pulling layout is unusually compatible with a visibility-buffer
  experiment. Vertex pulling is in place. Material textures are a per-draw set of five, which is the
  actual blocker for a single resolve.
- **Feature availability can be stated more firmly.** The
  [2026-08-09 synthesis](../2026-08-09-rendering-pipeline-synthesis.md) says barycentrics and
  primitive identity must be queried per family. The current feature tables list both for Apple7 and
  later, which covers every device Metal 4 runs on.

## Implications for Luminex

1. **The single forward pass is the correct baseline on this hardware and is not the "older of the
   two options".** For opaque geometry it already shades once per visible pixel with one geometry
   pass and no stored depth requirement. The desktop evidence that forward loses badly was measured
   on renderers that needed a depth prepass.
2. **The experiment in the M9 text should be re-scoped.** "Classification" has no content for a
   one-BRDF renderer. The measurable questions are helper-lane cost on Apple GPUs, masked overdraw,
   and reconstruction overhead. Two of the three can be answered before any new surface path exists.
3. **A visibility buffer is coupled to cluster geometry, not to M9's date.** It pays where triangles
   are a few pixels or smaller, at the quad cost stage 0 measures for this hardware, and it is the
   only way to shade what a compute rasterizer draws.
   Cluster LOD on ordinary indirect draws with relaxed error thresholds does not need it. If the
   cluster work targets pixel-scale triangles, the resolve should be a slice of that work with
   forward as the fallback.
4. **Indexed material textures are the common prerequisite.** A single resolve, ray-hit shading in
   M10 and any batching of materials across cluster draws all need them. It is RHI surface the
   project lacks today and it deserves its own accepted slice ahead of whichever consumer comes
   first.
5. **Attribute reconstruction should be a shared, separately tested module.** Barycentrics, depth,
   attribute interpolation and UV gradients from three clip-space vertices, with oracles against
   hardware barycentrics and derivatives. The visibility resolve and ray-hit shading both consume it.
6. **M8 does not have to wait.** M8.3's guides can be one extra attachment of the existing forward
   pass. The AO energy gate needs a decision about where indirect light is composited, and that
   decision determines whether the guide set stays at normal and roughness or grows to include
   albedo, which exceeds the current attachment limit.
7. **Publishing the stage 0 and stage 1 numbers would fill a real gap.** No public source reports
   quad utilization, masked overdraw or a forward-versus-visibility comparison on an Apple GPU.
8. **Pipeline variants will not shrink.** A visibility resolve unifies opaque, masked and motion
   shading in one function, but the forward shaders stay as the oracle and for transparency, so the
   total grows.

## Open questions and what could not be confirmed

- What helper lanes cost on Apple GPUs relative to covered pixels, whether the hardware omits them
  for fragment functions that take no derivatives, and what quad utilization is on real content.
  Apple's shading language documents the helper-thread and quad-group mechanism (section 5.2) but
  publishes no cost. Stage 0 measures it.
- Relative cost of explicit-gradient sampling and of per-pixel buffer loads on M-series GPUs.
  Aaltonen's figures are for phone-class GPUs.
- Whether a fragment function can read an integer attachment through programmable blending (the
  Apple notebook's probe compiled a Slang `SubpassInput` read but did not try an integer format),
  and whether the tile-local resolve of section 5.4 works as a whole. Tile shaders and imageblocks
  have no Slang stage.
- Whether Xcode's fragment-shader-invocation counter includes helper threads. The Metal API on the
  development machine exposes only the timestamp counter set (local probe), so the invocation and
  pixels-stored counters are capture-only evidence.
- How Unreal's Nanite compute shading obtains derivatives on Metal. The Epic macOS progress report
  returned HTTP 403; that Nanite runs on M2 and later through the Shader Model 6 path is *reported*
  from a search summary of that page, not read.
- The GDC 2024 "Nanite GPU-Driven Materials" session page returned HTTP 403. The description of
  compute shade binning here comes from source, not from that talk, and the engine version that
  introduced it was not confirmed.
- The Call of Duty engine's visibility-buffer use is known only from a one-paragraph abstract; the
  slides were not read.
- Dawn Engine's "Deferred+" is known only through citations by id and Epic.
- Numbers attributed to Hable's 2021 blog post, Engel's 2018 blog post, the Bevy post, the Arm
  article and the Castaño article were read through fetch summaries, twice each. The Engel, Bevy,
  Arm and Castaño figures were consistent between readings. Hable's blog totals differ from his 2024
  deck's recap of the same tests (section 5.3) while agreeing in order and in the 0.34 ms fixed
  cost; read the pages directly before a plan quotes any of them.
- The Mali quad-shading statements in Arm's counter guides were seen as search excerpts; the pages
  did not load. The Nanite article's statement that Mali shades fragments in quads was read through
  a fetch summary.
- San Miguel triangle and material counts were taken from the locally converted asset, not from the
  upstream archive's documentation. The pixels-per-triangle estimate in section 5.4 is arithmetic,
  not a measurement.

## Sources

Read directly (downloaded and read as text) unless noted.

Papers and talks:

- Burns and Hunt, "The Visibility Buffer: A Cache-Friendly Approach to Deferred Shading", JCGT 2(2),
  2013. https://jcgt.org/published/0002/02/04/paper.pdf
- Schied and Dachsbacher, "Deferred Attribute Interpolation for Memory-Efficient Deferred Shading",
  HPG 2015 (abstract and introduction). https://cg.ivd.kit.edu/publications/2015/dais/DAIS.pdf
- Karis, Stubbe and Wihlidal, "Nanite: A Deep Dive", SIGGRAPH 2021 Advances.
  https://advances.realtimerendering.com/s2021/Karis_Nanite_SIGGRAPH_Advances_2021_final.pdf
- McLaren, "Adventures with Deferred Texturing in Horizon Forbidden West", GDC 2022.
  https://www.guerrilla-games.com/read/adventures-with-deferred-texturing-in-horizon-forbidden-west
  (slides linked from that page)
- Hable, "Variable Rate Shading with Visibility Buffer Rendering", SIGGRAPH 2024 Advances.
  https://www.advances.realtimerendering.com/s2024/content/Hable/Advances_SIGGRAPH_2024_VisibilityVRS-SIGGRAPH_Advances_2024.pptx
- Hable, "Visibility Buffer Rendering with Material Graphs", 2021 (fetch summary).
  http://filmicworlds.com/blog/visibility-buffer-rendering-with-material-graphs/
- Lazarek and Hammer, "Visibility Buffer and Deferred Rendering in DOOM: The Dark Ages", GPC 2025.
  https://static.graphicsprogrammingconference.com/public/2025/talks/visibility-buffer-and-deferred-rendering-in-doom-the-dark-ages/Lazarek-Hammer-visibility-buffer-and-deferred-rendering-in-doom-the-dark-ages.pdf
- Fuller and Hammer, "Variable-Rate Compute Shaders in DOOM: The Dark Ages", GPC 2025.
  https://static.graphicsprogrammingconference.com/public/2025/talks/variable-rate-compute-shaders-in-doom-the-dark-ages/Fuller-Hammer-variable-rate-compute-shaders-in-doom-the-dark-ages.pdf
- Catana and Bastian, "Bringing Hitman to Your Pocket", GPC 2025.
  https://static.graphicsprogrammingconference.com/public/2025/talks/bringing-hitman-to-your-pocket/Catana-Bastian-bringing-hitman-to-your-pocket.pdf
- Mishima, "RE ENGINE Meshlet Rendering Pipeline", REAC 2025, slides and Q&A.
  https://enginearchitecture.org/downloads/REAC_2025_Capcom.pdf and
  https://enginearchitecture.org/downloads/REAC_2025_Capcom_QA.pdf
- de Vahl, "Pushing 60hz: Indiana Jones and The Great Circle", REAC 2025.
  https://enginearchitecture.org/downloads/REAC_2025_Indy.pdf
- Bukhalov and Orachev, "Warhammer 40000: Space Marine 2", REAC 2025.
  https://enginearchitecture.org/downloads/REAC_2025_Saber.pdf
- Geffroy, Gneiting and Wang, "Rendering the Hellscape of DOOM Eternal", SIGGRAPH 2020 Advances.
  https://advances.realtimerendering.com/s2020/RenderingDoomEternal.pdf
- Aaltonen, "HypeHype Mobile Rendering Architecture", SIGGRAPH 2023 Advances.
  https://advances.realtimerendering.com/s2023/AaltonenHypeHypeAdvances2023.pdf
- Drobot, "Geometry Rendering Pipeline Architecture", 2021 (abstract only).
  https://research.activision.com/publications/2021/09/geometry-rendering-pipeline-architecture
- SIGGRAPH Advances course programs. https://advances.realtimerendering.com/s2025/index.html and
  https://advances.realtimerendering.com/s2026/index.html
- REAC 2025 program. https://enginearchitecture.org/2025.htm

Apple:

- "Tailor your apps for Apple GPUs and tile-based deferred rendering".
  https://developer.apple.com/documentation/metal/tailor-your-apps-for-apple-gpus-and-tile-based-deferred-rendering
- "Rendering a scene with deferred lighting in Objective-C".
  https://developer.apple.com/documentation/metal/rendering-a-scene-with-deferred-lighting-in-objective-c
- "Harness Apple GPUs with Metal", WWDC20 (transcript). https://developer.apple.com/videos/play/wwdc2020/10602/
- "Optimize Metal apps and games with GPU counters", WWDC20 (transcript).
  https://developer.apple.com/videos/play/wwdc2020/10603/
- "Optimize Metal Performance for Apple silicon Macs", WWDC20 (transcript).
  https://developer.apple.com/videos/play/wwdc2020/10632/
- "Bring your Metal app to Apple silicon Macs", WWDC20 (transcript).
  https://developer.apple.com/videos/play/wwdc2020/10631/
- "Modern Rendering with Metal", WWDC19 (transcript). https://developer.apple.com/videos/play/wwdc2019/601/
- Metal Feature Set Tables, PDF dated 2026-06-02.
  https://developer.apple.com/metal/Metal-Feature-Set-Tables.pdf
- Metal Shading Language Specification, version 4.1, PDF dated 2026-06-04 (sections 6.10.3 and
  6.11.1). https://developer.apple.com/metal/Metal-Shading-Language-Specification.pdf

Engines and open source:

- Unreal Engine source, branches `ue5-main` and `5.8`, read 2026-10-01:
  `Engine/Shaders/Private/Nanite/NaniteShadeBinning.usf`,
  `Engine/Shaders/Private/Nanite/NaniteExportGBuffer.usf`,
  `Engine/Shaders/Private/Nanite/NaniteShadeCommon.ush`,
  `Engine/Shaders/Private/Nanite/NaniteAttributeDecode.ush`,
  `Engine/Source/Runtime/Renderer/Private/Nanite/NaniteShading.cpp`, and the commit history of the
  first and last of these.
- The Forge README, release notes 1.54 to 1.63. https://github.com/ConfettiFX/The-Forge
- Engel, "Triangle Visibility Buffer", 2018 (fetch summary).
  https://diaryofagraphicsprogrammer.blogspot.com/2018/03/triangle-visibility-buffer.html
- Turánszki, "Wicked Engine's graphics in 2024" (fetch summary).
  https://turanszkij.wordpress.com/2024/12/10/wicked-engines-graphics-in-2024/ and
  https://raw.githubusercontent.com/turanszkij/WickedEngine/master/features.txt
- JMS55, "Virtual Geometry in Bevy 0.14" (fetch summary).
  https://jms55.github.io/posts/2024-06-09-virtual-geometry-bevy-0-14/

Tile-based GPU vendors and measurements:

- Calvo Lista, "Mali and Unreal Engine's Nanite", Arm, 2026-03-18 (fetch summary).
  https://developer.arm.com/community/arm-community-blogs/b/mobile-graphics-and-gaming-blog/posts/mali-and-unreal-engine-s-nanite-enabling-the-future-of-mobile-graphics
- Imagination, "Hidden Surface Removal Efficiency" (fetch summary).
  https://docs.imgtec.com/starter-guides/powervr-architecture/html/topics/hidden-surface-removal-efficiency.html
- Castaño, "Encoding Render Targets for Free with Tile Shaders", 2026-09 (fetch summary).
  https://www.ludicon.com/castano/blog/2026/09/encoding-render-targets-for-free-with-tile-shaders/
- Arm Mali performance-counter guides (search excerpts only; pages did not load).
  https://developer.arm.com/documentation/102810/0110/Shader-core-data-path

Not retrieved (HTTP 403): Epic's macOS feature-parity progress report; the GDC 2024 session page for
"Nanite GPU-Driven Materials".
