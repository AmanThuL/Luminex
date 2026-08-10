# Luminex — one frame, as of the M4 baseline (2026-08-10)

What the renderer does between `beginFrame` and `endFrame`, written for planning what to build
next. Updated at milestone boundaries.

## The frame at a glance

A `RenderGraph` is declared fresh every frame: it imports the renderer's own targets and the
swapchain drawable, four passes declare their reads, attachments, and writes over them, and
`compile()` proves the declarations form a DAG and answers a serial schedule before any of it
reaches the GPU. `execute()` then runs that schedule, deriving the one render-target-to-sampled
barrier each declared cross-pass read justifies.

```
beginFrame (blocks until frame N-3 retired; shared-event pacing, ring-recycle invariant asserted)
│
├─ declare: import shadow map, scene color (HDR), scene depth, display color, swapchain drawable
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
│       └─ sky, drawn last: camera-centered sphere pinned to the reversed far plane (depth 0),
│            cull none, GreaterEqual, t2 cubemap, the same preExposure
│
├─ 3. lmx.pass.display     fullscreen triangle: Load the scene color texel-for-texel (no filter)
│       → Khronos PBR Neutral tone map → sRGB encode → display color (BGRA8Unorm, viewport-sized)
│       Shaders/DisplayTransform.slang — the only shader in the frame that encodes sRGB
│
├─ 4. lmx.pass.ui          → swapchain drawable
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
handles (`GraphTexture`/`GraphBuffer`) rather than as commands. Resources are **imported only** —
`importTexture`/`importBuffer` bring in a texture or buffer the frame already owns at version 0;
the graph creates no GPU objects and pools nothing. A pass names every version it reads, at most
one color and one depth attachment (each is a versioned write), and any non-attachment writes;
`compile()` hard-fails a read of a version no pass wrote, two passes writing one version, a cycle,
an attachment/format mismatch, or an export of a version nothing produced, and otherwise answers
one serial topological order. `execute()` re-validates, then runs that schedule: each pass becomes
one labelled render pass, its body runs between `beginRenderPass`/`endRenderPass` with a
`PassResources` that resolves only the handles the pass declared — an undeclared resolve is a
reported failure, not a resolved pointer. A graph is declared fresh every frame; at four passes,
compile cost is trivial and scheduling optimization, transient pooling, and dead-pass culling stay
deliberately absent (`docs/decisions/0005-render-graph.md`).

Every pass -- render or compute -- is also a GPU timing boundary: `rhi::Device::passTimings()`
reports each pass's label and GPU milliseconds for the most recently retired frame, and
`passTimingsFrame()` names that frame. Both are populated by counter samples the Metal 4 backend
takes at pass begin/end and resolved once the shared event proves that frame retired. The editor's Stats panel lists every pass of the newest retired frame with its time.

The backend-neutral interfaces and capture schema are public headers under `RHI/Include/RHI/`.
Their implementation and validation live in `RHI/Source/`; the only backend lives in
`RHI/Backends/Metal4/Source/`. Dear ImGui submission is an application-facing adapter in the
optional `RHIMetal4ImGui` target, so it does not make ImGui part of the core RHI dependency surface.

## Resources and lifetime

- **3 frames in flight.** Each in-flight slot owns an argument table, a bump allocator, and a
  256 KiB uniform ring. `setUniforms` copies into the ring at 256-byte alignment. `beginFrame`
  asserts (all builds) that the shared event proves the recycled slot's frame retired before
  reuse. A 12-frame GPU stress test attributes any cross-frame overwrite to its culprit by color.
- **Everything lives in one residency set** attached to the queue; textures join at creation.
- **Renderer-owned targets**: scene color (`RGBA16Float`, scene-linear, cpu-readable when the
  caller asks), scene depth (`D32Float`, kept sampled rather than discarded so a caller can
  reconstruct view-space distance from it), and display color (`BGRA8Unorm`, what the viewport and
  a screenshot read) all resize with the Viewport panel (debounced, GPU-drained); the shadow map
  is fixed at 2048².
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
- **Exposure and display**: every fragment multiplies its linear output by `preExposure =
  exp2(EV)` (a Render Settings slider, default 0) before the scene target sees it, so the scene
  color holds pre-exposed scene-linear radiance. `Shaders/DisplayTransform.slang` is the frame's
  one display boundary: Khronos PBR Neutral (identity minus a small black offset below its
  compression start, then peak-channel compression and desaturation toward it) followed by the
  sRGB encode. The editor's clear color is authored in display space and decoded-then-pre-exposed
  once, at pass declaration, so the cleared background and every shaded pixel agree on what space
  the target holds.
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

1. **Execution substrate** — the graph models render passes only; compute, storage resources,
   general barriers, transient pooling, and graph-level optimization are M5 territory, deferred by
   design rather than by oversight.
2. **Automatic exposure and post-processing** — exposure is a manual slider; there is no histogram
   adaptation, bloom, or temporal reconstruction yet.
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
8. **Portability** — Metal remains the only backend; the reversed-Z, HDR, and graph conventions
   above are what a future D3D12 backend has to reproduce.

Cross-references: `docs/milestones/m4.md`, `docs/decisions/0005-render-graph.md`,
`docs/decisions/0006-scene-linear-image-formation.md`, and `Shaders/` (ScenePass.slang and
Lighting.slang are the frame's shading core, DisplayTransform.slang its display boundary).
