# Luminex — one frame with visibility, local lighting and reconstruction (2026-09-18)

Native TAA, CPU culling, indirect submission and Clustered local lighting are defaults; vendor reconstruction shares temporal inputs. The [local-light default decision](milestones/m7.5-validation.md#default-decision) follows the passed lossless-list and scoped exact-image gates; Direct remains the reference.
## The frame at a glance

A fresh `RenderGraph` imports targets, five geometry/material scene buffers (plus lights when live), two submission buffers, persistent histogram/exposure buffers and the drawable. Renderer composes the stages below; graph compilation
validates the DAG, culls dead passes and derives RAW/WAR/WAW barriers before serial execution.
Opt-in [GPU visibility](guides/gpu-visibility.md) adds reset/classify/scan/emit before draws.
Occlusion reads the preceding declared frame's HZB using that frame's matrix, jitter and active extent.
After temporal consumers, current depth builds half-resolution R32Float minima, one pass per mip;
publication reads all mips. Two output-capacity pyramids alternate independently of temporal slots.
Invalid source/camera/coverage history or wireframe retains globally; shadows remain unculled.
Independent direct ID/depth checks copy into paced readback and join by frame and instance identity.

`App/Model/SceneSession` owns shared playback, views and motion; `prepareFrame` updates the retired scene-table slot after `beginFrame`.
`Render/FrameDeclaration` rotates the pool, declares/executes passes and returns App's retained record. Editor adds UI/present then platform windows; headless exports display and waits per frame.
Screenshots start at zero and sequences sample frame/60 with warmup. Editor loads Stopped; top-toolbar Scene Play/Step advances fixed steps after drawable acquisition, Pause stops advancement.
First Play captures camera/time and animation-owned object poses/emissive strength and tracked light positions; Stop or scene switch restores them and resets motion/temporal/exposure. Rendering settings and unrelated edits remain outside this shared-scene preview restoration.
Toolbar Measure Play runs deterministic W/N with Pause disabled and opens/focuses detached Performance once; closing it leaves the run active. Stop/completion restores preview state, retains results and does not reopen it. CLI scheduling is unchanged; see [playback](guides/gpu-debugging.md#editor-playback).

Below is the *default* frame — manual exposure, bloom on, temporal on with `NativeTaa` (`SceneView::temporal.enabled == true`, `reconstruction == NativeTaa`, the default since M6.2), at
`renderScale == 1.0` (below 1.0 the scene pass's render area shrinks; see Resources and lifetime). Auto-exposure, bloom, and temporal are ordinary
declared passes either way; toggling one off removes only the declaration reaching a sink.

```
beginFrame (blocks until frame N-3 retired; shared-event pacing, arena page-cursor recycle invariant asserted)
├─ SceneSession.prepareFrame(N): recompute bounds/coverageEpoch and write rows dirty for retired slot N % 3
├─ CPU visibility: jittered five-plane oracle; prepare scene/shadow row lists and draw arguments
├─ declare: import scene vertices/indices/meshes/instances/materials, shadow map, scene color,
│           both depth/colour history slots, display, histogram, exposure {applied, previous}, drawable
├─ [Clustered + live local lights] lmx.pass.light.reset/count/scan/fill
│       reset counters; count raw sphere/froxel hits; scan caps/prefixes; fill ascending row indices
│       graph barriers order light-table reads, count/scan/fill writes and scene grid/index reads
├─ 0. lmx.pass.exposure.seed   compute, 1 thread → exposure buffer
│       shift-and-set: `previous = applied; applied = exp2(manualEV)`, always the CPU-computed
│       manual exposure, whichever mode is running — including on an auto reset frame, where it is
│       what restarts the loop from the manual value (spec 9). Manual mode: declared every temporal
│       frame. Auto mode: only on a reset trigger
├─ 1. lmx.pass.shadow      depth-only → shadow map (2048², D32Float, store)
│       unculled opaque/masked draws: firstEntry b1, lightViewProj b2, rows b4, instances/materials b5/b6
│       depth bias {-4.0, -32.0} (reversed-Z), light 0 only
│       reversed depth: clears to 0, Greater compare, comparison sampler GreaterEqual
├─ 2. lmx.pass.scene       → scene color (RGBA16Float), lmx.render.motion (RG16Float, extra 0),
│    │  lmx.render.reactive (R8Unorm, extra 1, cleared to 0) + this frame's depth slot, viewport-sized
│       │  per-pass uniforms (b2): viewProj, shadowTransform, eye, time, preExposure,
│       │  3 directional lights, shadow filter
│       ├─ opaque/masked DrawItems: GGX metallic-roughness BRDF (direct lights) + diffuse/specular IBL
│       │    (split-sum reconstruction with Fdez-Agüera multi-scatter compensation) + shadow
│       │    factor (25-tap Poisson PCF or PCSS) + normal mapping (TBN) + occlusion (image-based
│       │    terms only) + material emissive × instance emissiveScale, summed and pre-exposed
│       │    b1 firstEntry + instanceIndex selects b4 row, then transforms b5 and material factors b6;
│       │    shared vertices b0, per-draw material textures and shared shadow/IBL textures
│       │    local lights b8, cluster grid b9, indices b10, copied LocalLightParams b11 (136 B)
│       │    one Off/Direct/Clustered loop adds point/spot terms before ambient/emissive and exposure
│       ├─ reads the shadow map's written version — the graph derives the barrier from that
│       │    declaration
│       ├─ auto-exposure only: binds ScenePassAuto.slang/SkyAuto.slang's pipelines instead of
│       │    ScenePass.slang/Sky.slang's, reading the exposure buffer directly in place of the
│       │    manual `preExposure` uniform; the exposure twins retain separate compiled pipelines
│       ├─ motion: `uvCurrent - uvPrevious` from unjittered clip positions (Temporal.h,
│       │    Motion.slang); an Invalid-class draw writes the +inf sentinel instead. Reactive:
│       │    `saturate(luminance(emissiveTerm · preExposure) / kReactiveEmissiveScale)`
│       └─ sky, drawn last: camera-centered sphere pinned to the reversed far plane (depth 0),
│            cull none, GreaterEqual, t2 cubemap, the same preExposure, motion from the previous
│            eye's rotation alone, reactive 0
├─ [reset reason None] 2b. lmx.pass.temporal.reproject   compute, diagnostic only, over the output
│       extent, reads the previous colour slot + scene colour + motion → temporalDiagnostic; culled
│       unless ReprojectionError sinks it
├─ 3. NativeTaa when render == output and (this frame resets or previous extents match):
│       lmx.pass.temporal.resolve   compute, 8×8, reads scene colour, both depth slots, motion,
│       reactive, the previous colour slot, the exposure pair → this frame's colour slot directly
│       (no copy) and, only when sunk, the rejection/reprojected transients. Dilated motion/
│       disocclusion, history exposure correction, YCoCg clipping, an inverse-luminance blend
│       saturating to 1 on rejection or full reactive weight (`Shaders/TemporalResolve.slang`,
│       ADR 0014); any other frame runs lmx.pass.temporal.upscale instead (`Shaders/
│       TemporalUpscale.slang`, ADR 0016). Raw: commitHistory or commitUpscaled writes this frame's
│       colour slot instead, right here
├─ 4. lmx.pass.exposure.clearHistogram   copy → histogram buffer (256 × uint32, fillBuffer 0)
├─ 5. lmx.pass.exposure.histogram        compute, over the render extent, reads *raw* scene color +
│       the exposure pair → histogram buffer (unaccumulated colour, independent of what it corrects)
├─ 6. lmx.pass.exposure.resolve          compute, 1 thread, reads histogram buffer + the pair
│       → exposure buffer; percentile-trimmed weighted average becomes the metered target, then a
│       bounded step (`adaptUpStopsPerSecond`/`adaptDownStopsPerSecond` × dt) moves `applied` toward
│       it and shifts the old `applied` into `previous` — rate 0 reproduces M6.1's instantaneous
│       result exactly. The resolved pair is what the *next* frame reads — the one-frame lag holds
├─ 7. lmx.pass.bloom.threshold        compute, over the output extent, reads the resolved (NativeTaa)
│       or raw (Raw) colour → bloomChain mip 0: 2×2-box prefilter + soft threshold
├─ 8. lmx.pass.bloom.downsample0..N-1  compute, one pass per level, mip L-1 → mip L of bloomChain
│       2×2-box downsample; N = kMaxBloomDownsampleLevels (4), clamped so no mip collapses to 1×1
│       early — a 32-wide bloom base (the common viewport size) reaches the full depth
├─ 9. lmx.pass.bloom.upsampleN-1..0   compute, one pass per level, walks back to mip 0
│       → bloomBlur (a second graph-created texture, one fewer mip than bloomChain): pixel-center
│       bilinear upsample + add. A second texture rather than in-place accumulation, since one pass
│       may read and write one texture only through disjoint ranges (spec 6)
├─ 10. lmx.pass.display     fullscreen triangle: Load the resolved colour (Raw reads raw scene
│       colour instead) + bilinearly reconstructed bloomBlur mip 0 × bloomIntensity
│       (bloom-off binds an exact-zero fallback, bit-identical to no bloom) → Khronos PBR Neutral (Shaders/Modules/Tonemap.slang, shared with the
│       debug view) → sRGB encode → display color (BGRA8Unorm), described by
│       Render/DisplayDomain.h: opaque 8-bit SDR, BT.709/D65, reference and peak white 1.0
├─ [a debug view selected] 10b. lmx.pass.temporal.debugView   raster, not compute — BGRA8Unorm
│       carries no storage-write usage under this RHI. Declared against the display pass's own
│       not-yet-declared output version; the graph's topological schedule still orders it after.
│       Off / MotionVectors / ReprojectionError (M6.1) plus ReprojectedHistory (tone mapped through
│       the shared module), RejectionMask (flat colours per reason, clipped flag added green),
│       BlendWeight (grey = weight kept), HistoryAge (grey = age / warmup)
├─ [Clustered + live lights + selected view] lmx.pass.light.debug
│       actual depth + light rows/grid/indices + SDR source transient → BGRA8 display target
│       count/overflow/missed; no scene/history writes, temporal/HZB views are mutually exclusive
├─ 11. lmx.pass.ui          → swapchain drawable
│       Dear ImGui: display-referred sRGB, straight-alpha blending in encoded space, SDR white;
│       the Viewport samples display color at 1:1 backing pixels once resize settles. App
│       declares this pass and reads the display output to join it; detached windows stay SDR.
└─ graph.execute(commands) → endFrame → present (or endFrame(nullptr) for the offscreen
     --screenshot / test path, which stops at lmx.pass.display and reads that target back)
```

**Raw** (`reconstruction == Raw`) shares every declaration above except step 3 (the raw commit, not
the resolve) and that bloom and display read raw scene colour, not a resolved one. Both modes leave
a real frame in the colour slot, so switching between them derives `HistoryResetReason::None`, not
a reset. **Temporal off** (`SceneView::temporal.enabled == false`) omits the manual seed, motion/reactive attachments and temporal passes, using one legacy-named depth slot. Auto reset
frames retain the exposure seed. Visibility and submission modes apply equally with temporal off.

**Vendor temporal** (`VendorTemporal`, `--temporal metalfx`) replaces step 3 with two passes:

| Pass | Kind | Reads → writes |
|---|---|---|
| `lmx.pass.temporal.vendor.pack` | compute, 8×8 over active render extent | Motion, reactive, exposure pair → output-capacity `vendorMotion`/`vendorReactive`, 1×1 `vendorExposure` |
| `lmx.pass.temporal.vendor` | external | Scene colour, current depth, packed inputs → current colour-history slot |

Packing maps invalid motion to zero/reactive one and writes `1 / applied` to the R16Float exposure
texel. The adapter passes negative render extents as motion scales, texel jitter, reversed depth
and `preExposure=1`; bloom/display retain pre-exposed output. Vendor history is independent of
engine slots: entry, engine reset or recreation resets the vendor, while switching modes leaves
engine history valid. Unsupported or failed creation falls back to Native TAA with explicit status.
`TemporalStatus` reports effective mode, fallback, vendor name, reset and generation. Inspector
separately retains the last reset event and live retired-frame timing, independent of panel freeze.

Motion/reprojection diagnostics stay engine-owned; ReprojectedHistory adds the vendor reprojection/exposure subset without accumulation. Rejection, blend-weight and per-pixel age are
native-only. Vendor frames record `ExternalWrite` for current colour, `ExternalRead` for depth/scene
colour and `ShaderRead` for other depth; previous colour changes only if sampled (ADR 0017).

Vertex pulling uses 48-byte vertices at b0 and the scene's rebased uint32 index pool (base vertex 0).
Shared CPU/Slang rows are `InstanceRow` 240 bytes, `MaterialRow` 112 and `MeshRow` 48. Instances hold current/previous/normal matrices, identity selectors, motion/bounds flags, emissive scale and
world bounds; meshes hold ranges and local bounds. `Scene::meshBounds` also feeds selection framing.
The CPU oracle reads uploaded rows: five jittered VP planes, 1e-3 world margin, no far plane. Unreliable/nonfinite inputs bypass; rejected table rows retain identity and motion.
Renderer owns three paced `DrawSubmission` pairs, retired on growth at last prepared frame + 3. Scene rows precede unculled shadows; `lmx.draw.rows`/`lmx.draw.args` expose graph reads.
Scene/shadow shaders select `gVisibleRows[gDraw.firstEntry + instanceIndex]` at b4, then b5/b6.
Direct binds a 16-byte firstEntry selector per object; indirect binds zero once and uses firstInstance
in each 20-byte argument. Batched stably sorts pipeline/material/mesh keys and draws one run per
command; fragments retain a flat instance row. Sky remains separate. Counts report issued commands.

Masked materials select `ScenePassMask`/`ScenePassAutoMask` and `ShadowPassMask` (ADR 0018).
Shared `AlphaMask` discards texture alpha × factor alpha below cutoff, using the same UV transform;
scene color/depth/motion/reactive share coverage. Two-sided variants reverse back-face shading
normals. Cutoff and flags come from the shared material row; the old alpha-mask uniform block is
retired. Opaque/masked pipelines stay separate; ordinary alpha mips can thin distant foliage.

The 16×9×24 light grid uses active render pixels and the raster's jittered projection, with
near-to-0.3 m, exponential-to-100 m and open-last slices. Count preserves raw hits; scan caps at
128 lights per froxel and 65,536 global indices. Fill keeps lowest row indices in order, with bit
31 in count marking truncation. Safe-math predicates match `LightClusters`' ordered fp32 mirror.
With zero live lights there is no light-table import or light-list/debug pass; all scene b8–b11
bindings still resolve through immutable fallbacks. Direct uses no list passes. Retired counters
join declaration frame/mode, and `--light-check` compares captured lists with that frame's CPU
mirror; `drainLightingAfterIdle` resolves the final in-flight frames before headless completion.
Stage callbacks borrow inputs through graph execution; Renderer retains frame targets and ordering.

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
`CompiledFrameRecord.h` owns the value-only record and debug vocabulary, separate from the builder.
The `CompiledFrameRecord` retains schedule, culling, barriers, lifetimes and memory assignment;
`GraphDump.h` prints it deterministically. Scheduling optimization remains limited to dead-pass culling and conservative transient pooling.

`TransientPool` owns one placement heap per frame slot, recycled only after `beginFrame` proves
that slot retired. Compilation places lifetime-disjoint compatible resources first-fit, emits a whole-resource barrier on alias reuse, and records high-water bytes and savings. Descriptor
compatibility includes kind, format, extent, mips, usage, size and alignment. Footprint changes may
create a new heap generation. Disabling pooling gives each transient separate bytes without changing the picture. Histogram and exposure buffers remain persistent imports across frames.

Every pass kind is timed. `Device::passTimings()` publishes the retired frame identified by
`passTimingsFrame()`. App joins its declaration-time counts/extents/context with those timings.
Performance shows a coherent 60-retired-frame rolling snapshot at 4 Hz; freeze/clear/resume are
independent of playback. Timed-pass sum excludes presentation, driver and untimed GPU work. Render Graph publishes one owned record and exactly matched timings at 4 Hz; first data/Resume
publish immediately, later topology changes wait. Freeze latches labels, details and Dump together.
Physical temporal-resource alternation preserves unchanged canvas identity and navigation.

The editor may append `lmx.pass.selection.coverage`, `lmx.pass.selection.visibility` and `lmx.pass.selection.outline` after the
scene display declaration. App owns this opt-in use of Render's `SelectionOutline` utility:
selected-only full-resolution unjittered depth/coverage retains a non-rejected object silhouette
(including masked cutouts), while a separate unjittered scene-depth pass resolves occluders.
The composite depth-tests both border source and destination against scene visibility, avoiding
false edges from foreground cuts and expansion onto foreground surfaces. A separate SDR target
holds the soft border and display for UI sampling. Its GPU costs remain visible in the compiled record and timing observations.
It writes neither scene targets nor temporal histories. Ordinary Renderer and offscreen capture paths do not declare the passes; the Viewport toggle controls the editor cue.

Console alone occupies the bottom dock; Performance/Graph are detached, initially closed. Workspace schema 3 restores visibility/bounds; schema 2 migrates to default topology and preserves valid UI scale.
Window > Performance toggles normally; Show measurement opens/focuses Measure anytime. ImGui vertex/index uploads stay in per-slot used lists until the next paced visit, so native windows cannot overwrite main-frame GPU reads. Log ingestion remains independent of GPU/panel freeze; see the [guide](guides/gpu-debugging.md).

Neutral interfaces/capture schema live in `RHI/Include/RHI/`, shared implementation in `RHI/Source/`,
and the backend in `RHI/Backends/Metal4/Source/`; optional `RHIMetal4ImGui` contains UI dependencies.

## Resources and lifetime

- **3 frames in flight.** Each slot owns an argument table, command allocator and growable
  frame-data arena. `bindFrameData` copies at 256-byte or wider alignment into 256 KiB pages
  (oversize pages round up to that quantum), returning the GPU address and binding it. `beginFrame` proves retirement before cursor reuse; pages retain their high-water capacity.
- **Scene tables.** Three `cpuWrite` slots per kind; changes mark all slots dirty, but only the
  retired slot uploads. Static scenes converge to zero writes. Mesh rows initialize once; growing
  tables retain old buffers until the last prepared frame + 3. Stable handles survive reorder and
  reject stale/foreign identities; new instances seed previous pose, promoted by `commitFrame`.
  `Buffer::write` validates permission/source/range. Inspector reports capacities, bytes and writes;
  Scene tables and submission lists are CPU-written graph imports without GPU-write barriers.
- **RHI resources join one residency set** attached to the queue; MetalFX manages its own private resources.
- **Renderer-owned targets**: scene color (`RGBA16Float`, scene-linear, cpu-readable on request),
  display color (`BGRA8Unorm`, what the viewport and a screenshot read), `lmx.render.motion` (`RG16Float`), `lmx.render.reactive` (`R8Unorm`), and two ping-ponged pairs — colour history
  (`lmx.render.historyColor0/1`, `RGBA16Float`) and depth (`lmx.render.sceneDepth0/1`, `D32Float`)
  — all resize with the Viewport panel (debounced, GPU-drained); the shadow map is fixed at 2048².
  A temporal frame renders depth and writes colour into slot `m_temporalFrame % 2` (the resolve
  directly under `NativeTaa` or `VendorTemporal`, the commit copy under `Raw`), so every temporal frame's colour slot
  holds that frame's output. Both pairs are created with the rest, not on first enable, so their
  cost is permanent and reported by `TemporalStatus::historyBytes`/`depthHistoryBytes`. The histogram buffer and the two-float exposure buffer (`{applied, previous}`) are fixed-size and
  persistent — a resize is one of spec 9's reset triggers, since the histogram's binning covered a
  differently-sized image the frame before; every temporal target allocates at the output extent regardless of render scale (ADR 0016), so a scale change reallocates nothing.
- **Vendor scaler**: lazy, output-extent-owned, recreated only on output resize. Packed textures
  are frame transients. Each encoded frame slot retains scaler state until retirement. A public
  fence bridges MetalFX's opaque encoders; CPU-readable outputs use creation-time private scratch and an in-call copy, included in external timing. Private outputs are written directly.
- **Scenes are cached for the device's lifetime** (`Scene/SceneLibrary.h`). GPU resources build on
  first selection; destruction requires retired GPU reads, while removed textures retire after three frames. IDs
  resolve to per-draw pointers/fallbacks. Only referenced images upload; decoded CPU data is freed.
- **IBL assets** (`Source/Asset/Ibl.h`): per-scene diffuse irradiance (16² cube), filtered
  specular (64², or 128² for MaterialLab, five mips), and a 64² DFG LUT. Filtering crosses cube
  faces and chooses source mips by GGX footprint. MaterialLab bounds diffuse work with a separate
  32² source. `Scene/IblUpload.h` owns GPU upload; RGBA16Float/RG16Float preserve HDR and absent
  inputs bind black-cube/zero-DFG fallbacks.
- **Texture mip baking** (`Source/Asset/TextureBake.h`): setup writes deterministic DDS mip chains
  and manifests. Colour filters in linear light; normal maps renormalize. Scene loading prefers
  baked assets and falls back to the same in-process filter. Metal's removed mip generator was measured to point-pick.

## The math

- **GGX metallic-roughness** (`Lighting.slang`): Trowbridge-Reitz D, height-correlated Smith
  visibility, Schlick F (`F0 = mix(0.04, baseColor, metallic)`) and energy-conserving Lambert diffuse.
  Perceptual roughness floors at 0.045 before squaring. Split-sum IBL uses Fdez-Agüera multiple-scattering compensation; occlusion attenuates image-based terms only.
- **Local lights**: relative intensity × linear colour uses finite-range inverse-square attenuation, a 0.01 m distance floor and squared spot-cone response. Exact range/cone/back-face early-outs precede the shared GGX core. The punctual accumulator applies Tokuyoshi/Kaplanyan 2021 Eq.13 normal-footprint specular filtering before divergent light traversal; authored roughness, directional lighting and IBL stay unchanged. See [follow-up](milestones/m7.5-followup.md); local shadows and glTF light import are absent.
- **Exposure/display**: fragments pre-expose linear radiance before the scene target. Manual
  exposure is `exp2(EV)`; auto uses the persistent `{applied, previous}` pair with bounded histogram
  adaptation. Temporal history is corrected by `applied / previous` before clipping/blending.
  DisplayTransform composites bloom, applies PBR Neutral and encodes sRGB. Authored clear colour
  decodes and pre-exposes once at declaration using manual exposure; a sky draws over that clear.
- **Reset policy**: exposure clears on scene/extent changes and auto enable, independently of the
  temporal camera/projection resets. `App/Model/ExposureReset.h` owns that mapping.
- **Reversed infinite-far depth**: near maps to 1, distance tends toward 0. Scene/shadow clear to
  zero and compare Greater; sky pins depth zero and compares GreaterEqual. View-space depth is `-nearZ / sampledDepth`. Light 0 casts through the scene-bound ortho fit with reversed bias
  `{-4.0, -32.0}`. PCF uses 25 Poisson taps; PCSS retains its view/NDC blocker-search mismatch.

## Scenes

File > Open Scene and `--scene` share eight IDs: **Sponza** (`sponza`, default, 120-second two-level corridor/atrium tour), **Damaged Helmet**
(`damaged-helmet`), **Milk Truck** (`milk-truck`, rigid animation), **MaterialLab** (`material-lab`),
**TemporalLab** (`temporal-lab`, motion/emissive diagnostics), **San Miguel** (`san-miguel`, masked
courtyard with a 12-second rail), and **VisibilityLab** (`visibility-lab`, seeded cube/icosphere grid,
four materials and a 12-second rail), plus **LightLab** (`light-lab`, point/spot grid and 12-second rail).
`--lab-instances` accepts 1..1,048,576 only for VisibilityLab (default 4,096, including boundary probes).
`--lab-lights` defaults to 256 (1..4096); `--lab-light-pile` defaults to 0 and their sum is at most 4096, both LightLab-only. Sponza authors 16 static lights; its `--local-light-rig on|off` defaults on. Explicit off retains disabled identities/rows; Hierarchy checkboxes preserve per-light edits. Only enabled lights count toward rendering participation.
San Miguel requires `xmake setup --san-miguel`; the procedural labs are always available. MaterialLab uses the fetched CC0 Studio Small 09 HDRI for sky/IBL, logging a neutral fallback
otherwise. Other scenes retain the shared neutral cubemap/IBL. Missing glTF assets disable catalog
entries with setup guidance; unavailable explicit CLI scenes fail rather than falling back.

`--capture-sequence <directory> --frames N --warmup W` saves N frames after W unsaved frames at
60 Hz as PNG by default (`--capture-format bmp` preserves BMP), with camera/settings/status
metadata. Manifest v2 names the display domain, container and absence of UI. PNG carries sRGB,
gAMA and cHRM plus `lmx:display` and `lmx:frame` text; screenshots select PNG/BMP by extension. The [offline comparison guide](guides/temporal-comparison.md)
covers synchronized reports and optional LDR-FLIP differences against Native TAA, not ground truth.

`--visibility cull|off` and `--submission direct|indirect|batched` apply to every run mode.
[Measurement](guides/gpu-debugging.md#measure-visibility-and-submission) uses exact frame joins and
serialized retirement, separating wait/encode time; editor runs are unscored, not throughput tests.

## Known limits

PCSS retains its view/NDC blocker-search mismatch; IBL rebuilds on load; direct lighting is single-scatter. Baked DDS keys on image index, not colour/data role.
Sponza startup is synchronous; Metal is the only backend. UI blends straight alpha in encoded SDR
sRGB. The Viewport maps pixels 1:1 after resize debounce and stretches the old target during it. Detached windows remain SDR. EDR is deferred; future scope belongs to the roadmap.

Cross-references: [render graph](decisions/0005-render-graph.md), [scene-linear image formation](decisions/0006-scene-linear-image-formation.md),
[vendor reconstruction](decisions/0017-vendor-reconstruction-capability.md); `Shaders/` holds entries,
`Shaders/Modules/` shared math and `Shaders/Tests/` oracles; runtime shader basenames stay unchanged.

UI zoom applies before NewFrame; debounced resize preserves camera, render scale and saved layouts.
