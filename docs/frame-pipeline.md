# Luminex — one frame with native or vendor reconstruction (2026-09-12)

What the renderer does between `beginFrame` and `endFrame`. Native TAA is the default; vendor
reconstruction is selected explicitly and shares the engine-owned temporal inputs.

## The frame at a glance

A `RenderGraph` is declared fresh every frame: it imports the renderer's own targets, the
persistent histogram and exposure buffers, and the swapchain drawable; the shadow, scene+sky,
exposure-feedback, temporal, bloom, and display passes declare their reads, attachments, and writes
over them; `compile()` proves the declarations form a DAG and answers a serial schedule (with
dead-pass culling) before any of it reaches the GPU. `execute()` then runs that schedule, deriving
the RAW, WAR, and WAW barriers each declared cross-pass access conflict justifies.

Below is the *default* frame — manual exposure, bloom on, temporal on with `NativeTaa`
(`SceneView::temporal.enabled == true`, `reconstruction == NativeTaa`, the default since M6.2), at
`renderScale == 1.0`, byte-identical to M6.2's declaration (below 1.0 the scene pass's render area
shrinks; see Resources and lifetime for capacity). Auto-exposure, bloom, and temporal are ordinary
declared passes either way; toggling one off removes only the declaration reaching a sink.

```
beginFrame (blocks until frame N-3 retired; shared-event pacing, arena page-cursor recycle invariant asserted)
│
├─ declare: import shadow map, scene color (HDR), this frame's depth and colour history slots, the
│           other slot's depth and colour, display color, histogram buffer, exposure buffer
│           {applied, previous}, swapchain drawable
│
├─ 0. lmx.pass.exposure.seed   compute, 1 thread → exposure buffer
│       shift-and-set: `previous = applied; applied = exp2(manualEV)`, always the CPU-computed
│       manual exposure, whichever mode is running — including on an auto reset frame, where it is
│       what restarts the loop from the manual value (spec 9). Manual mode: declared every temporal
│       frame. Auto mode: only on a reset trigger
│
├─ 1. lmx.pass.shadow      depth-only → shadow map (2048², D32Float, store)
│       every opaque DrawItem, depth bias {-4.0, -32.0} (negated for reversed-Z), light 0 only
│       reversed depth: clears to 0, Greater compare, comparison sampler GreaterEqual
│
├─ 2. lmx.pass.scene       → scene color (RGBA16Float), lmx.render.motion (RG16Float, extra 0),
│    │  lmx.render.reactive (R8Unorm, extra 1, cleared to 0) + this frame's depth slot, viewport-sized
│       │  per-pass uniforms (b2): viewProj, shadowTransform, eye, time, preExposure,
│       │  3 directional lights, shadow filter
│       ├─ opaque DrawItems: GGX metallic-roughness BRDF (direct lights) + diffuse/specular IBL
│       │    (split-sum reconstruction with Fdez-Agüera multi-scatter compensation) + shadow
│       │    factor (25-tap Poisson PCF or PCSS) + normal mapping (TBN) + occlusion (image-based
│       │    terms only) + emissive, summed and pre-exposed; nothing here encodes sRGB. Per-draw
│       │    per-draw transforms and material fields (b1), bindless vertices (b0), material
│       │    textures, shadow map and the scene's irradiance/prefiltered/DFG IBL textures
│       ├─ reads the shadow map's written version — the graph derives the barrier from that
│       │    declaration
│       ├─ auto-exposure only: binds ScenePassAuto.slang/SkyAuto.slang's pipelines instead of
│       │    ScenePass.slang/Sky.slang's, reading the exposure buffer directly in place of the
│       │    manual `preExposure` uniform — two shader files so the manual pipeline's compiled
│       │    output stays provably identical to pre-M5
│       ├─ motion: `uvCurrent - uvPrevious` from unjittered clip positions (Temporal.h,
│       │    Motion.slang); an Invalid-class draw writes the +inf sentinel instead. Reactive:
│       │    `saturate(luminance(emissiveTerm · preExposure) / kReactiveEmissiveScale)`
│       └─ sky, drawn last: camera-centered sphere pinned to the reversed far plane (depth 0),
│            cull none, GreaterEqual, t2 cubemap, the same preExposure, motion from the previous
│            eye's rotation alone, reactive 0
│
├─ [reset reason None] 2b. lmx.pass.temporal.reproject   compute, diagnostic only, over the output
│       extent, reads the previous colour slot + scene colour + motion → temporalDiagnostic; culled
│       unless ReprojectionError sinks it
│
├─ 3. NativeTaa when render == output and (this frame resets or previous extents match):
│       lmx.pass.temporal.resolve   compute, 8×8, reads scene colour, both depth slots, motion,
│       reactive, the previous colour slot, the exposure pair → this frame's colour slot directly
│       (no copy) and, only when sunk, the rejection/reprojected transients. Dilated motion/
│       disocclusion, history exposure correction, YCoCg clipping, an inverse-luminance blend
│       saturating to 1 on rejection or full reactive weight (`Shaders/TemporalResolve.slang`,
│       ADR 0014); any other frame runs lmx.pass.temporal.upscale instead (`Shaders/
│       TemporalUpscale.slang`, ADR 0016). Raw: commitHistory or commitUpscaled writes this frame's
│       colour slot instead, right here
│
├─ 4. lmx.pass.exposure.clearHistogram   copy → histogram buffer (256 × uint32, fillBuffer 0)
├─ 5. lmx.pass.exposure.histogram        compute, over the render extent, reads *raw* scene color +
│       the exposure pair → histogram buffer (unaccumulated colour, independent of what it corrects)
├─ 6. lmx.pass.exposure.resolve          compute, 1 thread, reads histogram buffer + the pair
│       → exposure buffer; percentile-trimmed weighted average becomes the metered target, then a
│       bounded step (`adaptUpStopsPerSecond`/`adaptDownStopsPerSecond` × dt) moves `applied` toward
│       it and shifts the old `applied` into `previous` — rate 0 reproduces M6.1's instantaneous
│       result exactly. The resolved pair is what the *next* frame reads — the one-frame lag holds
│
├─ 7. lmx.pass.bloom.threshold        compute, over the output extent, reads the resolved (NativeTaa)
│       or raw (Raw) colour → bloomChain mip 0: 2×2-box prefilter + soft threshold
├─ 8. lmx.pass.bloom.downsample0..N-1  compute, one pass per level, mip L-1 → mip L of bloomChain
│       2×2-box downsample; N = kMaxBloomDownsampleLevels (4), clamped so no mip collapses to 1×1
│       early — a 32-wide bloom base (the common viewport size) reaches the full depth
├─ 9. lmx.pass.bloom.upsampleN-1..0   compute, one pass per level, walks back to mip 0
│       → bloomBlur (a second graph-created texture, one fewer mip than bloomChain): pixel-center
│       bilinear upsample + add. A second texture rather than in-place accumulation, since one pass
│       may read and write one texture only through disjoint ranges (spec 6)
│
├─ 10. lmx.pass.display     fullscreen triangle: Load the resolved colour (Raw reads raw scene
│       colour instead) + bilinearly reconstructed bloomBlur mip 0 × bloomIntensity
│       (bloom-off binds an exact-zero fallback, bit-identical to no bloom) → Khronos PBR Neutral (Shaders/Tonemap.slang, shared with the
│       debug view) → sRGB encode → display color (BGRA8Unorm) — the only pass that encodes sRGB
│
├─ [a debug view selected] 10b. lmx.pass.temporal.debugView   raster, not compute — BGRA8Unorm
│       carries no storage-write usage under this RHI. Declared against the display pass's own
│       not-yet-declared output version; the graph's topological schedule still orders it after.
│       Off / MotionVectors / ReprojectionError (M6.1) plus ReprojectedHistory (tone mapped through
│       the shared module), RejectionMask (flat colours per reason, clipped flag added green),
│       BlendWeight (grey = weight kept), HistoryAge (grey = age / warmup)
│
├─ 11. lmx.pass.ui          → swapchain drawable
│       Dear ImGui (docked editor shell); the Viewport window samples the display color texture;
│       App declares this pass itself and reads the display pass's output to join it
│
└─ graph.execute(commands) → endFrame → present (or endFrame(nullptr) for the offscreen
     --screenshot / test path, which stops at lmx.pass.display and reads that target back)
```

