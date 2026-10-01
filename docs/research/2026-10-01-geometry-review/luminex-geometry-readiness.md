# Luminex geometry readiness: what the existing code offers and lacks

**Status**: Frozen — non-normative  
**Research date:** 2026-10-01

Evidence notebook for the [geometry direction review](../2026-10-01-geometry-direction-review.md),
mapping what the repository offered and lacked for cluster geometry on the research date. It was
collected read-only from the tree that carried the UX4 editor and the proposed UX5 record, with the
`RojoRHI/` component at its pinned revision; nothing was built or run except one read-only script
that counted accessors in the fetched glTF files. Paths are relative to the repository root and
describe the tree on that date, so check a symbol still exists before relying on it. Items marked
**[UNVERIFIED]** were not confirmed against the source; recheck them before a plan depends on them.

Sources read: `AGENTS.md`, the architecture pages and frame walkthrough, ADRs 0009, 0021, 0023 and
0027, both roadmap files for the rendering parts, the visibility guide, the conventions, the
RojoRHI ADRs 0003 and 0005, and the source, shader, tool and build files named below.

Short version: the scene tables, the paced-slot pattern, the count/scan/emit compaction kernels, the
HZB, the oracles, the measurement harness and the bake-tool template are all reusable. The gaps are
that nothing below "one mesh range per instance" exists anywhere (CPU model, tables, visibility,
submission), that the RHI has no integer target, no mesh pipeline, no multi-draw and no way to bind
more than 16 textures, and that shading code lives inside four hand-mirrored entry files.

---

## 1. Geometry data today

### CPU model and vertex format

- `Source/Engine/Geometry/Mesh.h` defines the only runtime vertex, `engine::Vertex`: position
  `float3`, normal `float3`, tangent `float4` (xyz + handedness), UV `float2`. It is 48 bytes,
  pinned by a `static_assert`. `engine::MeshData` is `std::vector<Vertex>` plus
  `std::vector<uint32_t>` triangle-list indices. Nothing else: no vertex color, second UV set,
  skin weights, quantized or compressed attributes.
- `Source/Engine/Asset/Model/GeometryGenerator.h` holds the Asset-side twin `asset::VertexPNTU`
  (same 48 bytes) and `asset::GeoData`; `engine::fromGeo` copies one into the other.
- Index width is 32-bit only, end to end. `rojoRHI::CommandList::drawIndexed` documents uint32 as
  "the only index type this RHI models" and the Metal backend hard-codes `MTL::IndexTypeUInt32` and
  `MTL::PrimitiveTypeTriangle`.

### glTF import

- `asset::loadGltf` (`Source/Engine/Asset/Model/GltfLoader.cpp`, over the single cgltf
  implementation) turns every primitive of every mesh reachable from the active scene into one
  `GeoData`. `decodePrimitive` requires `POSITION` and `NORMAL`, reads `TEXCOORD_0` (zeros when
  absent) and `TANGENT`, widens any index type to uint32 through `cgltf_accessor_unpack_indices`,
  and synthesizes a trivial index list for non-indexed primitives. Missing tangents are generated
  by `generateTangents` (per-triangle UV derivatives accumulated per vertex, Gram-Schmidt).
- Rejected as `Unsupported`: non-triangle primitive modes, morph targets, skins, BLEND materials,
  sparse/compressed accessors (the unpack count check fails). There is no welding, deduplication,
  vertex-cache or overdraw reordering, and no quantization at import.
- Node transforms are flattened: one `GltfInstance` per (node, primitive) with a world matrix. A
  mesh referenced by several nodes is stored once and instanced (`primitiveFlatIndex`).
- `Tools/convert_obj_to_gltf.py` produces Sponza and San Miguel as **one node, one mesh, one
  primitive per material** with un-shared per-primitive vertex streams and no tangents. This is
  why those scenes have very few, very large draws (item 8).

### The scene geometry pool

- `engine::Scene::addMesh` (`Source/Engine/Scene/SceneRegistry.cpp`) appends a `MeshData`, assigns
  the next `MeshRow` range by running sums (`firstVertex`, `firstIndex`), and computes the
  mesh-local AABB from all vertices. It asserts that the scene is not finalized: "geometry is
  immutable after finalize". `MeshId` never has a generation other than 1; meshes are never removed.
- `Scene::finalize` (`Source/Engine/Scene/SceneFrame.cpp`) concatenates every mesh's vertices,
  rebases every index by its mesh's `firstVertex` (so `baseVertex` is always 0), and creates two
  immutable buffers, `lmx.scene.vertices` and `lmx.scene.indices`, with `storageRead` and
  `cpuReadback`. It then clears the CPU `MeshData` (`meshData.clear(); shrink_to_fit()`), so **no
  CPU copy of geometry survives finalize**. The sky sphere is one more mesh in the same pool.
- Shaders pull vertices: `StructuredBuffer<Vertex> gVertices` at b0 indexed by `SV_VertexID`; the
  index buffer is passed per draw by GPU address.

### Row layouts (`Source/Engine/Scene/SceneTables.h`, mirrored by `Shaders/Common/SceneTables.slang`)

| Row | Size | Fields |
|---|---|---|
| `InstanceRow` | 240 B | `model`, `previousModel`, `normalMatrix` (3 x mat4), `meshRow`, `materialRow`, `flags`, `emissiveScale`, world AABB min/max, two explicit padding words |
| `MeshRow` | 48 B | `firstIndex`, `indexCount`, `firstVertex`, `vertexCount`, local AABB min/max, two explicit padding words |
| `MaterialRow` | 112 B | UV transform, albedo, emissive, roughness, metallic, occlusion strength, alpha cutoff, flags (normal map, masked, double sided) |
| `LightRow` | 64 B | position/range, strength/spotScale, direction/spotOffset, bound sphere |
| `DrawUniforms` | 16 B | `firstEntry` plus padding |

- Offsets are pinned by `static_assert`s and by the GPU ABI oracle `Shaders/Tests/SceneTableAbi.slang`
  with `Tests/Engine/Scene/GpuSceneTableAbiTests.cpp`, which writes distinct values into every
  field including the padding words.
- Instance, material and light tables are `PacedTable<Row>` (`Source/Engine/Scene/PacedTable.h`):
  three `cpuWrite` buffers, a CPU shadow copy, per-slot dirty bits, doubling growth with retirement
  at `lastFrame + 3`. The mesh table is written once at finalize into all three slots.
- Instance flags: `kInstanceMotionInvalid`, `kInstanceBoundsUnreliable`, `kInstanceDisabled`.

### Where bounds come from

- Mesh-local AABB: `Scene::addMesh`. A mesh with non-finite vertices or no non-degenerate triangle
  keeps the inverted default box and is reported as unreliable (`Scene::meshBounds` returns none).
- Instance world AABB: `Scene::prepareFrame` transforms the eight local corners with
  `lmx::transformAabb` (`Source/Core/Math/Aabb.h`) every frame for every object, then `memcmp`s
  the row against the shadow copy. There are no bounding spheres per mesh or instance and no
  normal cones. `Source/Core/Math/Sphere.h` has a `Sphere` type and an AABB-to-sphere helper only.

### What a mesh row can and cannot describe

