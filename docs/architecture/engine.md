# Engine, Asset and Scenes

**Status**: Implemented

`Source/Engine/` holds two units: `Asset`, a CPU-only content library, and `Engine`, the scene
vocabulary and the GPU-resident scene itself. `Source/Scenes` holds a third unit, the catalog of
scenes built on top of Engine. Asset depends on Core alone; Engine depends on Core, Asset and
RojoRHI; Scenes depends on Core, Asset, Engine and RojoRHI. Render depends on Engine, and through it
on Asset, but Engine may not depend on Render, and Render never depends on Scenes.

## Asset

`Source/Engine/Asset` (target `Asset`, namespace `lmx::asset`) keeps `Asset.h` and repository asset
discovery at its root, with `Image/`, `Model/` and `Texture/` folders. It owns procedural geometry,
DDS/glTF/Radiance HDR and PNG/BMP image handling, deterministic equirectangular environment
conversion and image-based-lighting generation (`HdrEnvironment.h`, `Ibl.h`), including filtered
cubemap sampling and a higher-resolution studio reflection source with a separate bounded diffuse
source for material preview, deterministic offline texture mip baking (`TextureBake.h`), animation
clip data and sampling, and the asset error domain. The glTF loader carries its own MASK
cutoff/double-sided vocabulary and rejects referenced BLEND materials.

Asset depends on `core` alone and links no GPU target: its RojoRHI header allowance admits only
`rojoRHI/Format.h` and `rojoRHI/TextureDesc.h`, so a tool such as `TextureBake` links Asset without
Metal. The [module contract](../conventions/modules.md#asset-independence) states the include,
framework and archive checks that hold this boundary.

## Engine's vocabulary

The rest of `Source/Engine/` is the static library `Engine`, namespace `lmx::engine`, split into
`View/`, `Lights/`, `Geometry/`, `Material/`, `Scene/` and `Upload/`. `View/` holds `Camera`;
`Lights/` holds `LocalLightMath`, `LocalLight.h` and `DirectionalLight.h`; `Geometry/` holds the
CPU geometry vocabulary, `Vertex` and `MeshData`; `Material/` holds `AlphaMode.h` and
`MaterialRecord.h`, whose record holds a material's factors and optional `TextureId`s (a null
handle selects the renderer's fallback); the scene view resolves per-draw texture pointers from
those handles. `Scene/` holds `DrawItem.h`, `MotionClass.h`, the shared `SceneTables.h` row ABI, and
the scene itself. Render consumes this vocabulary directly, plus the `SceneTables.h`, `DrawItem.h`
and `MotionClass.h` headers `Scene/` exports, but its reach into the scene type itself
(`Engine/Scene/Scene.h`) stays confined to one translation unit, the `SceneView` builder; see
[render-passes.md](render-passes.md#scene-view-and-buildsceneview) for what it builds and reads.
Renderer configuration (temporal settings, shadow filter, visibility, occlusion and lighting modes)
stays in Render; the scene carries none of it. See
[ADR 0025](../decisions/0025-engine-subsystem-and-render-on-engine.md) for the render-on-engine
edge and [ADR 0021](../decisions/0021-gpu-scene-handoff-contract.md) for the scene-identity and
update contract.

## Scene ownership

A scene is identified by five distinct generational handles, `InstanceId`, `MeshId`, `MaterialId`,
`TextureId` and `LightId`, each an 8-byte alias of Core's tagged handle template over its own slot
allocator; a stale generation or a handle from a foreign scene fails a checked query. `addMesh`,
`addTexture`, `addMaterial`, `addObject` and `addLight` build a scene; `finalize` merges every
mesh's geometry, including the sky, into one immutable, rebased vertex/index pool and allocates the
scene's GPU table slots. Slot indices stay stable across draw-list removal and reorder. `addLight`,
`updateLight` and `removeLight` use the local-light table; disabled lights keep their identity, row,
edits and orbit binding, and `localLights()` lists them alongside live ones, while
`enabledLightCount()` feeds the renderer's live count. `rigLightIds()` reports an authored light
rig's identities. Only lights added before `finalize` receive orbit-track indices; lights added
later stay static, and light edits do not advance the coverage epoch. Every mutation happens before
`prepareFrame`.

Instance, material, mesh and local-light rows live in triple-paced GPU tables, one copy per frame
in flight, each tracking its own dirty rows; a change marks its row dirty in every slot, so a static
scene converges to zero writes, and the borrowed CPU instance rows match the prepared GPU slot.
`prepareFrame(frameNumber)` follows the RHI's `Device::beginFrame` and updates only the rows dirty
for that retired slot. `addMesh` computes each mesh's local bounds (there are no per-object local
bounds), and `prepareFrame` recomputes world bounds from them with Core's shared AABB corner
transform, advancing the scene's coverage epoch for geometry and mask edits, excluding
previous-pose and emissive-only changes; `Scene::meshBounds` also supplies editor selection framing.
A table's growth doubles its capacity and retains the old GPU buffer until the frame that last read
it retires, three frames later; texture removal invalidates its handle immediately but defers the
allocation's release the same way. The scene's owner must wait for every GPU read to retire before
destroying it.

The scene tracks each object's previous transform (`SceneObject::previousModel`,
`motionClass`) and resets or commits it through `Scene::resetMotion`/`commitFrame`; a new instance
seeds its own previous pose, and `commitFrame` promotes transforms only when the caller accepts the
rendered frame. The scene plays back Asset's rigid animation tracks and follows a camera track when
one is authored. The glTF loader, `loadGltfScene`, takes an optional `SceneAuthoring` callback that
adds content, such as lights, before `finalize` runs.

