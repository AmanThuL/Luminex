# Renderer and Passes

**Status**: Implemented

`Render` composes a `Renderer` over a borrowed `SceneView` into ten shader-matched pass families
under `Source/Render/Passes/<family>/`, sharing mechanics through `Source/Render/Common/`. It
depends on Engine for the camera, the CPU geometry vocabulary (`Vertex`/`MeshData`) and the shared
scene-table rows; see [ADR 0025](../decisions/0025-engine-subsystem-and-render-on-engine.md) and
[module dependencies](../conventions/modules.md) for the render-on-engine edge. The
[render graph](render-graph.md) declares and compiles the passes described here, and the
[frame walkthrough](frame-pipeline.md) orders them within one frame.

## Renderer

`Renderer` composes `ShadowStage` and `SceneStage`, which own the opaque and masked pipelines and
the direct, indirect or batched submission described under Visibility and submission modes.
`SceneStage` draws sky last from the shared geometry pool. `Renderer` also composes the private
`ExposureStage`, `BloomStage` and `DisplayStage`, which own their pipelines and resources, and
keeps frame ordering and target selection. Construction, per-frame derivation and imports, and
status recording live in separate units (`RendererCreate.cpp`, `RendererFrame.cpp`,
`RendererStatus.cpp`), alongside stage-specific units for lighting, occlusion and visibility
(`RendererLighting.cpp`, `RendererOcclusion.cpp`, `RendererVisibility.cpp`); `Renderer.cpp` keeps
the stage wiring itself.
`SceneStage` separates layouts (`SceneStageLayouts.cpp`), pipeline creation
(`SceneStagePipelines.cpp`) and draw encoding (`SceneStageDraw.cpp`) from packing and declaration
(`SceneStage.cpp`/`.h`).

`Renderer` imports five read-only `lmx.scene.*` buffers (vertices, indices, meshes, instances and
materials), a sixth `lmx.scene.lights` import that exists only when a light is live, and
`lmx.draw.rows`/`lmx.draw.args`, which declare list and indirect-argument reads.

## Scene view and buildSceneView

`Render/Renderer/SceneView.h` holds the borrowed frame input independently of the renderer that
consumes it. `render::buildSceneView` (`Renderer/SceneViewBuilder.h`) produces it from a scene,
describing mesh ranges and textures and borrowing the CPU rows plus the five geometry/material GPU
buffers and the live-only lights. Engine's `SceneTables.h`, mirrored by its Slang module, defines the
240-byte instance, 112-byte material, 48-byte mesh and 64-byte local-light rows, including bounds;
instances and meshes carry both world and local AABBs. `SceneViewBuilder.cpp` is the one translation
unit allowed to reach `Engine/Scene/Scene.h` itself, beyond the `Engine/View/`, `Engine/Lights/`,
`Engine/Geometry/` and `Engine/Material/` vocabulary and the `SceneTables.h`/`DrawItem.h`/
`MotionClass.h` headers Render includes generally ([module dependencies](../conventions/modules.md)).

## Common helpers

`Source/Render/Common/` shares free functions and templates across the pass families: `Dispatch.h`
for compute dispatch, `GraphResources.h` for checked graph-resource lookups, `StageSetup.h`/`.cpp`
for shared pass setup, and `PacedSlots.h` for paced-buffer retirement. `DrawEncoding.h` holds
`bindSceneTables`, which the scene, shadow, occlusion-reference and selection-outline passes use to
bind the scene tables, and `encodeDrawRuns`, the direct or indirect draw-run loop, which only
`SceneStage` uses; the shadow pass keeps its own run loop, as the
[adoption measurements](../milestones/r/r3.5-validation.md) found.

## Visibility and submission modes

`Visibility.h` classifies the canonical uploaded CPU rows against five normalized planes derived
from the jittered raster view-projection, with a 1e-3 world-unit guard and no far plane. A rejected
row keeps its identity and motion; disabled, shadow-view, unreliable-bounds and nonfinite-transform
bypasses stay inspectable, and shadow candidates are never culled. CPU-only production leaves
`lmx.draw.rows` and `lmx.draw.args` without a GPU writer; opt-in `GpuVisibility` adds reset,
classify, scan and emit passes with deterministic fixed slots and retired diagnostics
([contracts](../guides/gpu-visibility.md)).

`Renderer` owns `DrawSubmission`: three paced, growable row/argument buffer pairs, retained on
replacement until the last prepared frame plus 3, with scene entries preceding shadow entries. The
default indirect mode issues one command per retained object; batched mode stably sorts pipeline,
material and mesh keys and issues one instanced command per run; direct submission remains
selectable. The 16-byte b1 `DrawUniforms.firstEntry`, combined with the shader instance index,
selects a b4 visible-list entry, which points to the instance and material rows at b5 and b6;
vertices at b0 and the rebased index pool are shared per pass.

## Occlusion and HZB