A `MeshRow` is exactly one contiguous index range and one contiguous vertex range with one box.
It cannot describe LOD levels, sub-ranges, clusters, a cluster hierarchy, error metrics, or a
material per range (material lives on the instance). The GPU mesh table is read by exactly one
consumer, the visibility emit kernels, which copy `indexCount`/`firstIndex` into indirect
arguments. `engine::DrawItem` carries a copied `MeshRow` plus five borrowed texture pointers per
instance. `MeshRow` and `InstanceRow` each have 8 bytes of explicit padding that could hold, for
example, a first-cluster index and count without changing row size, but the ABI oracle and its
test would have to change with them.

---

## 2. Visibility and submission today

### CPU path (default)

- `render::classifyInstance` / `classifyView` (`Source/Render/Passes/Visibility/Visibility.h/.cpp`)
  test each instance's world AABB against five guarded planes (`lmx::extractFrustum`,
  `planeRejects` in `Source/Core/Math/Frustum.h`; no far plane; 1e-3 world-unit guard) taken from
  the jittered raster matrix. Compiled with `-ffp-contract=off` so it matches the shader bit for
  bit. Precedence: authored-off, culling-off, view-unculled, non-finite transform, unreliable
  bounds, then the plane test.
- `Renderer::prepareVisibility` (`Source/Render/Renderer/RendererVisibility.cpp`) runs it twice,
  once for the scene view and once for the shadow view with `viewUnculled = true`.

### Draw rows, arguments and the three submission modes

- `render::buildDrawSubmission` (`Source/Render/Passes/Scene/DrawSubmission.cpp`) produces one
  `uint32` instance-row list (scene range, then shadow range) and one
  `rojoRHI::DrawIndexedIndirectArgs` (20 B) per `DrawRun`. `DrawSubmission` owns three paced
  buffer pairs, `lmx.draw.rows.N` and `lmx.draw.args.N`, sized
  `max(rows, instanceCapacity * 2)` and doubled on growth.
- Modes (`SubmissionMode`): **Direct** binds a 16-byte `DrawUniforms{firstEntry}` and calls
  `drawIndexed` per instance; **Indirect** (default) issues one `drawIndexedIndirect` per
  instance with `firstInstance` selecting the row; **Batched** stable-sorts by
  `(alphaMode, doubleSided, materialRow, meshRow)` and issues one instanced indirect draw per run.
- Shaders resolve `gVisibleRows[gDraw.firstEntry + SV_InstanceID]` at b4, then instance row at b5
  and material row at b6.
- **The CPU still issues one RHI command per run.** `encodeDrawRuns`
  (`Source/Render/Common/DrawEncoding.h`) loops over runs, selects the pipeline and binds five
  material textures before every command. There is no multi-draw call in the RHI (item 4).

### GPU classification (`GpuVisibility`, opt-in with `--classify gpu`)

- `Source/Render/Passes/Visibility/GpuVisibility.cpp` declares `lmx.pass.visibility.reset`
  (zero-fill counters), `.classify`, `.scan` (batched only) and `.emit`. Kernels are
  `Shaders/Passes/Visibility/VisibilityClassify.slang`, `VisibilityClassifyOcclusion.slang`,
  `VisibilityScan.slang`, `VisibilityEmit.slang` (dense) and `VisibilityEmitSparse.slang`, with
  the shared ABI in `Visibility.slang` and a 256-lane inclusive prefix scan in
  `VisibilityPrefix.slang`.
- It is "fixed slot": the CPU builds `CandidateRecord{instanceRow, run}`, `RunRecord` and
  `ChunkRecord` (256 candidates per threadgroup) tables every frame in
  `buildVisibilityTables` (`VisibilityTables.cpp`) and uploads them when their bytes changed.
  Sparse layout writes row and argument at the candidate's own index (a rejected candidate gets an
  all-zero argument). Dense layout packs retained rows at the start of each run's reserved range
  using per-chunk counts, a per-run scan with a carry, and prefix offsets; the run's argument gets
  the retained count. Atomics (`InterlockedAdd`) are used only for counters, never to pick an
  output position, which is what makes the output deterministic and comparable to the CPU oracle.
- State word per candidate: bits 0-1 visible/rejected/bypassed, bits 2-7 reason, bits 8-11
  occlusion outcome. Counters are 21 words per view (`kVisibilityCounterWords`).
- Capacities are explicit in `VisibilityParams` (`rowCapacity`, `argumentCapacity`,
  `stateCapacity`); writes past capacity are dropped and counted (`overflowedRows`,
  `overflowedCommands`). Production allocations cover all candidates, so overflow is reachable
  only through `GpuVisibility::setCapacityOverride` in fixtures. Overflow fails a capture sequence
  and invalidates a scored measurement.
- Readback is frame-keyed (`GpuVisibilityReadback.cpp`, `retireThrough`, `takeRetired`); with
  `--classify-check` the retired states, rows, arguments and counters are compared against the
  declaration-time CPU oracle (`VisibilityStatus::stateMismatches` and friends).

### How scene and shadow views share it

Both views live in the same candidate, run, chunk, row and argument buffers at disjoint absolute
ranges. `VisibilityTables::views` is a fixed `std::array<VisibilityViewParams, 2>`; the views
buffer is a fixed 224 bytes; the kernels decide the view with
`candidate >= gViews[1].firstCandidate` and compute totals from `gViews[1]`. The shadow view copies
the camera planes but sets the "unculled" flag, so shadow casters are never culled, on either path.
There is one shadow map, fitted to the whole scene's bounding sphere (`fitShadowOrtho`).

### HZB and occlusion (opt-in with `--occlusion on`, GPU classification only)

- `HzbStage` (`Source/Render/Passes/Occlusion/HzbStage.h/.cpp`) owns two alternating `R32Float`
  pyramids whose level 0 is half the output resolution; `Shaders/Passes/Occlusion/HzbReduce.slang`
  takes the 2x2 minimum (farthest in reversed Z), one compute pass per mip, then
  `HzbPublish.slang` reads every mip. Level count stops when the top level is at most 16 texels.
- `Shaders/Passes/Visibility/Occlusion.slang` (`projectOcclusionBounds`, `testOcclusionBounds`,
  CPU mirror in `Source/Render/Passes/Occlusion/Occlusion.cpp`) projects the eight AABB corners
  with the **previous** frame's jittered matrix, pads the rectangle by one texel, picks the first
  mip where it covers at most 2x2 texels, and rejects when the box's nearest depth plus a guard is
  behind the farthest stored depth. Near-plane crossings, off-source rectangles and rectangles too
  large for the top mip are retained and counted separately.
- It is **single phase**: there is no second pass against the current frame's depth, so newly
  disoccluded instances can be missing for one frame under camera motion (M7.4 accepts this).
- `occlusionHistoryReason` (`OcclusionHistory.h`) invalidates **globally** on a scene coverage
  epoch change, camera cut, more than 1 m or 10 degrees of camera motion per frame, output resize,
  a non-adjacent source frame, or wireframe. Any animated object invalidates occlusion for all.
- The oracle: `OcclusionReference` (`OcclusionReference.cpp`,
  `Shaders/Passes/Occlusion/OcclusionReference.slang`) redraws every candidate with direct draws
  into a private `RGBA8Unorm` target, writing `instanceRow + 1` as four bytes, copies it to a
  paced readback buffer, and `OcclusionCheckHistory` joins visible pixels per instance identity
  into missing-frame streaks (`OcclusionCheckResult::passed`: zero in a strict frame, streak below
  two under motion).

### What is per instance only, and what must generalize