## GPU uploads

`Upload/` holds `DdsUpload.h`, `IblUpload.h` and the shared `SceneEnvironment.h` sky/light rig.
Engine performs the scene's texture and IBL uploads and its initial camera mapping, and gives each
object a source-derived name. `SceneEnvironment.h` is public so that a catalog scene can attach its
own environment through it.

## Scenes catalog

`Source/Scenes` (target `Scenes`, namespace `lmx::scenes`) owns the eight-scene catalog,
`SceneLibrary`, above Engine. Engine reaches a catalog entry only through the `SceneAuthoring`
callback that entry passes to `loadGltfScene`; the `Engine` archive's `forbidUndefined:
lmx::scenes::` check keeps Engine from naming any Scenes symbol
([module contract](../conventions/modules.md#render-depends-on-engine-not-the-reverse)). Catalog
entries include `temporal-lab`, `milk-truck` and the optional `san-miguel`. San Miguel is imported
at authored metre scale with a deterministic 12-second camera rail; `xmake setup --san-miguel`
fetches its pinned official archive, converts the realtime OBJ with diffuse alpha cutouts and `N_`
tangent normals, preserves both upstream metadata and the bundled license in provenance, and bakes
its referenced images. The always-available VisibilityLab adds a seeded cube/icosphere grid, four
materials, five initial camera boundary probes and a 12-second rail, and its configurable total
instance count includes those probes.
LightLab adds a deterministic point/spot grid, a 12-second rail, position-only orbit tracks and an
optional overflow pile; authored orbits clear material rows by 0.25 m. Sponza authors 16 static
lights through `SponzaLightRig` and a 120-second two-level corridor/atrium camera tour; disabling
the light rig on the command line removes the lights' contribution without removing their
allocation. AppModel, App and Tests link `Scenes` and reach the catalog through it.

## Tests

- `Tests/Engine/Asset/` covers Asset: repository discovery and BMP writing, DDS loading and mip
  baking, procedural geometry, glTF loading, HDR environment conversion, IBL generation, PNG
  handling, transform decomposition and rigid, camera and orbit track sampling.
- `Tests/Engine/Lights/` covers `LocalLightMath`.
- `Tests/Engine/Scene/` covers the scene-table row ABI and its GPU upload, world-bounds recomputation,
  coverage-epoch advancement, generational scene identities, the local-light table and playback.
- `Tests/Engine/View/` covers `Camera`.
- `Tests/Scenes/` covers the catalog and each scene and lab it lists, including San Miguel import,
  LightLab, MaterialLab, the Sponza camera tour, TemporalLab and VisibilityLab, the `loadGltfScene`
  authoring callback and the Sponza light rig.
