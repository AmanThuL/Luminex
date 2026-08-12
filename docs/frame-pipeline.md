# Luminex — one frame, as of the M5 execution substrate (2026-08-11)

What the renderer does between `beginFrame` and `endFrame`, written for planning what to build
next. Updated at milestone boundaries.

## The frame at a glance

A `RenderGraph` is declared fresh every frame: it imports the renderer's own targets, the
persistent histogram and exposure buffers, and the swapchain drawable; the shadow, scene+sky,
exposure-feedback, bloom, and display passes declare their reads, attachments, and writes over
them; `compile()` proves the declarations form a DAG and answers a serial schedule (with dead-pass
culling) before any of it reaches the GPU. `execute()` then runs that schedule, deriving the RAW,
WAR, and WAW barriers each declared cross-pass access conflict justifies.

Below is the *default* frame — auto-exposure off (manual EV, the default mode), bloom on (its
default). Auto-exposure and bloom are both ordinary declared passes either way; toggling either off
does not remove it from the graph, it removes the one declaration that reaches a sink, and dead-pass
culling drops the rest (`docs/guides/gpu-debugging.md`'s dump shows exactly this for a toggled-off
frame).

```
beginFrame (blocks until frame N-3 retired; shared-event pacing, ring-recycle invariant asserted)
│
├─ declare: import shadow map, scene color (HDR), scene depth, display color, histogram buffer,
│           exposure buffer, swapchain drawable
│
├─ [only when auto-exposure is on and a reset trigger fired this frame]
│  0. lmx.pass.exposure.seed   compute, 1 thread → exposure buffer
│       writes exp2(manualEV): the loop's one-frame feedback restarts from the manual value on the
│       first frame, a scene switch, the auto-exposure enable transition, and a successful resize
│       (spec 9; the four triggers reduce to one pure decision, App/ExposureReset.h's
│       shouldResetExposure(), EditorShell calls at each site)
│
├─ 1. lmx.pass.shadow      depth-only → shadow map (2048², D32Float, store)
│       every opaque DrawItem, depth bias {-4.0, -32.0} (negated for reversed-Z), light 0 only
│       reversed depth: clears to 0, Greater compare, comparison sampler GreaterEqual
│
├─ 2. lmx.pass.scene       → scene color (RGBA16Float) + scene depth (D32Float), viewport-sized
│       │  per-pass uniforms (b2): viewProj, shadowTransform, eye, time, preExposure,
│       │  3 directional lights, shadow filter
│       ├─ opaque DrawItems: GGX metallic-roughness BRDF (direct lights) + diffuse/specular IBL
│       │    (split-sum reconstruction with Fdez-Agüera multi-scatter compensation) + shadow
│       │    factor (25-tap Poisson PCF or PCSS, comparison sampler s1) + normal mapping (TBN) +
│       │    occlusion (image-based terms only) + emissive, summed and pre-exposed; nothing here
│       │    encodes sRGB
│       │    per-draw uniforms (b1): mvp, model, inverse-transpose normalMatrix, uvTransform,
│       │    albedo, roughness, metallic, emissive, flags
│       │    per-draw textures t0 base color / t1 normal / t4 metallic-roughness / t5 occlusion /
│       │    t6 emissive (white/flat-normal fallbacks when unmapped); per-pass shared t3 shadow
│       │    map, t7 irradiance, t8 prefiltered environment, t9 DFG LUT
│       ├─ reads the shadow map's written version — the graph derives one
│       │    textureBarrier(RenderTarget, ShaderRead) in front of this pass from that declaration
│       ├─ auto-exposure only: binds ScenePassAuto.slang/SkyAuto.slang's pipelines instead of
│       │    ScenePass.slang/Sky.slang's, and reads the exposure buffer directly (kExposureOverride,
│       │    an ordinary buffer read, not a storage binding — raster passes have no storage
│       │    bindings) in place of the manual `preExposure` uniform. Two shader files rather than
│       │    one runtime branch, so the manual pipeline's compiled output stays provably identical
│       │    to pre-M5 (ScenePassAuto.slang's header)
│       └─ sky, drawn last: camera-centered sphere pinned to the reversed far plane (depth 0),
│            cull none, GreaterEqual, t2 cubemap, the same preExposure (or the same exposure-buffer
│            read, in auto mode)
│
├─ 3. lmx.pass.exposure.clearHistogram   copy → histogram buffer (256 × uint32, fillBuffer 0)
├─ 4. lmx.pass.exposure.histogram        compute, reads scene color + exposure buffer
│       → histogram buffer; log-luminance binning divides the pre-exposed pixel back down by this
│       frame's preExposure to reconstruct scene-referred luminance before binning it (spec 9)
├─ 5. lmx.pass.exposure.resolve          compute, 1 thread, reads histogram buffer
│       → exposure buffer; percentile-trimmed weighted average around a target grey point, EV-
│       clamped, becomes the value the *next* frame's scene/sky passes and step 0's seed above (on
│       a reset frame) will read — the whole chain's one-frame lag (spec 9)
│       │  declared every frame regardless of the toggle; `graph.exportBuffer` on the buffer this
│       │  pass writes happens only when auto-exposure is on, which is the sink dead-pass culling
│       │  needs to keep clearHistogram/histogram/resolve scheduled at all (`RenderGraphTests.cpp`:
│       │  "exposure and bloom passes are culled when both features are off")
│
├─ 6. lmx.pass.bloom.threshold        compute, reads scene color
│       → bloomChain mip 0 (ceil-half-res RGBA16Float, so odd final rows/columns remain covered):
│       2×2-box prefilter + soft threshold on pre-exposed luminance — bloom tracks what the display
│       sees, so manual and auto exposure shift it consistently (spec 10)
├─ 7. lmx.pass.bloom.downsample0..N-1  compute, one pass per level, mip L-1 → mip L of bloomChain
│       2×2-box downsample; N = kMaxBloomDownsampleLevels (4), clamped so no mip collapses to 1×1
│       early — a 32-wide bloom base (the common viewport size) reaches the full depth
├─ 8. lmx.pass.bloom.upsampleN-1..0   compute, one pass per level, walks back to mip 0
│       → bloomBlur (a second graph-created texture, one fewer mip than bloomChain): nearest 2×
│       upsample + add, each step's "small" input the previous step's own output. A second texture
│       rather than accumulating into bloomChain in place, because one compute pass may read and
│       write one texture only through disjoint ranges (spec 6) and the accumulate step's base and
│       small ranges would otherwise overlap if they shared a resource (`BloomUpsample.slang`'s
│       header)
│       │  both chains declared every frame; only the display pass's read of bloomBlur (below) is
│       │  conditional, so bloom's six-to-many passes cull together when the toggle is off
│
├─ 9. lmx.pass.display     fullscreen triangle: Load the scene color texel-for-texel (no filter)
│       → scene colour + bloomBlur mip 0 × bloomIntensity (bloom-off binds an exact-zero 1×1
│       fallback, zeroes the intensity uniform, and skips the texture load, so no out-of-range
│       fallback access occurs and the sum stays bit-identical to no bloom at all)
│       → Khronos PBR Neutral tone map → sRGB encode → display color (BGRA8Unorm, viewport-sized)
│       Shaders/DisplayTransform.slang — the only shader in the frame that encodes sRGB
│
├─ 10. lmx.pass.ui          → swapchain drawable
│       Dear ImGui (docked editor shell); the Viewport window samples the display color texture;
│       App declares this pass itself and reads the display pass's output to join it
│
└─ graph.execute(commands) → endFrame → present (or endFrame(nullptr) for the offscreen
     --screenshot / test path, which stops at lmx.pass.display and reads that target back)
```