| Mechanism | Per instance today | What per-cluster work needs |
|---|---|---|
| CPU oracle | `classifyInstance` over `InstanceRow` bounds | The same plane test over cluster bounds in instance space; a CPU mirror of LOD selection if "GPU equals CPU oracle" stays the gate |
| Candidate tables | CPU-built, one record per instance per view, uploaded per frame | Candidates must be GPU-generated (instance x cluster); a CPU-built list of every cluster instance does not scale and re-creates the fixed CPU encode cost |
| Capacities | Sized from `instanceCapacity * 2`, cannot overflow in production | A real budget (visible clusters per view), so the drop-and-count overflow path becomes a production path with a visible diagnostic |
| Counters | 21 words x 2 views, instance reasons | New reason codes (LOD-rejected, cone-culled, cluster-occluded), cluster and triangle totals, per-view |
| Compaction | 256-lane prefix scan per run, deterministic order | Reusable as is for one more level (clusters within an instance run); a hierarchy traversal needs a work queue the current kernels do not have |
| Views | Hard-coded 2, shadow unculled, shared planes | N views with their own planes, matrix, HZB and output ranges (this is also what M8.1 per-cascade culling needs) |
| Occlusion | One phase, previous frame, global invalidation, AABB of instance | Two-phase (retest rejected set against current depth) if per-cluster occlusion is to be safe under motion; LOD changes alter depth, so HZB conservativeness under LOD needs a stated rule |
| Oracle | 32-bit instance ID in `RGBA8Unorm`, per-instance pixel counts | (instance, cluster) identity; fits in two `RGBA8Unorm` targets today, or one integer target after an RHI addition |
| Submission | One CPU-issued command per instance or run; five textures bound per run | Either clusters-as-instances inside existing per-material runs, a GPU-compacted index buffer per run, or a new RHI multi-draw/ICB path |

Recorded cost context (M7.3 validation, M3 Max, 1280x720, Native TAA): GPU classification added
cost in 11 of 12 cells and removed no CPU encoding; CPU encode at 65,536 instances was 43 ms
indirect and 13 ms batched. The per-instance CPU loops (`Scene::prepareFrame`,
`Scene::fillDrawItems`, `buildVisibilityTables`, `encodeDrawRuns`) are the scaling limit in
instance count; cluster work does not change them.

---

## 3. Raster and shading today

### Stages and pipelines

- `ShadowStage` (`Source/Render/Passes/Shadow/ShadowStage.cpp`): one depth-only pass
  `lmx.pass.shadow` into a fixed 2048x2048 `D32Float`, three pipelines (opaque, masked, masked
  double-sided), its own run loop.
- `SceneStage` (`Source/Render/Passes/Scene/SceneStage*.cpp`): one pass `lmx.pass.scene` writing
  scene color (`RGBA16Float`), depth (`D32Float`) and, with temporal on, motion (`RG16Float`) and
  reactive (`R8Unorm`). Pipelines created up front in `SceneStagePipelines.cpp`: 8 opaque
  (manual/auto exposure x motion x wireframe), 16 masked (double-sided x auto x motion x
  wireframe), 4 sky. With the 3 shadow pipelines that is **31 graphics pipelines** for geometry.
- Stage shape is fixed by `docs/conventions/engineering.md` ("Render stages"): static `create`,
  one `declare(graph, commands, view, inputs)`, labels `lmx.render.<name>Pipeline` and
  `lmx.pass.<family>.<step>`, three paced slots for frame data.

### Mirrored shader files

- `Shaders/Passes/Scene/ScenePass.slang`, `ScenePassAuto.slang`, `ScenePassMask.slang`,
  `ScenePassAutoMask.slang` (326 to 351 lines each) differ only in exposure source and in alpha
  coverage with two-sided normals. `Sky.slang`/`SkyAuto.slang` and
  `ShadowPass.slang`/`ShadowPassMask.slang` are mirrored the same way.
- The surface shading function (`shadeSurfaceDetailed`, `applyNormalMap`, `shadeVertex`,
  `reactiveFromEmissive`) is **inside each entry file**, not in a module. `Shaders/Common/` holds
  only `Lighting` (BRDF, directional light, IBL), `LocalLights` (`AccumulateLocalLights`),
  `Shadow` (`CalcShadowFactor`), `Motion`, `AlphaMask`, `Encode`, `Tonemap`, `SceneTables`.
- ADR 0027 closed deduplication as DEFER; the twin rule stays
  (`docs/conventions/shader-style.md`, "Mirrored exposure variants"). Its consequence, stated in
  the ADR: "Scene-shading edits in M8 and M9 are mirrored across four files by hand."

### How per-draw data reaches shaders

- Buffers in the scene pass: b0 vertices, b1 `DrawUniforms`, b2 `PassUniforms` (400 B), b3
  exposure (auto only), b4 visible rows, b5 instances, b6 materials, b7 reserved for meshes, b8
  lights, b9 cluster grid, b10 light indices, b11 `LocalLightParams` (136 B). **Twelve of sixteen
  buffer slots are used; four are free.**
- Textures: t0 base color, t1 normal, t4 metallic-roughness, t5 occlusion, t6 emissive are bound
  **per run** from `DrawItem` pointers; t3 shadow map, t7 to t9 IBL are per pass; t2 sky. There is
  no texture array and no bindless table. One argument table per frame slot is shared by all
  passes and its texture slots are cleared at each pass start.
- Shading relies on hardware derivatives: every material texture uses implicit-LOD `Sample`, and
  `LocalLights.slang` calls `ddx`/`ddy` on the normal for the punctual specular footprint filter.

### Motion vectors

`vertexMainMotion` computes unjittered current and previous clip positions from
`InstanceRow::model` and `previousModel`; `fragmentMainMotion` writes
`uvCurrent - uvPrevious` (`Shaders/Common/Motion.slang`) or the `+inf` sentinel for
`kInstanceMotionInvalid`. Motion is rigid per instance only.

### What a new geometry path must duplicate or can share

