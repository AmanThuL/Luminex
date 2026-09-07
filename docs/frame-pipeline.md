# Luminex — one frame, as of the M6.2 native TAA and exposure stability path (2026-09-08)

What the renderer does between `beginFrame` and `endFrame`, written for planning what to build
next. Updated at milestone boundaries.

## The frame at a glance

A `RenderGraph` is declared fresh every frame: it imports the renderer's own targets, the
persistent histogram and exposure buffers, and the swapchain drawable; the shadow, scene+sky,
exposure-feedback, temporal, bloom, and display passes declare their reads, attachments, and writes
over them; `compile()` proves the declarations form a DAG and answers a serial schedule (with
dead-pass culling) before any of it reaches the GPU. `execute()` then runs that schedule, deriving
the RAW, WAR, and WAW barriers each declared cross-pass access conflict justifies.

Below is the *default* frame — manual exposure, bloom on, temporal on with `NativeTaa`
(`SceneView::temporal.enabled == true`, `reconstruction == NativeTaa`, the default since M6.2).
Auto-exposure, bloom, and temporal are all ordinary declared passes either way; toggling one off
removes only the declaration reaching a sink, and dead-pass culling drops the rest
(`docs/guides/gpu-debugging.md`'s dump shows exactly this for a toggled-off frame).

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
│       │    uniforms (b1): mvp, model, normalMatrix, uvTransform, albedo, roughness, metallic,
│       │    emissive, flags; textures t0 base color / t1 normal / t4 metallic-roughness /
│       │    t5 occlusion / t6 emissive; per-pass shared t3 shadow map, t7 irradiance,
│       │    t8 prefiltered environment, t9 DFG LUT
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
├─ [reset reason None] 2b. lmx.pass.temporal.reproject   compute, diagnostic only, reads the
│       previous colour slot + scene colour + motion → temporalDiagnostic; culled unless
│       ReprojectionError sinks it
│
├─ 3. NativeTaa: lmx.pass.temporal.resolve   compute, 8×8, reads scene colour, both depth slots,
│       motion, reactive, the previous colour slot, the exposure pair → this frame's colour slot
│       directly (no copy: this frame's output *is* the next frame's history) and, only when sunk,
│       the rejection/reprojected transients. Dilated motion/disocclusion, history exposure
│       correction, YCoCg clipping, an inverse-luminance blend saturating to 1 on rejection or full
│       reactive weight (`Shaders/TemporalResolve.slang`, ADR 0014). Raw: lmx.pass.temporal.
│       commitHistory (copy, M6.1's pass) writes the raw scene colour into this frame's colour slot
│       instead, right here — not under NativeTaa, whose resolve already wrote it
│
├─ 4. lmx.pass.exposure.clearHistogram   copy → histogram buffer (256 × uint32, fillBuffer 0)
├─ 5. lmx.pass.exposure.histogram        compute, reads *raw* scene color + the exposure pair →
│       histogram buffer; metering the unaccumulated colour keeps it independent of what it corrects
├─ 6. lmx.pass.exposure.resolve          compute, 1 thread, reads histogram buffer + the pair
│       → exposure buffer; percentile-trimmed weighted average becomes the metered target, then a
│       bounded step (`adaptUpStopsPerSecond`/`adaptDownStopsPerSecond` × dt) moves `applied` toward
│       it and shifts the old `applied` into `previous` — rate 0 reproduces M6.1's instantaneous
│       result exactly. The resolved pair is what the *next* frame reads — the one-frame lag holds
│
├─ 7. lmx.pass.bloom.threshold        compute, reads the resolved (NativeTaa) or raw (Raw) colour
│       → bloomChain mip 0: 2×2-box prefilter + soft threshold on pre-exposed luminance
├─ 8. lmx.pass.bloom.downsample0..N-1  compute, one pass per level, mip L-1 → mip L of bloomChain
│       2×2-box downsample; N = kMaxBloomDownsampleLevels (4), clamped so no mip collapses to 1×1
│       early — a 32-wide bloom base (the common viewport size) reaches the full depth
├─ 9. lmx.pass.bloom.upsampleN-1..0   compute, one pass per level, walks back to mip 0
│       → bloomBlur (a second graph-created texture, one fewer mip than bloomChain): nearest 2×
│       upsample + add. A second texture rather than in-place accumulation, since one compute pass
│       may read and write one texture only through disjoint ranges (spec 6)
│
├─ 10. lmx.pass.display     fullscreen triangle: Load the resolved colour (Raw reads raw scene
│       colour instead) + bloomBlur mip 0 × bloomIntensity (bloom-off binds an exact-zero fallback,
│       bit-identical to no bloom) → Khronos PBR Neutral (Shaders/Tonemap.slang, shared with the
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

Vertex data is bindless vertex-pulling everywhere: a `StructuredBuffer<VertexPNTU>` at slot b0
(48-byte pos/normal/tangent₄/uv), indices as plain uint32 buffers consumed per draw.

## The render graph

`Source/Render/RenderGraph.h/.cpp` models a frame's passes as declarations over versioned logical
handles (`GraphTexture`/`GraphBuffer`) rather than as commands. A resource is either **imported** —
`importTexture`/`importBuffer` bring in a texture or buffer the caller already owns, at version 0 —
or **transient**: `createTexture`/`createBuffer` declare one the graph owns for exactly one frame
(`docs/decisions/0008-transient-graph-resources.md`). A pass carries a kind (raster, compute, copy)
and names every version it reads or writes, with per-subresource ranges where it matters (bloom's
downsample/upsample steps read one mip and write another of one texture); `compile()` hard-fails a
read of a version no pass wrote, two passes writing one version, overlapping read/write ranges in
one pass, a cycle, an attachment/format mismatch, a transient consumed before its first write, a
sink naming a transient, or an export of a version nothing produced, and otherwise answers one
serial topological order holding only the passes a declared sink (`exportTexture`, `exportBuffer`,
swapchain presentation, or a readback destination) reaches. `execute()` re-validates, then runs that
schedule: each pass becomes one labelled render, compute, or copy pass, its body runs inside that scope
with a `PassResources` that resolves only the handles the pass declared — an undeclared resolve is a
reported failure, not a resolved pointer. Barrier derivation covers RAW, WAR, and WAW conflicts,
including per-subresource texture writers. Persistent-import overloads seed the prior frame's terminal
texture or buffer access, so reused renderer targets and exposure feedback are ordered across command
buffers as well. Compilation answers with a `CompiledFrameRecord` describing everything it decided, which
`Render/GraphDump.h` renders as deterministic text. A graph is declared fresh every frame; scheduling
optimization beyond dead-pass culling and conservative transient pooling stays deliberately absent.

Transients are placed in a `Render/TransientPool`: one placement heap per frame-in-flight slot, reused
only after `Device::beginFrame()` has proved that slot's previous frame retired, and resized into a new
generation when a frame's footprint changes. Compilation assigns offsets first-fit over lifetime-disjoint
transients whose descriptors agree on kind, format, extent, mip count, usage, size, and alignment, emits
a whole-resource barrier wherever one transient takes bytes another held, and records every lifetime,
assignment, the heap high-water mark, and the alias savings. `RenderGraph::setPoolingEnabled(false)` —
the editor's Transient pooling checkbox — gives every transient its own bytes and cannot change the
picture, because a transient holds nothing until a pass writes it. The histogram and exposure buffers are
imported, not transient: both must outlive the frame that wrote them (the exposure buffer for a full
frame, into the next one's shading).

Every pass -- render or compute -- is also a GPU timing boundary: `rhi::Device::passTimings()`
reports each pass's label and GPU milliseconds for the most recently retired frame, and
`passTimingsFrame()` names that frame. Both are populated by counter samples the Metal 4 backend
takes at pass begin/end and resolved once the shared event proves that frame retired. The editor's
Performance panel retains up to 60 samples per schedule position and publishes a pausable rolling
Pass/Average/Latest/Min-Max/Samples table four times per second; a changed ordered pass-label
sequence resets the window. The Render Graph panel instead shows the exact newest retired frame's
full declaration, schedule, culling, barriers, and joined timing values.

The backend-neutral interfaces and capture schema are public headers under `RHI/Include/RHI/`.
Their implementation and validation live in `RHI/Source/`; the only backend lives in
`RHI/Backends/Metal4/Source/`. Dear ImGui submission is an application-facing adapter in the
optional `RHIMetal4ImGui` target, so it does not make ImGui part of the core RHI dependency surface.

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
- **Everything lives in one residency set** attached to the queue; textures join at creation.
- **Renderer-owned targets**: scene color (`RGBA16Float`, scene-linear, cpu-readable on request),
  display color (`BGRA8Unorm`, what the viewport and a screenshot read), `lmx.render.motion`
  (`RG16Float`), `lmx.render.reactive` (`R8Unorm`), and two ping-ponged pairs — colour history
  (`lmx.render.historyColor0/1`, `RGBA16Float`) and depth (`lmx.render.sceneDepth0/1`, `D32Float`)
  — all resize with the Viewport panel (debounced, GPU-drained); the shadow map is fixed at 2048².
  A temporal frame renders depth and writes colour into slot `m_temporalFrame % 2` (the resolve
  directly under `NativeTaa`, the commit copy under `Raw`), so every temporal frame's colour slot
  holds that frame's output. Both pairs are created with the rest, not on first enable, so their
  cost is permanent and reported by `TemporalStatus::historyBytes`/`depthHistoryBytes`. The
  histogram buffer and the two-float exposure buffer (`{applied, previous}`) are fixed-size and
  persistent — a resize is one of spec 9's reset triggers, since the histogram's binning covered a
  differently-sized image the frame before.
- **Scenes are cached for the device's lifetime** (`SceneLibrary`): meshes, materials, textures,
  and the scene's IBL set build once on first selection. Sponza and Damaged Helmet upload only
  material-referenced images; decoded CPU image data is dropped before the builder returns.
- **IBL assets are per-scene and generated at build time** (`Source/Engine/Ibl.h`): a cosine-convolved
  irradiance cube (16² faces), a GGX-prefiltered specular chain (64² base, 5 mips), and a split-sum DFG
  lookup table (64², `RG16Float`), all uploaded `RGBA16Float`/`RG16Float` so radiance above 1.0 survives.
  A `SceneView` that carries none substitutes black-cube and zero-DFG fallbacks rather than reading an
  unbound slot.
- **Base-color and normal images bake offline when `xmake setup` runs**: `Tools/TextureBake` (wrapping
  `Source/Engine/TextureBake.h`) box-filters a full mip chain in linear light (sRGB images
  decode/filter/re-encode; normal maps renormalize per level) and writes a DDS plus a manifest recording
  the source hash. `Scene` prefers the baked DDS beside a glTF file and falls back to the same filter
  computed in-process (slower load, not incorrect) when it is absent. `Device::generateMipmaps` no longer
  exists — Metal's blit variant was measured to point-pick.

## The math, briefly

- **GGX metallic-roughness BRDF** (`Shaders/Lighting.slang`): Trowbridge-Reitz `D`, height-correlated
  Smith `V` (combined `G / (4 N·V N·L)` form), Schlick `F` with `F0 = mix(0.04, baseColor, metallic)`,
  energy-conserving Lambert diffuse `(1 - F)(1 - metallic) baseColor / π`. Perceptual roughness is
  floored at 0.045 before squaring to `alpha`, bounding the specular lobe the raster grid can resolve.
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
- **Shadows**: unchanged from M3's PCF/PCSS mechanics — one directional caster (light 0), an
  ortho frustum fit to the scene's bounding sphere, 25-tap Poisson-disk PCF or PCSS (blocker
  search → penumbra → variable PCF) as a runtime toggle. The depth bias sign flipped with the
  reversed convention (now `{-4.0, -32.0}`); PCSS's own view/NDC unit mismatch is preserved rather
  than fixed here.

## Scenes

Five, behind the Scene panel's catalog selector, drawn from one catalog (`--scene` accepts the same
IDs): **Sponza** (`sponza`, the default world scene, converted deterministically from the official
Crytek OBJ+PNG archive), **Damaged Helmet** (`damaged-helmet`, glTF, generated tangents),
**CesiumMilkTruck** (`milk-truck`, glTF, a fetched rigid-animation sample), **MaterialLab**
(`material-lab`, always available — a code-generated roughness×metallic sphere grid plus
horizontally arranged color, texture, normal, and depth diagnostics), and **TemporalLab**
(`temporal-lab`, always available — a deterministic checkerboard floor, rigid and orbiting motion,
and one `Invalid`-flagged object for verifying the M6.1 motion contract). MaterialLab and
TemporalLab use the pinned CC0 Studio Small 09 HDRI for their visible sky and generated IBL when
setup has fetched it, and log before falling back to a deterministic neutral environment otherwise.
The other scenes retain the shared code-generated neutral cubemap and IBL set. Missing required
glTF assets disable their dropdown entries with setup guidance; an unavailable explicit CLI scene
exits with an error instead of falling back.

## Known gaps / candidate techniques for the next milestone

1. **Bloom's upsample is nearest-neighbour** — a deterministic, testable choice (spec 10), but it
   produces a visibly blocky halo around small bright highlights at the current chain depth;
   bilinear or a wider filter kernel is a candidate improvement, not a correctness fix.
2. **PCSS parameterization** — the migrated blocker search mixes a view-space near-plane constant
   with NDC-space receiver depth, a preserved unit bug; fixing it is cheap and deferred.
3. **IBL regeneration cost** — each scene's irradiance/prefiltered/DFG set regenerates on load, and
   direct lighting stays single-scatter (only the image-based term compensates); caching becomes
   worthwhile as more authored environments or faster scene switching arrive.
4. **Baked-DDS selection keys on image index alone**, not on how a material uses that image; a
   glTF file that reused one image in both a color and a data role would need the offline bake to
   distinguish them, which it does not yet do.
5. **Sponza startup is still synchronous** — decode and upload still block the window before it
   becomes responsive; asynchronous staging remains future work.
6. **Portability** — Metal remains the only backend; the reversed-Z, HDR, graph, and barrier
   conventions above are what a future D3D12 backend has to reproduce.

Cross-references: `docs/specs/2026-08-11-m5-execution-substrate-design.md` (sections 6/7/9/10 —
subresource model, graph semantics, exposure feedback, features), `docs/decisions/0005-render-graph.md`,
`docs/decisions/0006-scene-linear-image-formation.md`, and `Shaders/` (ScenePass.slang and
Lighting.slang are the frame's shading core, HistogramAccumulate/ExposureResolve.slang the exposure
chain, BloomThreshold/Downsample/Upsample.slang the bloom chain, DisplayTransform.slang its display
boundary).