**Raw** (`reconstruction == Raw`) shares every declaration above except step 3 (the raw commit, not
the resolve) and that bloom and display read raw scene colour, not a resolved one. Both modes leave
a real frame in the colour slot, so switching between them derives `HistoryResetReason::None`, not
a reset. **Temporal off** (`SceneView::temporal.enabled == false`) declares exactly M5.5's frame:
no manual-mode seed, no motion/reactive attachments, no reproject/resolve/commit/debug-view
passes, one depth slot imported under its legacy name. Auto-mode reset frames retain the exposure
seed. Fixed-camera output is byte-identical to M5.5's.

**Vendor temporal** (`VendorTemporal`, `--temporal metalfx`) replaces step 3 with two passes:

| Pass | Kind | Reads → writes |
|---|---|---|
| `lmx.pass.temporal.vendor.pack` | compute, 8×8 over active render extent | Motion, reactive, exposure pair → output-capacity `vendorMotion`/`vendorReactive`, 1×1 `vendorExposure` |
| `lmx.pass.temporal.vendor` | external | Scene colour, current depth, packed inputs → current colour-history slot |

Packing preserves ordinary UV motion, maps invalid motion to zero with reactive 1, and writes
`1 / applied` to the R16Float exposure texel. The adapter passes negative render width/height as
motion scales, `jitterTexelOffset(jitterPixels)` as texel jitter, reversed depth and `preExposure=1`.
This cancels engine exposure in MetalFX's working domain; measured output stays pre-exposed for
unchanged bloom/display. The vendor owns private history independently of the engine's slots.
Switching to or from it derives no engine reset; vendor entry, an engine reset or scaler recreation
sets its own reset flag. Unsupported or failed creation falls back to Native TAA with status.
`TemporalStatus` reports effective mode, fallback, vendor name, reset and generation.