- **Ordinary indexed cluster draws** (a cluster is an index sub-range in the existing pool,
  drawn with the instance's row through `firstInstance`): the existing vertex and fragment entries
  work unchanged, because they only need `SV_VertexID` into the pool and an instance row. This is
  the cheapest path to a correct image. Its cost is one indirect command per visible cluster,
  CPU-issued, which is the M7.3 fixed-encode problem at a much larger count.
- **Clusters as instances** (one instanced draw per material run, the vertex stage reading a
  visible-cluster list and a cluster table): needs new vertex entry points and two or three new
  buffer bindings (of the four free), in all four scene files and both shadow files, plus a
  doubled pipeline set (up to +27). Fragment code is shared.
- **Visibility buffer plus resolve**: needs an ID target (item 4), a resolve pass that re-derives
  position, normal, tangent, UV and their screen-space derivatives analytically, replaces every
  implicit-LOD `Sample` and the `ddx`/`ddy` in `LocalLights.slang`, writes motion and reactive,
  and can reach every material's five textures at once, which the 16-slot per-run binding model
  cannot do. The resolve would be a fifth copy of the surface shading unless a shared module is
  introduced, which for the existing four ADR 0027 gates behind its reopening conditions (parent
  Native TAA repeatability over eight rounds, then a new R milestone of its own).
- **Any edit to `ScenePass*.slang`** changes the shader hashes in measurement provenance and
  obliges the exact-image matrix; R4.1 showed Native TAA captures of the unchanged parent are not
  always repeatable. A new path in new files, off by default, avoids touching the reference.
- Graph goldens (`Tests/Golden/`) pin the default frame's imports and passes. The M7.5 precedent
  is to add an import or pass only on frames that use it (zero-light frames declare nothing new).

---

## 4. RHI surface (`RojoRHI/Include/rojoRHI/`)

### What exists

| Area | Symbols | Notes |
|---|---|---|
| Indirect draws | `CommandList::drawIndirect`, `drawIndexedIndirect`; `DrawIndirectArgs` (16 B), `DrawIndexedIndirectArgs` (20 B: indexCount, instanceCount, firstIndex, baseVertex, firstInstance) in `Indirect.h` | **One argument record per call; no count parameter, no multi-draw, no indirect command buffer.** Offsets must be 4-byte aligned |
| Indirect dispatch | `CommandList::dispatchIndirect`, `DispatchIndirectArgs` (12 B) | Threads per group come from `ComputePipelineDesc::threadsPerThreadgroup` |
| Compute and storage | `bindStorageBuffer(slot, buffer, StorageAccess)`, `bindStorageTexture(slot, texture, view, access)`; compute-pass only | Raster passes read buffers through `bindBuffer` only; no storage writes from fragment stages are modeled |
| Binding model | `kMaxBufferBindings = 16`, `kMaxTextureBindings = 16`, `kMaxSamplerBindings = 8`; `bindFrameData` returns a `GpuAddress` valid for the frame | No texture arrays, no unbounded tables. `GpuAddress` deliberately has no arithmetic |
| Formats (`Format.h`) | `BGRA8Unorm`, `RGBA8Unorm`, `RGBA8Unorm_sRGB`, `RGBA16Float`, `RG16Float`, `R8Unorm`, `BC1Unorm`, `BC1Unorm_sRGB`, `D32Float`, `R16Float`, `R32Float` | See below |
| Texture kinds | `TextureKind::Tex2D`, `Cube` | No 2D arrays, no 3D (copy regions reserve `z`/`depth` for later) |
| Render pass | `RenderPassDesc`: one color target plus `kMaxExtraColorTargets = 3`, one depth target, origin-anchored `renderAreaWidth/Height` | No stencil, no MSAA (`setRasterSampleCount(1)`), no layered targets, no viewport offset |
| Pipelines | `GraphicsPipelineDesc`: `vertexEntry`, `fragmentEntry`, formats, depth test/write/compare, `FillMode`, `CullMode`, `DepthBias` | **No object/mesh stage fields, no blend state.** Triangles only |
| Capabilities | `DeviceCapabilities { TemporalScalerSupport temporalScaler; }` | Nothing else is reported. The device is rejected unless `MTLGPUFamilyMetal4` |
| Heaps | `Device::createHeap`, `createPlacedTexture`, `createPlacedBuffer`, `textureSizeAlign`, `bufferSizeAlign` | Untracked placement; hazards are the caller's barriers. Used by `TransientPool` |
| Buffer writes | `BufferDesc::cpuWrite` + `Buffer::write(offset, data, size)` | Caller proves all GPU use of the range retired (paced slot or `waitIdle`); placed private buffers reject `cpuWrite`. Immutable data uses `createBuffer(desc, initialData)` |
| Barriers | `textureBarrier`, `bufferBarrier` with `TextureUse`/`BufferUse` (including `IndirectArgument`), `BarrierOptions::ResourceAlias` | Derived by the render graph |
| Timing | `Device::passTimings()`, `passTimingsFrame()` | Every pass kind is timed with no caller work |

Format capabilities (`RojoRHI/Source/Validate.cpp`): color-renderable are `BGRA8Unorm`,
`RGBA8Unorm`, `RGBA8Unorm_sRGB`, `RGBA16Float`, `RG16Float`, `R8Unorm`; storage formats are
`RGBA8Unorm`, `RGBA16Float`, `RG16Float`, `R8Unorm`, `R16Float`, `R32Float`; depth is `D32Float`
only. **There is no unsigned-integer format of any width.** `R32Float` is sampled, storage and
CPU-readable but not a color attachment. The only existing 32-bit ID target is the
`RGBA8Unorm` byte packing in `OcclusionReference.slang`.

Atomics are not an RHI concept. The shader tree uses 32-bit `InterlockedAdd` on
`RWStructuredBuffer<uint>` (visibility counters, exposure histogram). No shader uses a 64-bit
integer type, a texture atomic, `SV_PrimitiveID` or barycentric inputs.

A hook exists in the dependency: the pinned metal-cpp (`ThirdParty/metal-cpp`, macOS 26.4 release)
already ships `MTL4MeshRenderPipeline.hpp` and `MTL4TileRenderPipeline.hpp`, so a mesh pipeline
needs no re-pin, only backend and interface work.

### Checkpoint A

Sixteen RHI cases are frozen by name in `RojoRHI/Tests/checkpoint-a.inventory` (RojoRHI ADR 0005,
restating Luminex ADR 0009) across eight areas: upload and layout, resource views, sRGB,
reversed-Z, storage hazards, load/store, indirect arguments, frame-slot retirement. Three more
graph/transient cases stay in Luminex (`Tests/checkpoint-a.inventory` is the union;
`Tools/check_checkpoint_a.py` checks the split). Rules: every listed case must keep passing
unchanged; adding the tag to a new case needs no ADR; removing or retagging needs a superseding
ADR. A cluster-path RHI addition does not touch any listed case as long as existing argument
layouts (`DrawIndexedIndirectArgs` in particular) and barrier semantics stay as they are.

### How an RHI change lands

- A Luminex commit never edits `RojoRHI/`. The change is a `rojo-rhi` pull request with its own
  smoke shader under `RojoRHI/Shaders/Tests/` and its own cases in `RojoRHITests` ("a new
  capability lands with the case that proves it", `RojoRHI/docs/conventions/testing.md`), then a
  Luminex pin bump.
  `Tools/check_submodule_pin.py` fails policy unless the pinned commit is reachable from
  `rojo-rhi`'s `origin/main`, so the RHI change must be **merged there first**.
- RojoRHI ADR 0003: "A capability is added when a consumer needs it, not because a backend offers
  it." Each addition needs a named Luminex consumer.
- Hosted CI has no Metal 4 GPU; GPU cases run on the development machine before merge.

### What a cluster path would need added

| Need | For | Size |
|---|---|---|
| `R32Uint` (render target, shader load, storage, CPU readback), possibly `RG32Uint` | Visibility buffer, cluster-ID oracle and debug views | Small: enum, three predicates in `Validate.cpp`, `toMTL`, `bytesPerPixel`, one conformance case. Integer clear values need a decision (`clearColor` is `float[4]`) |
| Multi-draw: an indirect draw with a GPU-read count, or an indirect command buffer object | Per-cluster indexed draws without one CPU command per cluster | Large. The M7.3 ICB spike (`docs/research/2026-09-15-m7.3-icb-runtime-spike.md`) passed execution and 900-frame reliability and failed its labeled-capture gate; no adapter was authorized |
| Mesh pipeline: object/mesh entries in a pipeline descriptor, a `drawMeshThreadgroups`-class command (direct and indirect), a capability flag | Optional mesh-shader execution | Medium to large, and needs the first field in `DeviceCapabilities` beyond the temporal scaler plus a tested fallback |
| Texture access beyond 16 per-run slots: a texture array binding, a larger argument table, or resource IDs in a buffer | Any resolve pass that shades many materials in one draw or dispatch | Medium to large; it changes the binding convention recorded in `docs/conventions/shader-style.md` |
| Stencil, or a viewport offset | Material classification by stencil; rendering into atlas sub-rectangles | Small each, but neither has a consumer in the accepted M9 text |
| Safe-math selection by something other than a hard-coded name prefix | New precise-math kernels | Small; see item 5 |
| Nothing | Primitive ID and barycentrics are shader-language inputs; GPU-written index buffers already work (`drawIndexedIndirect` takes any `Buffer`, and `BufferUse::ShaderRead` covers indices) | - |

Paths that need **no** RHI change: cluster frustum/cone/HZB culling in compute; clusters drawn as
instances of a fixed-size draw (non-indexed `drawIndirect` with a GPU-written `instanceCount`,
vertex stage pulling from cluster tables); GPU-compacted index buffers drawn by one
`drawIndexedIndirect` per material run; a two-target `RGBA8Unorm` ID oracle.

---

## 5. Shader toolchain

- `xmake/shaders.lua` defines rule `slang2metallib`. Per `.slang` file it runs
  `ThirdParty/slang/bin/slangc <file> -I Shaders/Common [-I Shaders/Passes/<each family> for
  sources under Shaders/Tests/] -target metal -o <targetdir>/Shaders/<basename>.metal`, then, when
  the offline Metal toolchain is present,
  `xcrun -sdk macosx metal -std=metal4.0 -frecord-sources -gline-tables-only -o <basename>.metallib`.
  No `-profile`, `-capability`, `-stage` or `-entry` flags are passed; entry points are found by
  `[shader("...")]` attributes. Without the toolchain the backend compiles the `.metal` at load.
- Slang is pinned at `v2026.14.1` with a binary SHA-256 in `xmake/setup.lua`.
- Precise math is selected **by output basename prefix**: `Visibility*`, `Occlusion*` (except
  `OcclusionReference`), `Hzb*`, `LightCluster*` get `-fp-mode precise` and
  `-fno-fast-math -ffp-contract=off`. The **same prefix list is duplicated in the RHI backend**
  (`RojoRHI/Backends/Metal4/Source/Metal4DevicePipeline.cpp`, the runtime-MSL fallback, which sets
  `MathModeSafe`). A cluster-culling kernel that must match a CPU mirror bit for bit either takes
  one of those prefixes or needs a Luminex build-rule edit **and** a `rojo-rhi` change.
- Every `.slang` under the include directories is a dependency of every shader of the target, so
  any module edit rebuilds all shaders for App, Tests and FrameDataBench.
- `Tools/check_shader_imports.py` (run by `xmake policy`) enforces: every source sits in
  `Shaders/Common/`, `Shaders/Tests/` or `Shaders/Passes/<family>/`; `Common` imports only
  `Common`; a family-local module is imported only from its own folder (only `Tests/` may cross);
  no import reaches a file with an entry point; named imports only, no `#include`; output
  basenames are unique across the whole tree, case-insensitively.
- A new family follows the existing ten (`Bloom`, `Display`, `Exposure`, `LocalLights`,
  `Occlusion`, `Scene`, `SelectionOutline`, `Shadow`, `Temporal`, `Visibility`): a folder
  `Shaders/Passes/<Family>/` with entry files and local modules, a matching
  `Source/Render/Passes/<Family>/` and `Tests/Render/Passes/<Family>/`, oracles in
  `Shaders/Tests/`. A module needed by two families (for example cluster decode used by both a
  cull family and the scene family) must live in `Shaders/Common/`.
- Explicit flat binding indices map one to one to argument-table slots; the convention says any
  replacement binding model must update `docs/conventions/shader-style.md`.

---

## 6. Offline tooling precedent

`Tools/TextureBake` is the template, in four layers:

1. **Library in Asset.** `Source/Engine/Asset/Texture/TextureBake.h/.cpp`: `bakeMips`,
   `writeDds`, `writeManifest`. Deterministic (fixed scan order, no parallel reduction), unit
   tested in `Tests/Engine/Asset/`. Asset depends on Core only and may include just
   `rojoRHI/Format.h` and `rojoRHI/TextureDesc.h`.
2. **Thin CLI.** `Tools/TextureBake/main.cpp` (about 100 lines, argv and file I/O only) with
   `Tools/TextureBake/xmake.lua` (`add_deps("Core", "Asset")`). It is its own unit,
   `texture-bake`, in the module contract (`docs/conventions/modules.md`,
   `Tools/module_contract.json`), with a link check that it pulls in no framework.
3. **Driver.** `Tools/bake_gltf_textures.py` parses the glTF/GLB JSON itself, selects images by
   role, calls the binary, and skips an image whose manifest (`<out>.dds.json`: `source`,
   `sourceSha256`, `filter`, `toolVersion`) is current. Tested by
   `Tools/tests/test_bake_gltf_textures.py`.
4. **Setup and load.** `xmake setup` (`xmake/setup.lua`) builds the tool
   (`xmake build -P . -y TextureBake`) and runs the driver for Sponza, Damaged Helmet, Milk Truck
   and, when present, San Miguel. Output goes to `<gltf dir>/Baked/image<N>.dds`.
   `appendGltfScene` (`Source/Engine/Scene/GltfScene.cpp`, `bakedDdsPath`) prefers the baked file
   and otherwise runs the same filter in process with one warning.

Pinning, precisely: **inputs** are pinned in `xmake/setup.lua` (archive SHA-256, converted-tree
digest via `Tools/tree_digest.py`, tool binary hashes). **Baked outputs are not hash-pinned**;
they are keyed by their manifests, and `tree_digest.py` explicitly skips any `Baked` directory so
the pinned tree digest stays stable across re-bakes.

Fetched assets live under gitignored `Assets/Fetched/<Name>/` with their license and provenance
files (`LICENSE.md`/`COPYRIGHT.txt`, `metadata.json`, `ARCHIVE_INFO.js`, `PROVENANCE.json`);
`THIRD_PARTY_NOTICES.md` and `Assets/README.md` carry the public record. Scene documents
(`Assets/Scenes/*.scene.gltf`) reference assets by Assets-relative URI plus the file's SHA-256,
checked at preflight (`Source/Engine/Scene/SceneDocumentPrepare.cpp`).

What a geometry bake would differ in:

- It needs a third-party clusterization library in the `asset` unit, whose allowed third-party
  set is `glm, cgltf, stb`. That is a module-contract edit and either an xrepo requirement (hard
  rule: "xrepo deps only as needed") or a pinned `ThirdParty/` fetch in `xmake/setup.lua`.
- The texture fallback is "do the same bake in process". For San Miguel (5.6 M triangles) an
  in-process cluster and simplification build at load is not a comparable fallback; the natural
  one is "no bake, draw the plain mesh".
- Bake time enters `xmake setup`, which CI runs on hosted runners.
- M9's deferral list names "general asset tooling"; the texture tool's shape (one library file,
  one CLI, one driver) is the scope precedent.

---

## 7. Labs, measurement and diagnostics

### Generators

- Catalog: `Source/Scenes/SceneLibrary.cpp` hard-codes six ids (`kIds`, `kNames`) and roles; each
  is a checked-in document under `Assets/Scenes/`.
- A lab is a document node carrying `extensions.LMX_scene.generator { name, params }`.
  `scenes::sceneGenerators` (`Source/Scenes/Generators.cpp`) returns a
  `std::map<std::string, engine::SceneGenerator>`; `validateSceneGenerators` whitelists names and
  parameter names and ranges (`instances` 1..1,048,576, `occluders` 0..1024, `lights`, `pile`,
  `axisStation`). `scenes::GeneratorOverrides` carries the CLI overrides (`--lab-instances`,
  `--lab-occluders`, `--lab-lights`, `--lab-light-pile`), threaded through `AppOptions` and
  `SceneLibrary`'s constructor. Engine calls generators through an injected lookup and names no
  Scenes symbol.
- `appendVisibilityLab` (`Source/Scenes/VisibilityLab.cpp`): two meshes (unit cube, 12 triangles;
  a once-subdivided icosphere, 80 triangles, unindexed), four materials (two masked, one double
  sided), five frustum-boundary probes, then a seeded jittered cubic grid alternating the two
  meshes; optional occluder slabs. `appendLightLab` (`LightLab.cpp`): a floor plane, eight pillar
  cubes and eight spheres, then N lights with closed-form orbits.
- Generators call `Scene::addMesh` with `MeshData` before finalize, so a "GeometryLab" can
  synthesize dense meshes procedurally with no asset dependency. Adding one touches: a new
  `append...` function and `CatalogScenes.h`, the whitelist and map in `Generators.cpp`,
  `GeneratorOverrides`, `kIds`/`kNames`, a document in `Assets/Scenes/`, `AppOptions` parsing and
  conflicts, `labDescription` (`DiagnosticLegend.h`), `MeasurementPlan`, a
  `Tests/Scenes/Scene<Lab>Tests.cpp`, and `Tools/Scenes/validate_documents.py` coverage.
- The roadmap's M9 text says "Extend MipLab and VisibilityLab". **No MipLab exists** (the name
  occurs only as a proposed lab in the frozen 2026-08-09 rendering-pipeline synthesis); the only
  mip fixture is MaterialLab's "mip probe" quad.

### Measurement

- `app::MeasurementRun` (`Source/App/Model/Performance/MeasurementRun.h/.cpp`,
  `MeasurementReport.cpp`) owns a deterministic warmup/measure plan, records a
  `MeasurementCpuSample` per frame, and joins retired GPU data by exact frame id through three
  entry points: `retire` (pass timings), `retireVisibility`, `retireLighting`. It refuses a scored
  run under validation or capture instrumentation.
- Report schema is **5**: plan, provenance (device, OS, build mode, executable hash, every shader
  hash, instrumentation environment), starting population, per-sample CPU times
  (`classifyMs`, `prepareMs`, `encodeMs`, `slotWaitMs`), counts, bytes, and prefix sums
  `gpuSumMs`, `visibilityGpuMs`, `hzbGpuMs`, `lightingGpuMs`, `sceneGpuMs`, `shadowGpuMs`, plus
  every pass label and time. Pacing is "serialized-retirement": isolated frame cost, not
  throughput.
- A cluster path adds a fourth join (`retireClusters`-style), a prefix sum for its family, counts
  and bytes, and therefore schema 6. `Tools/Bench/visibility_paired.py` accepts schema versions
  2 to 5 explicitly and would need the new one.
- Paired drivers: `Tools/Bench/visibility_paired.py` (`WORKLOADS`, `CONTROLS`, `METRICS`
  dictionaries; 12 AB/BA fresh-process pairs, W32/N256, 1280x720, Native TAA, 10,000 bootstrap
  resamples, seed `0x4C4D5836`; a `--parent` binary for same-plan controls),
  `Tools/Bench/lighting_paired.py` (`--control local|zero`), `Tools/Bench/frame_data_paired.py`.
  All keep every attempt and select no default. New workloads and controls are dictionary entries.
- Image gates: `Tools/Screenshots/parity.py`, `compare.py`, `parity_rounds.py`,
  `repeatability.py` over `Tools/Screenshots/reference.json` (15 cases: Sponza, MaterialLab,
  TemporalLab x Off / TAA 1.0 / TAA 0.5 / MetalFX 1.0 / MetalFX 0.5, 1280x720, 32 frames, BMP).

### Debug views and legends

- `app::DebugView` (`Source/App/Model/Rendering/Settings/DebugView.h/.cpp`) is one mutually
  exclusive selector over three topics, `Temporal`, `Lighting`, `Occlusion`, stored as three
  fields of `EditorRenderSettings` and forwarded into `SceneView`
  (`temporal.debugView`, `lightDebugView`, `hzbDebugLevel`). `debugViewEntries` supplies labels
  and disabled reasons, `reconcileDebugView` returns to Final with a notice.
- Each view is a post-display raster pass into the display target
  (`TemporalDebugView.slang`, `LightDebugView.slang`, `HzbDebugView.slang`); it never writes scene
  color or history. `Renderer::declarePasses` asserts exclusivity.
- `app::diagnosticLegend` (`DiagnosticLegend.h/.cpp`) supplies the viewport chip text;
  `labDescription` the lab blurb.
- Performance groups passes by the label segment after `lmx.pass.` (`app::passStage`), so a new
  family `lmx.pass.cluster.*` appears as a stage row with no further work. Render Graph and
  graph dumps pick up new passes automatically.
- Adding cluster diagnostics: a fourth `DebugViewTopic`, a settings field and `SceneView` field,
  labels and reasons, a legend, a CLI flag with conflict rules in `AppOptions.cpp`, a Rendering
  topic (`Source/App/Panels/Rendering/RenderingTopics.cpp`) backed by a 250 ms display model like
  `VisibilityDisplay`/`LightingDisplay`, and a debug stage. A per-pixel cluster or LOD view in a
  forward path has no ID to read; it needs either the fragment stage to output an ID or a
  separate ID pass in the style of `OcclusionReference`.

---

## 8. Scene statistics

| Scene | Triangles | Meshes (primitives) | Instances | Materials | Source of the numbers |
|---|---:|---:|---:|---:|---|
| Sponza | 262,267 | 25 | 25 | 25 (0 masked) | Triangles: `docs/milestones/m7/m7.5-followup.md` ("262267 asset triangles"). 25 primitives under one source node: `docs/guides/scene-documents.md`. Materials, masked count, 184,406 vertices, largest primitive 31,436 triangles: counted from the fetched `Sponza.gltf` accessors on 2026-10-01, not a recorded measurement |
| San Miguel | 5,617,451 | 281 | 281 | 281 (97 masked, 54 with normal maps) | `docs/milestones/m6/m6.4.md` ("5,617,451 triangles ... 281 material-grouped draws, 265 images, 97 masked materials and 54 materials using normal maps"). 5,861,789 vertices and largest primitive 806,992 triangles: counted from the fetched file, not recorded |
| Damaged Helmet (inside MaterialLab) | 15,452 | 1 | 1 | 1 | Counted from the fetched GLB; not recorded in a milestone |
| Milk Truck (inside TemporalLab) | 2,856 | 4 | 5 | 4 | Counted from the fetched GLB; not recorded |
| VisibilityLab | about 46 per instance on average (12 or 80) | 2 | 4,096 default, 1 to 1,048,576 | 4 (+2 with occluders) | Code: `Source/Scenes/VisibilityLab.cpp`, `Generators.cpp`. Totals per configuration are not recorded |
| LightLab | not recorded | 3 (plane, cube, 24x16 sphere) | 17 | 9 (floor + 8 columns) | Code: `Source/Scenes/LightLab.cpp` (`kFieldColumnCount = 8`); triangle total not recorded |
| MaterialLab | not recorded | 4 generated + Helmet | not recorded | not recorded | Code: `Source/Scenes/MaterialLab.cpp` (5x5 sphere grid, patches, probes); totals not recorded |
| TemporalLab | not recorded | 4 generated + Truck | not recorded | not recorded | Code: `Source/Scenes/TemporalLab.cpp`; totals not recorded |

Every scene adds one sky-sphere mesh (20x20) to its pool. Derived sizes, from the counts above:
San Miguel's pool is about 281 MB of vertices (48 B each) and 67 MB of indices; Sponza's is about
8.9 MB and 3.1 MB.

Recorded frame cost for scale (M7.3 validation, M3 Max, 1280x720, Native TAA, timed-pass sum):
Sponza about 0.8 ms, San Miguel about 4.4 to 4.7 ms, VisibilityLab at 65,536 instances about 4.8
to 22 ms depending on submission. **No catalog scene is geometry-bound at the 60 Hz planning
target.** San Miguel's 281 draws are material groups spanning the courtyard, so per-instance
frustum culling barely applies to it (M7.2 recorded about 5% GPU change from culling), which makes
it the one existing workload where sub-instance culling has something to remove.

---

## 9. Standing constraints that shape slicing

From `docs/roadmap.md`, "Project direction and delivery":

- "Each milestone has one recognizable completion outcome. Use a few independently accepted
  slices; implementation steps belong in a just-in-time plan or PR, not an expanding series of
  milestone IDs." M9 to M11 "retain bounded work areas until planned".
- "Only one implementation plan is active at a time; independent entry does not start another
  plan." Starting a milestone needs an `In progress` plan under `docs/plans/` "that decomposes the
  accepted boundary without expanding it". `docs/conventions/documentation.md` allows at most one
  `In progress` file there. Today `docs/plans/2026-10-01-ux5-session.md` is `Proposed`; UX5 and N1
  precede M9 in the execution sequence.
- Every rendering slice includes "a diagnostic fixture, relevant intermediate views,
  deterministic seeds/camera tracks and declared temporal warmup, plus raw/reference and final
  captures. Retain Sponza and MaterialLab integration checks."
- "Freeze a device, resolution, content and CPU/GPU/memory budget before measuring; 60 real
  frames/s is a planning target, not an unmeasured performance claim. Report full
  update/render/reconstruction/composite cost, timing variation and unavailable counters."
- "Track pipeline variants, cache misses and compilation stalls when a slice introduces them."
- "Grow depth, normal, roughness, motion and identity outputs only for real consumers with shared
  meaning; add no speculative G-buffer." A visibility or ID target needs a named consumer in the
  same slice.
- "Each feature states its fallback, overflow and reset behavior."
- "Grow the thin RHI through actual consumers."

Failed-gate history (`AGENTS.md`, roadmap execution sequence): M7.1 11/15 original; M7.2 13/15 and
9/15; M7.3 two 14/15 gates and an incomplete ICB capture gate; M7.4 13/15; M7.5 11/15 zero-light
mode invariance; R3.5 and R3.6 scoped parity exceptions; R4 closed DEFER on one unexplained
Native TAA outlier; UX3 and UX4 image matrices failed as measured. Every one was owner-accepted
with the failure retained (M7.1 and M7.5 later passed scoped or re-rigged comparisons beside
it), and **no GPU-generated or occlusion default was adopted**: CPU classification, indirect
submission and occlusion-off remain defaults; only Clustered lighting became a default, on its
lossless-list and scoped exact-image gates. The recurring failing cells are Sponza Native TAA at
scale 0.5 and MetalFX cases (the M7.3, M7.4 and UX4 validation records). An exact-image gate
that crosses Native TAA or MetalFX is likely to fail for reasons unrelated to the change under
test; gates that held are list/set equality against a CPU mirror, counter reconciliation, an
independent ID oracle, and exact images with temporal off.

From `docs/conventions/`:

- `engineering.md`: land one observable outcome at a time; "Keep `main` runnable. Incomplete large
  features remain disabled or explicitly selectable"; "An interface must have a current caller and
  at least two credible implementations or a concrete portability constraint"; CPU/shader
  structures need compile-time size/offset checks and an ABI test in the same change; evidence
  table by risk (RHI or shader ABI: GPU smoke test with Metal validation; rendered output:
  fixed-camera comparison; performance claim: repeatable command, several samples, raw data).
- `modules.md`: every file belongs to exactly one unit in `Tools/module_contract.json`; a new tool
  is a new unit row; a new third-party package must be added to the owning unit's set; Asset must
  stay linkable without a GPU; Render never depends on Scenes; Render's reach into `Scene.h` is
  confined to `SceneViewBuilder.cpp`; tests mirror `Source/` paths; math with no domain meaning
  goes to Core, domain constants stay in their unit.
- `shader-style.md`: three shader folders, mirrored exposure twins, flat binding indices.
- `cpp-style.md` and the hard rules: experimental source stays on a short-lived `exp/<topic>`
  branch with an evidence tag; only conclusions, ADRs and adopted production code reach `main`.
  A surface-path comparison is an experiment by this rule.
- `commits.md`: squash merge, normally one commit per milestone; renderer/RHI/shader PRs need
  `MTL_DEBUG_LAYER=1 xmake test Tests/gpu` on Metal 4 hardware.
- `documentation.md` and `Tools/check_project_policy.py`: line budgets are enforced. Each
  roadmap file is capped at 300 lines; `docs/roadmap/gpu-driven-hybrid-rendering.md` is at 254,
  `docs/architecture/frame-pipeline.md` at 299, `AGENTS.md` at 248 of 250. Re-slicing M9 into
  several milestones with gates will not fit in the existing roadmap part; a new part file also
  means editing the part-ownership list in `documentation.md`.
- Two-repository rule: each RHI addition is a separate `rojo-rhi` change merged before the Luminex
  commit that consumes it.

---

## What exists and can be reused as is

| Piece | Where | Size of reuse | Why it is reusable |
|---|---|---|---|
| Immutable rebased vertex/index pool with vertex pulling | `Scene::finalize`, `gVertices[SV_VertexID]` | small | A cluster is an index sub-range of the same pool; existing vertex entries draw it unchanged |
| Generational identities and paced tables | `SceneIds.h`, `PacedTable.h`, ADR 0021 | small | A static cluster table is simpler than these (immutable after finalize, like the mesh table) |
| Shared row ABI discipline | `SceneTables.h`, `SceneTables.slang`, `SceneTableAbi.slang`, `GpuSceneTableAbiTests.cpp` | small | A `ClusterRow` gets the same asserts and oracle |
| Plane test and AABB math with matched FP contraction | `Core/Math/Frustum.h`, `Aabb.h`, `Visibility.slang::classifyBounds` | small | Same test applies to cluster bounds |
| Deterministic compaction kernels | `VisibilityPrefix.slang`, `VisibilityScan.slang`, `VisibilityEmit.slang`; second instance in `LightClusterCount/Scan/Fill.slang` | medium | Count, prefix, emit with explicit capacities and drop-and-count overflow is the house pattern, twice proven against a CPU mirror |
| Paced slot, readback and retirement helpers | `Render/Common/PacedSlots.h`, `GpuVisibility::retireThrough`, `LightClusterStage` | small | Frame-keyed diagnostics for a new stage follow these |
| Stage convention and helpers | `docs/conventions/engineering.md`, `Render/Common/StageSetup.h`, `GraphResources.h`, `Dispatch.h` | small | `createComputePipelines`, `declareZeroFill`, `importPaced` |
| Render graph with transient pooling, barriers for storage, indirect and readback | `Source/Render/Graph/` | small | Compute-written rows/arguments and GPU-written index buffers are already expressible |
| HZB pyramid and source-frame facts | `HzbStage`, `HzbReduce.slang`, `Occlusion.slang` | medium | Conservative min pyramid and box test apply to cluster boxes; only phase count and invalidation policy change |
| Independent ID oracle pattern | `OcclusionReference`, `OcclusionCheckHistory` | medium | Direct redraw plus identity join; extend the ID to (instance, cluster) |
| Per-pass GPU timing for every pass kind | `Device::passTimings`, `app::passStage` | small | Cull/raster/resolve costs separate automatically by label |
| Measurement run with exact frame joins and provenance | `MeasurementRun`, schema 5 | medium | Add one join and one prefix sum; protocol unchanged |
| Paired bench drivers and image tools | `Tools/Bench/*.py`, `Tools/Screenshots/*.py` | small | Workloads and controls are table entries |
| Generator registry and lab documents | `Source/Scenes/Generators.cpp`, `Assets/Scenes/` | small | Procedural dense meshes need no asset |
| Debug-view selector and legend | `DebugView.h`, `DiagnosticLegend.h` | small | One more topic |
| Bake tool template and manifest scheme | `Tools/TextureBake`, `Asset/Texture/TextureBake.h`, `Tools/bake_gltf_textures.py`, `xmake/setup.lua` | medium | Library in Asset, thin CLI, driver, manifest, `Baked/` beside the glTF |
| San Miguel as a workload | `xmake setup --san-miguel` | small | 5.6 M triangles in 281 huge draws; the only existing content where sub-instance culling can matter |
| Common shading modules | `Shaders/Common/Lighting`, `LocalLights`, `Shadow`, `Motion`, `AlphaMask` | medium | Importable by a new family; only the surface composition is not shared |
| RHI: indirect draws and dispatches, storage buffers, heaps, 3 extra color targets | `RojoRHI/Include/rojoRHI/` | small | Enough for compute culling plus instanced or index-compacted cluster raster with no RHI change |

## What must be added, by subsystem

### Asset

| Addition | Size | Reason |
|---|---|---|
| Cluster build library (clusterize, bounds, cones, hierarchical simplification through a maintained library) and a serialized cluster asset with a manifest | large | Nothing exists; needs a new third-party dependency in the `asset` unit and deterministic output |
| Reader for the baked cluster asset with validation | medium | CPU decode and validation finish in Asset by convention |
| Optional import clean-up (weld, vertex-cache order) | small to medium | Converted Sponza/San Miguel have un-shared per-material vertex streams; cluster quality depends on connectivity |

### Engine

| Addition | Size | Reason |
|---|---|---|
| Cluster and hierarchy tables in the scene (immutable after finalize), a `ClusterRow` ABI, and a link from `MeshRow` to its clusters | medium | `MeshRow` is one range with one box; no LOD or sub-range concept exists |
| Upload of baked cluster data in `appendGltfScene`, with the "no bake, plain mesh" fallback | small to medium | Mirrors `bakedDdsPath` |
| `SceneTables`/`SceneTableStats` fields and capture layouts for the new buffers | small | Borrowed bindings and diagnostics are how Render sees the scene |
| Decision on keeping CPU geometry after finalize | small | `finalize` frees `MeshData`; a CPU cluster oracle or later ray-tracing build needs it or the baked data |

### Render

| Addition | Size | Reason |
|---|---|---|
| Cluster cull stage: GPU candidate generation (instance x cluster), frustum/cone/LOD tests, compaction, capacities, counters, readback, CPU mirror | large | Today candidates are CPU-built per instance and capacity cannot overflow in production |
| LOD selection with a declared error metric and transition rule | medium to large | The M9 exit gate requires declared error and stability limits; nothing exists |
| N-view parameterization of visibility (views array, per-view planes and outputs) | medium | Kernels hard-code two views; shadow is unculled |
| Two-phase occlusion and a per-instance rather than global invalidation policy | medium to large | Single phase, previous frame, global invalidation today |
| Cluster raster path in `SceneStage`/`ShadowStage` (clusters as instances or compacted indices) with its pipeline variants | medium to large | Up to 27 more pipelines if every variant is mirrored; pipeline count must be tracked |
| Cluster-ID oracle (extended `OcclusionReference`) | medium | The mesh-shader and fallback paths must match on visible set |
| Surface-path experiment (visibility target, resolve with analytic derivatives, material access) | large | Belongs on an `exp/` branch by rule; needs RHI additions and a fifth copy of shading or an ADR 0027 reopening |
| Cluster debug stage | small to medium | Post-display pass like the light and HZB views |

### RHI (each a `rojo-rhi` change, merged first)

| Addition | Size | Reason |
|---|---|---|
| `R32Uint` (and `RG32Uint` if needed) with render-target, load, storage and readback support | small | No integer format exists |
| Multi-draw or indirect command buffer | large | One argument record per call today; the ICB spike left a failed capture gate |
| Mesh pipeline, mesh draw commands, capability bit | medium to large | No object/mesh stage hook; capabilities report only the temporal scaler |
| Texture arrays or a wider texture table | medium to large | 16 texture slots, bound per run |
| Safe-math selection decoupled from name prefixes | small | The prefix list is duplicated in the backend's runtime-compile path |

### Shaders

| Addition | Size | Reason |
|---|---|---|
| Cluster cull family (or `Visibility*`-prefixed kernels to inherit precise math) and its oracles | medium | New kernels plus CPU-mirror tests |
| `Common` cluster ABI/decode module | small | Shared by cull and raster families, so it cannot be family-local |
| Cluster vertex entries mirrored across four scene files and two shadow files, or a separate cluster scene family duplicating shading | medium | ADR 0027 keeps the twins; either choice is hand-mirrored |
| Resolve shader with analytic derivatives, including a replacement for `ddx`/`ddy` in `LocalLights.slang` | large | Only for the surface-path experiment |
| Build-rule edit for any new precise-math prefix | small | `xmake/shaders.lua` and the backend list must agree |

### Tools

| Addition | Size | Reason |
|---|---|---|
| `Tools/GeometryBake` CLI, its unit row in the module contract, a driver script, setup wiring, tests | medium | Follows `TextureBake`; "general asset tooling" is deferred, so it stays thin |
| Third-party pin for the clusterization library | small | Hard rule on dependencies; setup verifies hashes |
| Bench driver workloads/controls and schema-6 acceptance | small | Dictionary entries plus the version check |
| Optional dense-content fetch with provenance and notices | small to medium | No catalog scene is geometry-bound today |

### App

| Addition | Size | Reason |
|---|---|---|
| GeometryLab generator, document, catalog entry, CLI overrides, lab description, tests | medium | Six places are hard-coded lists (`kIds`, generator whitelist, overrides, options, plan, legend) |
| CLI flags and conflict rules for cluster mode, LOD bias, checks and views | small | `AppOptions.cpp` pattern |
| `EditorRenderSettings` fields, a Rendering topic and a 250 ms display model for cluster counters | medium | Mirrors `VisibilityDisplay`/`LightingDisplay` |
| `DebugViewTopic::Geometry` with labels, reasons and legend | small | One more exclusive topic |
| Measurement schema 6: cluster join, counters, `clusterGpuMs`, plan fields | small to medium | `MeasurementRun` has one join function per GPU family |
| Documentation within line budgets (new roadmap part, architecture page, guide) | small | Three files are already within 1 to 46 lines of their caps |