Vertex data is bindless vertex-pulling everywhere: a `StructuredBuffer<VertexPNTU>` at slot b0
(48-byte pos/normal/tangent₄/uv), indices as plain uint32 buffers consumed per draw. There are
no vertex descriptors in the pipeline.

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
schedule: each pass becomes one labelled render, compute, or copy pass, its body runs inside that
scope with a `PassResources` that resolves only the handles the pass declared — an undeclared
resolve is a reported failure, not a resolved pointer. Barrier derivation covers RAW, WAR, and WAW
conflicts, including per-subresource texture writers. Persistent-import overloads seed the prior
frame's terminal texture or buffer access, so reused renderer targets and exposure
feedback are ordered across command buffers as well. Compilation answers with a `CompiledFrameRecord`
describing everything it decided, which `Render/GraphDump.h` renders as deterministic text. A graph
is declared fresh every frame; scheduling optimization beyond dead-pass culling and conservative
transient pooling stays deliberately absent.

Transients are placed in a `Render/TransientPool`: one placement heap per frame-in-flight slot,
reused only after `Device::beginFrame()` has proved that slot's previous frame retired, and resized
into a new generation when a frame's footprint changes. Compilation assigns offsets first-fit over
lifetime-disjoint transients whose descriptors agree on kind, format, extent, mip count, usage,
size, and alignment, emits a whole-resource barrier wherever one transient takes bytes another held,
and records every lifetime, assignment, the heap high-water mark, and the alias savings.
`RenderGraph::setPoolingEnabled(false)` — the editor's Transient pooling checkbox — gives every
transient its own bytes and cannot change the picture, because a transient holds nothing until a
pass writes it. The histogram and exposure buffers are imported, not transient: both must outlive
the frame that wrote them (the exposure buffer for a full frame, into the next one's shading).

Every pass -- render or compute -- is also a GPU timing boundary: `rhi::Device::passTimings()`
reports each pass's label and GPU milliseconds for the most recently retired frame, and
`passTimingsFrame()` names that frame. Both are populated by counter samples the Metal 4 backend
takes at pass begin/end and resolved once the shared event proves that frame retired. The editor's
Stats panel retains up to 60 samples per schedule position and publishes a pausable rolling average
four times per second, with latest/range details on hover; a changed ordered pass-label sequence
resets the window. The Render Graph inspector instead shows the exact newest retired frame's full
declaration, schedule, culling, barriers, and joined timing values.

The backend-neutral interfaces and capture schema are public headers under `RHI/Include/RHI/`.
Their implementation and validation live in `RHI/Source/`; the only backend lives in
`RHI/Backends/Metal4/Source/`. Dear ImGui submission is an application-facing adapter in the
optional `RHIMetal4ImGui` target, so it does not make ImGui part of the core RHI dependency surface.

## Resources and lifetime

- **3 frames in flight.** Each in-flight slot owns an argument table, a command allocator, and a
  growable per-slot frame-data page arena (256 KiB normal pages). `bindFrameData` bump-allocates
  within the active page at 256-byte alignment (or wider, for an over-aligned type) and returns the
  block's GPU address. `beginFrame` asserts (all builds) that the shared event proves the recycled
  slot's frame retired before reuse. A 12-frame GPU stress test attributes any cross-frame overwrite
  to its culprit by color.
- **Everything lives in one residency set** attached to the queue; textures join at creation.
- **Renderer-owned targets**: scene color (`RGBA16Float`, scene-linear, cpu-readable when the
  caller asks), scene depth (`D32Float`, kept sampled rather than discarded so a caller can
  reconstruct view-space distance from it), and display color (`BGRA8Unorm`, what the viewport and
  a screenshot read) all resize with the Viewport panel (debounced, GPU-drained); the shadow map
  is fixed at 2048². The histogram buffer (256 × uint32) and the one-float exposure buffer are
  fixed-size and persistent — a resize is one of spec 9's reset triggers precisely because the
  histogram's binning covered a differently-sized image the frame before.
- **Scenes are cached for the device's lifetime** (`SceneLibrary`): meshes, materials, textures,
  and the scene's IBL set build once on first selection. Sponza and Damaged Helmet upload only
  material-referenced images; decoded CPU image data is dropped before the builder returns.
- **IBL assets are per-scene and generated at build time** (`Source/Engine/Ibl.h`): a
  cosine-convolved irradiance cube (16² faces), a GGX-prefiltered specular chain (64² base, 5
  mips), and a split-sum DFG lookup table (64², `RG16Float`), all uploaded `RGBA16Float`/`RG16Float`
  so radiance above 1.0 survives. A `SceneView` that carries none substitutes black-cube and
  zero-DFG fallbacks rather than reading an unbound slot.
- **Base-color and normal images bake offline when `xmake setup` runs**: `Tools/TextureBake`
  (wrapping `Source/Engine/TextureBake.h`) box-filters a full mip chain in linear light (sRGB
  images decode/filter/re-encode; normal maps renormalize per level) and writes a DDS plus a
  manifest recording the source hash. `Scene` prefers the baked DDS beside a glTF file and falls
  back to the same filter computed in-process (slower load, not incorrect) when it is absent.
  `Device::generateMipmaps` no longer exists — Metal's blit variant was measured to point-pick.

## The math, briefly

- **GGX metallic-roughness BRDF** (`Shaders/Lighting.slang`): Trowbridge-Reitz `D`, height-correlated
  Smith `V` (combined `G / (4 N·V N·L)` form), Schlick `F` with `F0 = mix(0.04, baseColor,
  metallic)`, energy-conserving Lambert diffuse `(1 - F)(1 - metallic) baseColor / π`. Perceptual
  roughness is floored at 0.045 before squaring to `alpha`, bounding the specular lobe the raster
  grid can resolve.
- **Image-based lighting**: the split-sum reconstruction (Karis 2013) plus Fdez-Agüera's
  multiple-scattering compensation, so a white furnace returns its own radiance at every roughness
  and metallic value rather than losing energy to single-scattering loss as roughness rises.
  Occlusion attenuates the image-based terms only — the shadow map already answers direct-light
  visibility, and applying occlusion to both would darken a lit surface twice.
- **Exposure and display**: every fragment multiplies its linear output by `preExposure` before the
  scene target sees it, so the scene color holds pre-exposed scene-linear radiance. Manual mode (the
  default) computes `preExposure = exp2(EV)` from the Render Settings slider on the CPU, unchanged
  since before M5. Auto mode instead reads a persistent GPU exposure buffer that a one-frame
  histogram feedback loop maintains (spec 9, diagrammed above) — the loop resolves an instantaneous
  target only; adaptation and smoothing are M6. `Shaders/DisplayTransform.slang` is the frame's one
  display boundary: bloom composites in first (pre-exposed luminance, so it shifts with exposure the
  same way the rest of the frame does), then Khronos PBR Neutral (identity minus a small black
  offset below its compression start, then peak-channel compression and desaturation toward it)
  followed by the sRGB encode. The editor's clear color is authored in display space and
  decoded-then-pre-exposed once, at pass declaration, so the cleared background and every shaded
  pixel agree on what space the target holds; the clear always uses the *manual* exposure value even
  in auto mode, since auto's actual value lives only in the GPU-side buffer and every scene with a
  sky draws over the clear entirely.
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

Three, behind the Inspector dropdown, drawn from one catalog (`--scene` accepts the same IDs):
**Sponza** (`sponza`, the default world scene, converted deterministically from the official
Crytek OBJ+PNG archive), **Damaged Helmet** (`damaged-helmet`, glTF, generated tangents), and
**MaterialLab** (`material-lab`, always available — a code-generated roughness×metallic sphere
grid plus horizontally arranged color, texture, normal, and depth diagnostics). MaterialLab uses
the pinned CC0 Studio Small 09 HDRI for both its visible sky and generated IBL when setup has
fetched it, and logs before falling back to a deterministic neutral environment otherwise. Sponza
and Damaged Helmet retain the shared code-generated neutral cubemap and IBL set. Missing required
glTF assets disable their dropdown entries with setup guidance; an unavailable explicit CLI scene
exits with an error instead of falling back.

## Known gaps / candidate techniques for the next milestone

1. **Exposure adaptation and smoothing** — spec 9 resolves an instantaneous target only; temporal
   adaptation, a history-reset framework, and automatic exposure as a default mode are M6.
2. **Bloom's upsample is nearest-neighbour** — a deterministic, testable choice (spec 10), but it
   produces a visibly blocky halo around small bright highlights at the current chain depth;
   bilinear or a wider filter kernel is a candidate improvement, not a correctness fix.
3. **PCSS parameterization** — the migrated blocker search still mixes a view-space near-plane
   constant with NDC-space receiver depth, a preserved unit bug; fixing it is cheap and deferred.
4. **IBL regeneration cost** — each scene's irradiance/prefiltered/DFG set regenerates on load;
   MaterialLab now supplies a small studio environment, so caching becomes worthwhile if more
   authored environments or faster scene switching arrive.
5. **Direct lighting is single-scatter** — the analytic BRDF has no multi-scatter compensation;
   only the image-based term does.
6. **Baked-DDS selection keys on image index alone**, not on how a material uses that image; a
   glTF file that reused one image in both a color and a data role would need the offline bake to
   distinguish them, which it does not yet do.
7. **Sponza startup is still synchronous** — decode and upload still block the window before it
   becomes responsive; asynchronous staging remains future work.
8. **Portability** — Metal remains the only backend; the reversed-Z, HDR, graph, and barrier
   conventions above are what a future D3D12 backend has to reproduce.

Cross-references: `docs/specs/2026-08-11-m5-execution-substrate-design.md` (sections 6/7/9/10 —
subresource model, graph semantics, exposure feedback, features), `docs/decisions/0005-render-graph.md`,
`docs/decisions/0006-scene-linear-image-formation.md`, and `Shaders/` (ScenePass.slang and
Lighting.slang are the frame's shading core, HistogramAccumulate/ExposureResolve.slang the exposure
chain, BloomThreshold/Downsample/Upsample.slang the bloom chain, DisplayTransform.slang its display
boundary).