Motion and reprojection-error views remain engine-owned. ReprojectedHistory additionally declares
`lmx.pass.temporal.reprojectedHistory` using `VendorTemporalHistory.slang`'s reprojection/exposure
subset without accumulation. Rejection, blend-weight and per-pixel age views are native-only.
Vendor frames retain conservative `ExternalWrite` for current colour, `ExternalRead` for current
depth and scene colour, and `ShaderRead` for the other depth; previous colour retains its access
unless an engine diagnostic samples it. Native terminal-use rows remain unchanged (ADR 0017).

Vertex data is bindless vertex-pulling everywhere: a `StructuredBuffer<VertexPNTU>` at slot b0
(48-byte pos/normal/tangent₄/uv), indices as plain uint32 buffers consumed per draw.

## The render graph

`Source/Render/RenderGraph.h/.cpp` declares raster, compute, copy and external passes over versioned
`GraphTexture`/`GraphBuffer` handles. Imports borrow persistent resources at version 0; transients
belong to one frame (ADR 0008). Each pass declares every read/write and its subresource range.
`compile()` rejects missing producers, duplicate writers, overlapping in-pass read/write ranges,
cycles, invalid attachments/formats, transient reads before writes and invalid sinks/exports.
It returns a serial topological schedule containing only passes reachable from a declared sink:
exports, presentation or readback destinations. Execution validates again, then runs each callback
with `PassResources` restricted to that pass's declared handles. Raster, compute and copy callbacks
run inside labelled RHI scopes; external callbacks open no scope and invoke a timed RHI operation.

RAW, WAR and WAW barrier derivation includes subresources and `ExternalRead`/`ExternalWrite`.
Persistent imports seed the prior frame's terminal access, ordering reuse across command buffers.
The `CompiledFrameRecord` retains schedule, culling, barriers, lifetimes and memory assignment;
`GraphDump.h` prints it deterministically. Scheduling optimization remains limited to dead-pass
culling and conservative transient pooling.

`TransientPool` owns one placement heap per frame slot, recycled only after `beginFrame` proves
that slot retired. Compilation places lifetime-disjoint compatible resources first-fit, emits a
whole-resource barrier on alias reuse, and records high-water bytes and savings. Descriptor
compatibility includes kind, format, extent, mips, usage, size and alignment. Footprint changes may
create a new heap generation. Disabling pooling gives each transient separate bytes without
changing the picture. Histogram and exposure buffers remain persistent imports across frames.

Every pass kind is a GPU timing boundary. `Device::passTimings()` returns labels and milliseconds
for the retired frame named by `passTimingsFrame()`, using Metal counter samples resolved after
shared-event retirement. Performance retains a pausable 60-sample rolling table, refreshed four
times per second; ordered pass-label changes reset it. Render Graph shows the exact newest retired
compiled record with joined timings, including the external pass's card and opaque-operation cost.