Opt-in `HzbStage` owns two output-capacity `R32Float` pyramids that alternate independently of the
temporal slots: half-resolution minima, one pass per mip, then a publication read of all mips. GPU
`Occlusion` tests each candidate against the preceding declared frame's matrix, jitter and active
extent. `OcclusionHistory` retains globally on invalid source, camera or coverage evidence, or on
wireframe. `OcclusionReference` draws every candidate directly into private ID and depth targets,
and at retirement `OcclusionCheck`'s `OcclusionCheckHistory` joins the observations into
generational missing-frame streaks and a pass/fail `OcclusionCheckResult`. The
[GPU visibility guide](../guides/gpu-visibility.md) records the HZB and occlusion contracts beside
visibility's.

## Scene, shadow and masked materials

`AlphaMode::Mask` selects dedicated `ScenePassMask`/`ScenePassAutoMask` and `ShadowPassMask`
pipelines ([ADR 0018](../decisions/0018-masked-material-coverage.md)). Shared `AlphaMask.slang`
discards a fragment when its base-color texture alpha times factor alpha falls below the material's
cutoff; color, depth, motion and reactive coverage share one scene invocation, and masked shadows use
the same UV transform and cutoff. One- and two-sided pipeline variants support foliage and reversed
back-face shading normals, with cutoff and sidedness taken from the shared material row.
`AlphaMask.slang` stays a pure coverage function, with no per-draw alpha-mask frame-data block.

## Clustered local lighting

