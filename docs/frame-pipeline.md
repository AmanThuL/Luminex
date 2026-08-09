# Luminex — one frame, as of the M3.1 baseline (2026-08-10)

What the renderer does between `beginFrame` and `endFrame`, written for planning what to build
next. Updated at milestone boundaries.

## The frame at a glance

```
beginFrame (blocks until frame N-3 retired; shared-event pacing, ring-recycle invariant asserted)
│
├─ 1. Shadow pass          depth-only → shadow map (2048², depth32Float, storeDepth)
│       every opaque DrawItem, depth bias {-4.0, slope -32.0}, light 0 only
│       reversed depth: clears to 0, keeps Greater, comparison sampler GreaterEqual
│
├─ 2. Scene pass           → offscreen color (BGRA8Unorm) + depth (D32), viewport-sized
│       │  per-pass uniforms (slot b2): viewProj, shadowTransform, eye, time, ambient,
│       │  3 directional lights, shadow filter
│       ├─ opaque DrawItems: Blinn-Phong in linear space + cubemap reflection term
│       │    + shadow factor (25-tap Poisson PCF or PCSS, comparison sampler s1)
│       │    + normal mapping (TBN, uniform branch on material flag)
│       │    per-draw uniforms (b1): mvp, model, uvTransform, albedo, fresnelR0, roughness, flags
│       │    textures t0 diffuse / t1 normal (white / flat-normal fallbacks when unmapped)
│       ├─ sky, drawn last: camera-centered sphere, z = 0 far-plane trick, GreaterEqual, cull none,
│       │    cubemap t2
│       └─ every fragment sRGB-encodes on output (Encode.slang) — the target stays non-sRGB
│
├─ textureBarrier            scene color: RenderTarget → ShaderRead
│
├─ 3. UI pass              → swapchain drawable
│       Dear ImGui (docked editor shell); the Viewport window samples the scene color texture
│
└─ endFrame → present (or endFrame(nullptr) for the offscreen --screenshot / test path)
```

Vertex data is bindless vertex-pulling everywhere: a `StructuredBuffer<VertexPNTU>` at slot b0
(48-byte pos/normal/tangent₄/uv), indices as plain uint32 buffers consumed per draw. There are
no vertex descriptors in the pipeline.

## Resources and lifetime

- **3 frames in flight.** Each in-flight slot owns an argument table, a bump allocator, and a
  256 KiB uniform ring. `setUniforms` copies into the ring at 256-byte alignment. `beginFrame`
  asserts (all builds) that the shared event proves the recycled slot's frame retired before
  reuse. A 12-frame GPU stress test attributes any cross-frame overwrite to its culprit by color.
- **Everything lives in one residency set** attached to the queue; textures join at creation.
- **Scenes are cached for the device's lifetime** (`SceneLibrary`): meshes, materials, and
  textures build once on first selection, and scene switches never free GPU memory a recorded
  frame could still name. Sponza uploads only material-referenced textures; decoded CPU image data
  is dropped before the builder returns.
- **Shadow map + offscreen color/depth are Renderer-owned.** The color/depth pair resizes with
  the Viewport panel (debounced, GPU-drained); the shadow map is fixed at 2048².

## The lighting/shadow math, briefly

The forward-lighting baseline uses these deliberately preserved equations and constants:

- **Blinn-Phong**: `m = shininess·256`, `(m+8)/8 · (N·H)^m` spec factor, Schlick Fresnel at the
  half-vector, `spec/(spec+1)` LDR clamp. Materials are scalar `fresnelR0` + `roughness` with a
  diffuse map — no metallic/roughness maps (glTF's metallic maps onto `fresnelR0` via
  `mix(0.04, baseColor, metallic)` at load).
- **Reflection term**: `shininess · SchlickFresnel · skyCubemap(reflect(−toEye, N))`.
- **Shadows**: one directional caster (light 0). Ortho frustum fit to the scene's bounding
  sphere (`center − 2r·dir` eye), 25-tap Poisson-disk PCF re-seeded per pixel from a hash of the
  shadow UV (intentionally noisy), or PCSS (blocker search → penumbra → variable
  PCF) as a runtime toggle. Depth bias tuned by measurement: constant 4.0, slope 32.0 (the PCF
  kernel spans ~25 texels; a constant bias of 1.0 was insufficient in measured captures).
- **Gamma**: color textures are sRGB formats (hardware-decode on
  sample), lighting runs linear, fragments encode on output. Authored color constants
  (light strengths, ambient, albedos) are decoded once at scene build. The one deliberate
  exception: the fog-gray clear (0.7) is written raw — the hardware clear bypasses the encode
  shader, so it must already be display-space.

## Scenes

Two, behind the Inspector dropdown: **Sponza** (`sponza`, the default world scene, converted
deterministically to core glTF from the official Crytek OBJ+PNG archive) and **Damaged Helmet**
(`damaged-helmet`, glTF, generated tangents). Both use a code-generated neutral cubemap. Missing
fetched assets disable the dropdown entry with setup guidance; an unavailable explicit CLI scene
exits with an error instead of falling back.

## Known gaps / candidate techniques for the next milestone

Ordered roughly by how much they'd change the image, with the sharpest first:

1. **True PBR** — the material model is Blinn-Phong with scalar Fresnel/roughness; Sponza and
   Helmet ship metallic-roughness maps that currently collapse to per-material scalars.
   The natural headline feature for M4.
2. **Post-processing stack** — there is none: no tonemapping (LDR throughout), no bloom, no
   exposure. A HDR intermediate + tonemap would immediately lift Sponza's interior.
3. **Shadow quality** — single 2048² map for the whole scene: cascades (CSM) for range,
   and the current PCSS path carries a preserved unit bug (view-space NEAR_PLANE mixed with NDC
   z-receiver → blocker search sweeps ±190 texels, penumbra barely tracks occluder distance) —
   fixing its parameterization is cheap and visible. `CalcShadowFactor`'s perspective divide is
   untested (ortho w≡1) if spot/point shadows ever arrive.
4. **Mip quality** — Metal 4's blit `generateMipmaps` was measured to point-pick, not box-filter:
   Sponza's textures alias in minification. Candidates: offline mips (KTX2), a compute
   downsample pass, or staging through a Private texture.
5. **MSAA** — not implemented. The offscreen architecture needs resolve plumbing in the RHI.
6. **Normal transform correctness** — the shader transforms normals by the model matrix's upper
   3×3; only exact today because every non-uniform scale in shipped scenes is axis-aligned with
   its normals. A future free-form non-uniform scale needs an inverse-transpose in
   `ObjectUniforms`.
7. **Frame-graph-ish growth** — passes are hand-sequenced in `Renderer::render`; a third pass
   (post, cascades) is where explicit pass/resource description starts paying.
8. **GPU-driven / modern-Metal candidates**: MetalFX upscaling, mesh shaders, GPU culling, and
   Metal ray tracing remain roadmap choices with explicit feature gates.

Cross-references: the M3 milestone record and `Shaders/` (the six pipeline/module files,
plus the test-oracle shaders, are short and commented — ScenePass.slang is the frame's core in
~200 lines).