Neutral interfaces and capture schema live under `RHI/Include/RHI/`, implementation/validation in
`RHI/Source/`, and the sole backend in `RHI/Backends/Metal4/Source/`. Optional `RHIMetal4ImGui`
submission does not make ImGui a core RHI dependency.

## Resources and lifetime

- **3 frames in flight.** Each in-flight slot owns an argument table, a command allocator, and a
  growable per-slot frame-data page arena (256 KiB normal pages; a request too large for the
  active page gets a new page rounded up to that quantum, never a per-draw GPU object).
  `bindFrameData` is one call: it bump-allocates within the active page at 256-byte alignment (or
  wider, for an over-aligned type), copies the caller's block to that offset, composes the address
  as the page's cached GPU base plus the offset, binds it into the argument table, and returns that
  `GpuAddress` to the caller. `beginFrame` asserts (all builds) that the shared event proves the
  recycled slot's frame retired before that slot's page cursors reset to reuse their prior
  high-water capacity. A 12-frame GPU stress test attributes any cross-frame overwrite to its
  culprit by color.
- **RHI resources join one residency set** attached to the queue; MetalFX manages its own private resources.
- **Renderer-owned targets**: scene color (`RGBA16Float`, scene-linear, cpu-readable on request),
  display color (`BGRA8Unorm`, what the viewport and a screenshot read), `lmx.render.motion`
  (`RG16Float`), `lmx.render.reactive` (`R8Unorm`), and two ping-ponged pairs — colour history
  (`lmx.render.historyColor0/1`, `RGBA16Float`) and depth (`lmx.render.sceneDepth0/1`, `D32Float`)
  — all resize with the Viewport panel (debounced, GPU-drained); the shadow map is fixed at 2048².
  A temporal frame renders depth and writes colour into slot `m_temporalFrame % 2` (the resolve
  directly under `NativeTaa` or `VendorTemporal`, the commit copy under `Raw`), so every temporal frame's colour slot
  holds that frame's output. Both pairs are created with the rest, not on first enable, so their
  cost is permanent and reported by `TemporalStatus::historyBytes`/`depthHistoryBytes`. The
  histogram buffer and the two-float exposure buffer (`{applied, previous}`) are fixed-size and
  persistent — a resize is one of spec 9's reset triggers, since the histogram's binning covered a
  differently-sized image the frame before; every temporal target allocates at the output extent
  regardless of render scale (ADR 0016), so a scale change reallocates nothing.
- **Vendor scaler**: lazy, output-extent-owned, recreated only on output resize. Packed textures
  are frame transients. Each encoded frame slot retains scaler state until retirement. A public
  fence bridges MetalFX's opaque encoders; CPU-readable outputs use creation-time private scratch
  and an in-call copy, included in external timing. Private outputs are written directly.
- **Scenes are cached for the device's lifetime** (`SceneLibrary`): meshes, materials, textures,
  and the scene's IBL set build once on first selection. Sponza and Damaged Helmet upload only
  material-referenced images; decoded CPU image data is dropped before the builder returns.
- **IBL assets** (`Source/Engine/Ibl.h`): per-scene diffuse irradiance (16² cube), filtered
  specular (64², or 128² for MaterialLab, five mips), and a 64² DFG LUT. Filtering crosses cube
  faces and chooses source mips by GGX footprint. MaterialLab bounds diffuse work with a separate
  32² source. RGBA16Float/RG16Float preserve HDR; absent inputs bind black-cube/zero-DFG fallbacks.
- **Texture mip baking** (`Source/Engine/TextureBake.h`): setup writes deterministic DDS mip chains
  and manifests. Colour filters in linear light; normal maps renormalize. Scene loading prefers
  baked assets and falls back to the same in-process filter. Metal's removed mip generator was
  measured to point-pick.

## The math, briefly

- **GGX metallic-roughness BRDF** (`Shaders/Lighting.slang`): Trowbridge-Reitz D,
  height-correlated Smith visibility, Schlick F (`F0 = mix(0.04, baseColor, metallic)`) and
  energy-conserving Lambert diffuse. Perceptual roughness floors at 0.045 before squaring.
- **Image-based lighting**: the split-sum reconstruction (Karis 2013) plus Fdez-Agüera's
  multiple-scattering compensation, so a white furnace returns its own radiance at every roughness
  and metallic value rather than losing energy to single-scattering loss as roughness rises.
  Occlusion attenuates the image-based terms only — the shadow map already answers direct-light
  visibility, and applying occlusion to both would darken a lit surface twice.