`LocalLightMode` selects Off, Direct (the reference) or Clustered (the default) in one shared
punctual-light loop. Clustered is the default because its lossless-list and scoped exact-image
gates passed, independently of cost
([default decision](../milestones/m7/m7.5-validation.md#default-decision)); owner acceptance is
recorded separately. Point and spot terms follow the directional sum before ambient, emissive and
the single pre-exposure multiply, and affect opaque and masked surfaces without casting shadows.
Shared punctual normal-footprint filtering broadens only the specular lobe; directional and IBL
terms are unchanged. `PassUniforms` stays 400 bytes; the shared `LocalLights.slang` module selects
the mode using local lights at b8, the cluster grid at b9, indices at b10 and frame parameters at
b11.

Clustered mode adds `LightClusterStage`, with three paced slots and reset, count, scan and fill
passes ahead of scene shading. Its 16×9×24 pixel-aligned froxels use the jittered projection and the
uploaded reversed-Z slices; ordered fp32 CPU/GPU predicates, ascending row lists, a 128-light
per-froxel cap and a 65,536-index cap make truncation deterministic. `LightClusters` owns the CPU
mirror, and `LightClusterCheck` compares that declaration-time mirror against the retired GPU lists
and counters. `LightingStatus` retains the frame, scene, and requested/effective mode for a
declaration; a zero-live-light frame uses Off and declares no light-list or debug pass. Lazy
`LightDebugStage` reads the actual scene depth and lists after display and renders count, overflow
or missed-light views into the display target from a dedicated SDR source transient, leaving scene
and history unchanged. [ADR 0023](../decisions/0023-local-light-and-cluster-contract.md) owns the
accepted contract for this family. Historical image failures, current diagnostics and evidence
limits are recorded in the [validation record](../milestones/m7/m7.5-validation.md) and the
[follow-up](../milestones/m7/m7.5-followup.md).

## Temporal reconstruction

Render owns the camera's temporal history and the GPU-resident motion/history contract
(`Temporal.h`, `TemporalHistory.h`, `Shaders/Common/Motion.slang`): the previous `CameraFrameState`,
the Halton jitter sequence, the derived `HistoryResetReason`, and the `lmx.render.motion`/
`lmx.render.reactive` textures `Renderer` creates and the temporal passes declare when
`SceneView::temporal.enabled` is set
([ADR 0013](../decisions/0013-temporal-motion-and-history-contract.md), narrowed by ADR 0016).

`TemporalResolve` ([ADR 0014](../decisions/0014-temporal-reconstruction-and-exposure-correction.md))
keeps native, upscale, vendor and diagnostic declaration units behind one history owner; the native
and upscale kernels share one `declareReconstruction` declaration, each supplying its own
parameter block, pipeline, pass name and dispatch extent. It owns two
ping-ponged colour/depth slot pairs (`lmx.render.historyColor0/1`, `lmx.render.sceneDepth0/1`) and
the pipelines that reproject, reject, clip and blend a native `NativeTaa` frame, or commit a raw
copy under `Raw`; every temporal frame's colour slot holds that frame's output regardless of mode,
which is what lets a mode switch skip a history reset. A colour import records its final consumer's
access: `ShaderRead` after `NativeTaa` or after `Raw`'s HistoryAge view, and `CopyDestination` for
a raw commit otherwise. The previous slot changes only when read
([ADR 0015](../decisions/0015-temporal-slot-terminal-access.md)). These targets are allocated with
the scene targets and recreated by `resize()` alongside them, so the allocation exists from
creation and enabling temporal allocates nothing.

Every temporal target allocates at the *output* extent regardless of `SceneView::temporal.renderScale`
([ADR 0016](../decisions/0016-active-render-extent-and-resolution-control.md)). `Temporal.h`'s
`renderExtentsForScale`, `jitterTexelOffset` and `renderSamplePosition` derive the active *render*
rectangle that a scale below 1.0 confines the scene pass and reconstruction to, and
`TemporalHistory`'s `ExtentChanged` fires on an output-extent change alone, so a render-scale change
alone derives `None` and reuses history. `TemporalResolve` selects between the native `NativeTaa`
kernel (`Shaders/Passes/Temporal/TemporalResolve.slang`) and the upscale kernel
(`Shaders/Passes/Temporal/TemporalUpscale.slang`) by whether render equals output extent and history
was not just accumulated at another extent; `Raw` gets the matching split against
`Shaders/Passes/Temporal/SpatialUpscale.slang`. Shared reason codes, constants and colour-space
helpers live in `Shaders/Passes/Temporal/TemporalCommon.slang`, imported by both kernels.
`Source/Render/Passes/Temporal/ResolutionController` is a pure class, with no device, graph or App
dependency, that proposes the next render scale from a retired frame's summed GPU pass time against
a budget, with hysteresis.

`VendorTemporal` selects a composed `VendorTemporalScaler` inside the existing resolve stage
([ADR 0017](../decisions/0017-vendor-reconstruction-capability.md)). It lazily creates the device
scaler and replaces only the reconstruction kernel: a packing pass translates invalid motion,
reactive weight and reciprocal applied exposure, then an external pass writes the current colour
slot, recorded as `ExternalRead`/`ExternalWrite` with no scope opened around the external callback
and the usual dependency, culling and transient rules otherwise unchanged. Output resize recreates
the scaler; a scale change only moves the content rectangle. Entering vendor reconstruction, or an
engine reset, discards the vendor's own private history without resetting engine history on a mode
switch, and an unsupported or failed creation falls back to Native TAA with an explicit status
reason. Native reprojection diagnostics stay available in vendor mode: `VendorTemporalHistory.slang`
supplies corrected reprojected history only for that selected view, while rejection, blend-weight and
per-pixel age remain native-only. Vendor colour imports retain conservative `ExternalWrite`; current
depth and scene colour record `ExternalRead`, and the native terminal accesses described above are
unchanged.

## Exposure, bloom and display

`ExposureStage`, `BloomStage` and `DisplayStage` (composed under Renderer above) declare histogram
exposure (clear, accumulate and resolve, with bounded adaptation and GPU-resident
`{applied, previous}` feedback into the next frame), bloom (threshold, downsample and bilinear
upsample) and the display transform, respectively. `Render/Renderer/DisplayDomain.h` names the one
output domain the display transform writes: opaque 8-bit SDR with BT.709 primaries, the sRGB
transfer function and the PBR Neutral tone map. `Renderer` exposes it to capture metadata and to
the read-only Inspector Display details
(domain, encoded SDR UI, backing scale and 1:1 status).

## Selection outline

App alone declares `Render/Passes/SelectionOutline/SelectionOutline`, an editor-only utility it
opts into after scene display. It reads full-resolution, unjittered selected-only coverage and
depth plus scene visibility, using the same instance and material tables and masked alpha as the
main scene pass, so the outline preserves the true silhouette. A soft 1.5-logical-point border is
depth-tested at both its source and destination before compositing into its own SDR target, so an
occlusion cut in front of the selection does not read as an edge. `lmx.pass.selection.coverage`,
`lmx.pass.selection.visibility` and `lmx.pass.selection.outline` appear in graph costs; the
ordinary Renderer and offscreen paths never declare these passes, and scene targets, temporal
history and scene-only captures stay unchanged.

## Tests

- `Tests/Render/Renderer/`: the renderer's capture-schema layout registration (with four RojoRHI
  `CaptureSchema` cases kept beside it) and `DisplayDomain` naming.
- `Tests/Render/Common/`: `Dispatch.h`'s shared compute-dispatch helper.
- `Tests/Render/Passes/Bloom/`: bloom threshold/downsample/upsample against CPU references.
- `Tests/Render/Passes/Display/`: the display-transform pass.
- `Tests/Render/Passes/Exposure/`: exposure seed/accumulate/resolve and its composition with display.
- `Tests/Render/Passes/LocalLights/`: cluster count/scan/fill, the debug views, the shared
  punctual-light loop, `Renderer`'s lighting composition, `LightClusterCheck`/`LightClusters`, the
  LightLab cluster case and the zero-enabled-light invariant.
- `Tests/Render/Passes/Occlusion/`: HZB construction and graph wiring, occlusion against the graph
  and probes, the occlusion reference oracle and `OcclusionCheck`.
- `Tests/Render/Passes/Scene/`: masked-material coverage, `Renderer`'s scene composition, scene
  shading and the BRDF furnace/limit cases.
- `Tests/Render/Passes/SelectionOutline/`: the selection-outline passes.
- `Tests/Render/Passes/Shadow/`: `Renderer`'s shadow composition and `fitShadowOrtho`.
- `Tests/Render/Passes/Temporal/`: temporal accumulation, history, motion, upscale, vendor pack and
  vendor reconstruction, the resolution controller, and the zero-light history case.
- `Tests/Render/Passes/Visibility/`: CPU and GPU visibility, its contribution and graph wiring, the
  visibility rail and work cases, and visibility's row tables.
