# Engine, Asset and Scenes

**Status**: Implemented

`Source/Engine/` holds two units: `Asset`, a CPU-only content library, and `Engine`, the scene
vocabulary and the GPU-resident scene itself. `Source/Scenes` holds a third unit, the catalog of
scenes built on top of Engine. Asset depends on Core alone; Engine depends on Core, Asset and
RojoRHI; Scenes depends on Core, Asset, Engine and RojoRHI. Render depends on Engine, and through it
on Asset, but Engine may not depend on Render, and Render never depends on Scenes.

## Asset

`Source/Engine/Asset` (target `Asset`, namespace `lmx::asset`) keeps `Asset.h` and repository asset
discovery at its root, with `Document/`, `Image/`, `Model/` and `Texture/` folders. It owns procedural geometry,
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
The scene owns its Asset-defined `SceneLook`: exposure, bloom, shadow filter and environment.
`buildSceneView` converts that CPU vocabulary into Render settings. Reconstruction, render scale,
visibility, occlusion, submission and lighting mode remain renderer/session configuration. See
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

## Scene documents and catalog

`Asset/Document/SceneDocument` is a GPU-free glTF 2.0 model with `KHR_lights_punctual` and the
optional `LMX_scene` extension. It references fetched assets under `Assets/`, records their file
hashes, and owns the look, cameras, rails, own-enabled flags and imported-node overrides.
`SceneDocumentRead` validates fields and buffer access; `SceneDocumentWrite` uses Core's
`JsonWriter` for canonical JSON plus an external animation buffer, written only when the document has animations. `Orientation` searches exact
float quaternion encodings with contraction disabled. `Model/CgltfImplementation` owns the single
cgltf implementation and `JsonTokens`; `GltfLoader` retains source nodes, primitive bindings and
all animation clips. The [document guide](../guides/scene-documents.md) describes the file form.

`Scene/SceneInstantiate` preflights asset hashes, overrides, cameras, lights and required HDRI
content before GPU creation, then constructs a replacement `LoadedScene`. It carries the scene,
loaded document, path/hash and `SceneBinding`. Bindings distinguish document nodes, imported
source nodes (including empty ancestors), primitive instances and generated subjects. Imported
node pose/enabled edits fan out to every primitive of that source node. Resource creation retains
asset order, the generator's environment attachment point and document light order. Engine calls
an injected generator lookup; it names no Scenes symbol.

`Source/Scenes` owns `SceneLibrary`, the generator registry and six catalog documents under
`Assets/Scenes/`: Sponza, MaterialLab, TemporalLab, San Miguel, VisibilityLab and LightLab.
`SceneId` holds a catalog key or supplied document path. San Miguel still requires
`xmake setup --san-miguel`; its document owns the 12-second rail. Sponza's document owns sixteen
local lights and its 120-second tour. MaterialLab references Damaged Helmet, and TemporalLab
references Milk Truck; their former standalone catalog IDs have retired. Generated lab geometry,
emissive step tracks and closed-form light orbits stay in Scenes. CLI generator parameters and
`--local-light-rig` apply session overrides without changing authored document values.

`SessionDocumentState` retains own flags, the immutable imported-pose baseline and an explicitly
set scene camera. `exportSceneDocument` derives a candidate document from the loaded snapshot,
this state and the live scene. It uses the nearest quaternion when no exact orientation exists and returns an error for
inconsistent primitive poses, pose overrides on nodes without meshes and non-finite look values. `documentDirty` compares canonical JSON and buffer bytes; generated
edits, animation preview and ordinary editor-camera movement do not participate. Save adoption
replaces the document baseline only after write, canonical reread/equality and hash succeed.

Runtime document animation currently accepts LINEAR translation/rotation of the selected camera.
Referenced asset clips loop on their own durations using retained local hierarchy/channels;
generators own their other motion. Unsupported document channels and transformed generator roots
fail preflight. STEP scale keys are validated, and an indecomposable sampled pose holds the previous pose with one
warning. The [validation record](../milestones/ux/ux3-validation.md#parity-root-cause) retains the
failed migration image gates and their cause.

## Authored enabled state

Own-enabled flags combine by ancestor AND. Disabling an object keeps its identity and table row,
sets `kInstanceDisabled = 4u`, and bumps the occlusion coverage epoch. Both classification paths
reject it before culling bypasses, including the unculled shadow view. Disabled rows contribute
only to the disabled counter, with no colour, depth, motion, shadow, outline or HZB coverage.
`AuthoredOff` means disabled; `CullingOff` means view culling is bypassed. Frustum/HZB rejection
remains transient and never changes authored state.

Disabled local lights retain their identities and edits. A disabled directional retains its role
and direction but contributes zero strength; the selected enabled caster receives the shadow
factor without changing pass declarations. Measurement freezes the starting population and
refuses enabled edits. [Proposed ADR 0028](../decisions/0028-scene-document-contract.md) records
the population amendment to ADR 0021; identity and three-slot retirement rules remain unchanged.

## Tests

- `Tests/Engine/Asset/` covers Asset: repository discovery and BMP writing, DDS loading and mip
  baking, procedural geometry, glTF loading, HDR environment conversion, IBL generation, PNG
  handling, transform decomposition, document reader/writer failures and rigid, camera and orbit sampling.
- `Tests/Engine/Lights/` covers `LocalLightMath`.
- `Tests/Engine/Scene/` covers the scene-table row ABI and its GPU upload, world-bounds recomputation,
  coverage-epoch advancement, generational scene identities, the local-light table and playback.
- `Tests/Engine/View/` covers `Camera`.
- `Tests/Scenes/` covers the catalog and each scene and lab it lists, including San Miguel import,
  LightLab, MaterialLab, camera rails, TemporalLab and VisibilityLab, canonical document round trips,
  source bindings, pure export/dirty state and enabled-state persistence.