- **Exposure and display**: every fragment multiplies its linear output by `preExposure` before the
  scene target sees it, so the scene color holds pre-exposed scene-linear radiance. Manual mode (the
  default) computes `preExposure = exp2(EV)` from the Render Settings slider on the CPU. Auto mode
  reads a persistent GPU exposure buffer — `{applied, previous}` — that a one-frame histogram
  feedback loop maintains (spec 9/7, diagrammed above): the seed shifts and sets the pair on every
  manual temporal frame and every auto reset frame, and the resolve moves `applied` toward the
  metered target at a bounded stops-per-second rate (independent up/down rates), shifting the old
  value into `previous`. The pair is the single source of applied exposure the scene pass, the
  histogram and the temporal resolve all read the same version of; the resolve multiplies its
  fetched history by `exposure[0] / exposure[1]` before clipping and blending it, so a manual EV
  edit or an adaptation step never blends two frames recorded at different brightness uncorrected.
  `Shaders/DisplayTransform.slang` is the frame's one display boundary: bloom composites in first
  (pre-exposed luminance), then Khronos PBR Neutral (`Shaders/Tonemap.slang`, shared with the
  temporal debug view), then the sRGB encode. The editor's clear color is authored in display space
  and decoded-then-pre-exposed once, at pass declaration, always at the *manual* exposure value even
  in auto mode, since auto's value lives only in the GPU-side buffer and every scene with a sky
  draws over the clear entirely.
- **Exposure reset mapping.** The temporal history and the exposure buffer each still choose their
  own reset policy: exposure clears on `SceneChanged`, `ExtentChanged`, and the auto-exposure enable
  transition (spec 9's triggers, unchanged), and ignores `CameraCut`/`ProjectionChanged`, which the
  temporal history treats as resets of its own. `App/ExposureReset.h` is untouched: unifying the two
  events was considered and rejected for M6.2, since `sceneGeneration` bumps on any content change
  and the blend's own exposure correction already absorbs a real exposure reset's discontinuity.
- **Reversed infinite-far depth**: `Camera::projectionMatrix` maps the near plane to 1 and lets
  depth fall toward 0 without ever reaching it, concentrating float precision at the far plane
  instead of the near one. Scene and shadow pipelines clear to 0 and keep the `Greater` fragment;
  the sky pins every vertex to depth 0 and passes `GreaterEqual`; `fitShadowOrtho` reverses to
  match, and the shadow comparison sampler is `GreaterEqual`. View-space depth reconstructs from a
  sampled texel as `viewZ = -nearZ / d`.
- **Shadows**: light 0 casts through an ortho fit to the scene bounds; 25-tap Poisson PCF or
  PCSS is selectable. Reversed-Z bias is `{-4.0, -32.0}`. PCSS retains its view/NDC unit mismatch.

## Scenes

The Scene panel and `--scene` share five catalog IDs: **Sponza** (`sponza`, default, converted
Crytek OBJ), **Damaged Helmet** (`damaged-helmet`, glTF), **CesiumMilkTruck** (`milk-truck`, rigid
animation), **MaterialLab** (`material-lab`, procedural materials and image diagnostics), and
**TemporalLab** (`temporal-lab`, checker floor, rigid/orbiting motion, emissive and invalid-motion
objects). The two procedural labs are always available.
MaterialLab uses the pinned CC0 Studio Small 09 HDRI for its visible sky and generated IBL when
setup has fetched it, and logs before falling back to a deterministic neutral environment otherwise.
The other scenes retain the shared code-generated neutral cubemap and IBL set. Missing required
glTF assets disable their dropdown entries with setup guidance; an unavailable explicit CLI scene
exits with an error instead of falling back.

## Known limits

PCSS retains its view/NDC blocker-search mismatch. IBL rebuilds on scene load; direct lighting
remains single-scatter. Baked-DDS selection keys on image index rather than colour/data role.
Sponza startup is synchronous, and Metal is the only backend. Future scope belongs to the roadmap.

Cross-references: [render graph](decisions/0005-render-graph.md),
[scene-linear image formation](decisions/0006-scene-linear-image-formation.md),
[vendor reconstruction](decisions/0017-vendor-reconstruction-capability.md), and `Shaders/` for
scene shading, exposure, temporal reconstruction, bloom and the display boundary.
