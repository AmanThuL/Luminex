# M4 — Correct Image Formation: Execution Plan

**Status**: In progress

This plan decomposes the accepted M4 boundary in [the roadmap](../roadmap.md) without expanding it.
The roadmap owns the outcome, deliverables, exit gate, and deferrals; where this plan makes an
implementation choice, the choice serves that boundary. Tasks land in dependency order; every task
leaves `main` buildable with green unit tests, GPU tests on Metal 4 hardware, formatting, and
policy. Frozen research informed the choices below but does not extend the milestone.

**Goal:** the frame runs through a small validating render graph and produces a scene-linear HDR
image with physically based glTF materials, closed by the roadmap's M4 exit gate.

**Approach:** grow the RHI first (GPU timestamps, `RGBA16Float`, reversed-Z compares), introduce
the graph and prove bit-identical migration before any image change, then change the image
deliberately: FP16 scene color with manual exposure and a replaceable display transform, reversed-Z,
deterministic filtered mips, full metallic-roughness materials with GGX direct lighting and IBL.
`MaterialLab` arrives before the shading rewrite so every image change lands with a deterministic
check.

## Global constraints

- C++23; Metal 4 only; 3 frames in flight; creation returns `Result<T>`; misuse is `LMX_ASSERT`;
  every GPU object gets a label; no Metal types in public RHI headers.
- Lighting math runs in linear space; authored color constants decode via `engine::srgbToLinear`.
- Deferred by the roadmap — do not build: general compute and storage execution, transient pooling,
  graph optimization, automatic exposure, bloom, temporal reconstruction, local-light scaling,
  advanced material lobes, ray tracing.
- Renderer/RHI/shader tasks run `MTL_DEBUG_LAYER=1 xmake test Tests/gpu` on Metal 4 Apple Silicon
  before merge; every commit passes `xmake format --check` and `xmake policy`.
- One short-lived outcome branch per task per [commit conventions](../conventions/commits.md).

## Stage 1 — Graph foundation and GPU timing

### T1: RHI GPU pass timestamps

- **Files:** modify `Source/RHI/RHI.h`, `Source/RHI/Metal4/Metal4Device.h`,
  `Source/RHI/Metal4/Metal4Device.cpp`, `Source/RHI/Metal4/Metal4DeviceFrame.cpp`,
  `Source/RHI/Metal4/Metal4CommandList.cpp`; test `Tests/GpuRhiTests.cpp`.
- **Produces:** `struct PassTiming { std::string label; double gpuMilliseconds; }` and
  `std::span<const PassTiming> Device::passTimings()` returning per-pass GPU times for the most
  recent retired frame. `CommandList` samples at `beginRenderPass`/`endRenderPass` internally —
  callers add nothing.
- Discovery first: verify which Metal 4 counter-sampling surface metal-cpp exposes (counter sample
  buffers at encoder stage boundaries, or command-buffer resolve); shape the backend to it without
  leaking types. Sample buffers are per frame slot and resolved only after the shared event proves
  the slot retired — no mid-frame sync.
- Tests first: a GPU case renders two labeled passes, then asserts `passTimings()` reports both
  labels with positive milliseconds after retirement; a case asserts the span is empty before any
  frame completes.

### T2: RHI formats, compares, and binding budget

- **Files:** modify `Source/RHI/RHI.h`, `Source/RHI/Validate.cpp`,
  `Source/RHI/Metal4/Metal4Common.h`, `Source/RHI/Metal4/Metal4Device.cpp`,
  `Source/RHI/Metal4/Metal4DeviceResources.cpp`, `Source/RHI/Metal4/Metal4Resources.cpp`; tests
  `Tests/RHIValidateTests.cpp`, `Tests/GpuRhiTests.cpp`.
- **Produces:** `Format::RGBA16Float` (color-renderable, sampled, cpu-readback), `Format::RG16Float`
  (sampled, for the DFG LUT), `DepthCompare::Greater` and `GreaterEqual`,
  `CompareFunc::GreaterEqual`; texture readback sized by a per-format bytes-per-pixel instead of the
  hardcoded 4; Metal4 texture binding capacity raised from 8 to 16 slots.
- Tests first: validation unit cases for each new format/usage pairing; a GPU oracle writes values
  above 1.0 into an `RGBA16Float` target and reads back exact half-precision values; a depth oracle
  proves `Greater` keeps the nearer-in-reversed-Z fragment.

### T3: render graph core (CPU)

- **Files:** create `Source/Render/RenderGraph.h`, `Source/Render/RenderGraph.cpp`; test
  `Tests/RenderGraphTests.cpp`.
- **Produces:** `lmx::render::RenderGraph` with versioned logical handles `GraphTexture` /
  `GraphBuffer`; `importTexture(rhi::Texture&, std::string_view name)` / `importBuffer(...)`;
  `addPass(std::string_view label, PassDesc, ExecuteFn)` where `PassDesc` declares reads, color and
  depth attachments (clear/load/store intent), and writes; `exportTexture(GraphTexture)` roots
  liveness; `compile()` returning `Result<Schedule>` — a serial topological order.
- Validation hard-fails: read-before-write, cycles, attachment extent/format mismatch, double write
  to one version, and export of a never-written resource. `ExecuteFn` receives `PassResources`
  whose `texture(GraphTexture)` resolves only handles the pass declared — an undeclared resolve is
  a validation failure, proven by an intentional illegal read in tests.
- Pure CPU; no device, no execution. One Catch2 case per rule, red first.

### T4: graph execution and pass migration

- **Files:** modify `Source/Render/RenderGraph.h/.cpp`, `Source/Render/Renderer.h/.cpp`,
  `Source/App/main.cpp`, `Source/App/EditorShell.cpp`; tests `Tests/GpuRendererTests.cpp`.
- **Consumes:** T1 timings, T2 nothing, T3 graph core. **Produces:**
  `RenderGraph::execute(rhi::CommandList&)` encoding passes in schedule order — render passes built
  from declared attachments, `lmx.pass.*` labels taken from pass names, `textureBarrier` edges
  derived from declared render-target-then-sampled uses, T1 timestamps per pass.
- The Renderer declares shadow, scene(+sky) passes; the App joins the UI pass with the imported
  swapchain texture. The graph is declared fresh each frame; at four passes, compile cost is
  trivial and optimization stays deferred.
- Equivalence gate: a GPU test renders one frame through the legacy hand-sequenced path and one
  through the graph, reads both back, and asserts byte-identical output; the legacy path is removed
  in the same task once the test passes. Every image-affecting constant stays untouched.
- Editor: the Stats panel lists every pass label with its GPU milliseconds (exit gate: visible
  timestamps). A screenshot regression pins the graph-path image as the M3 reference thereafter.

## Stage 2 — HDR image formation

### T5: MaterialLab scene

- **Files:** create `Source/Engine/MaterialLab.cpp` (+ declaration in `Source/Engine/Scene.h`);
  modify `Source/Engine/SceneLibrary.cpp`; tests `Tests/EngineSceneTests.cpp`.
- **Produces:** catalog entry `material-lab` (`SceneRole::Diagnostic`, always available, fully
  code-generated): a sphere grid for material sweeps, known-color patches with authored sRGB
  values, a horizontal gradient ramp, depth probes at known view distances, and a normal-map probe
  quad. Deterministic geometry and materials; reachable via `--scene material-lab`.
- Tests first: the scene loads without fetched assets; probe objects project to expected pixels
  using the existing projection helper pattern; the catalog still has exactly one default.

### T6: HDR scene color, manual exposure, and display transform

- **Files:** create `Shaders/DisplayTransform.slang`; modify `Source/Render/Renderer.h/.cpp`,
  `Shaders/ScenePass.slang`, `Shaders/Sky.slang`, `Source/App/EditorShell.cpp`,
  `Source/App/Screenshot.cpp`; tests `Tests/GpuRendererTests.cpp`.
- **Produces:** scene and sky render into an `RGBA16Float` scene-linear color target; fragments
  write pre-exposed linear values (`preExposure = exp2(ev)` from a new `PassUniforms` field; EV
  slider in Render Settings, default 0). Scene shaders stop importing `Encode` entirely. A new
  graph output pass runs `DisplayTransform.slang`: neutral tone map (Khronos PBR Neutral) then sRGB
  encode, fullscreen triangle into the SDR viewport/readback target. UI pass unchanged.
- The clear color becomes scene-linear at set time (decoded once); the raw-display-space clear
  exception disappears and its documentation follows in T12.
- Exit-gate tests: known-color patches round-trip through the full path within stated tolerance at
  EV 0; the gradient ramp is monotonic with no adjacent step above threshold; the sRGB-encode
  oracle moves its expectations to the display transform; a furnace-style readback samples the HDR
  target directly (values above 1.0 survive).

### T7: reversed-Z depth

- **Files:** modify `Source/Render/Camera.cpp`, `Source/Render/Renderer.cpp`,
  `Shaders/Shadow.slang`, `Shaders/Sky.slang`; tests `Tests/RenderTests.cpp`,
  `Tests/GpuRendererTests.cpp`.
- **Produces:** `Camera::projectionMatrix` becomes reversed infinite-far perspective (near maps to
  1, far to 0); `fitShadowOrtho` reversed to match; depth clears become 0; scene pipelines use
  `DepthCompare::Greater`; the sky pins to the far plane at depth 0 with `GreaterEqual`; the shadow
  sampler compare flips to `GreaterEqual` with the depth bias sign re-tuned by measurement. The
  PCSS parameterization is not reworked here (deferred to M8) but must not gain new code that mixes
  NDC and view-space units.
- Exit-gate test: reconstruct view-space depth from the sampled depth buffer at MaterialLab's depth
  probes and compare against a CPU reference near, mid, and far in the frustum, tolerance stated.
  The pinned projection and shadow unit tests update in the same change.

## Stage 3 — Physically based materials

### T8: deterministic filtered mips

- **Files:** create `Tools/TextureBake/` (small C++ target using stb, added in `xmake.lua`);
  modify `xmake.lua` (setup wiring), `Source/Engine/Scene.cpp`; tests `Tests/EngineAssetTests.cpp`,
  `Tests/EngineSceneTests.cpp`.
- **Produces:** a deterministic offline bake: decode source images, build mips with a linear-light
  box filter (sRGB-aware; normal maps renormalized per level), write DDS with the full chain plus a
  manifest recording source hash, settings, and tool version. `xmake setup` bakes Sponza and Helmet
  images after fetch/convert; `Scene` texture upload prefers the baked DDS (first shipping caller
  of `createTextureFromDds`). `Device::generateMipmaps` loses its last caller and is removed with
  its oracle per the obsolete-path rule.
- Tests first: a synthetic 4×4 image bakes to exact expected filtered values; two runs produce
  byte-identical output; the exit-gate mip check renders MaterialLab's high-frequency pattern at
  strong minification and asserts convergence to its mean color within tolerance (point-picked mips
  fail this).

### T9: full glTF metallic-roughness inputs

- **Files:** modify `Source/Engine/GltfLoader.h/.cpp`, `Source/Engine/Scene.cpp`,
  `Source/Render/Renderer.h/.cpp`, `Shaders/ScenePass.slang`; tests `Tests/EngineGltfTests.cpp`,
  `Tests/CaptureSchemaTests.cpp`.
- **Produces:** the loader reads the metallic-roughness texture, occlusion texture, and emissive
  factor and texture; `render::Material` gains `metallic`, `metallicRoughness`, `occlusion`,
  `emissive` (factor and texture); `fresnelFromMetallic` is deleted — F0 derives in-shader in T10.
  `ObjectUniforms` and its shader mirror change in lockstep with the capture-schema registration
  and its pinned test; the texture slot map is re-budgeted and documented in the Renderer.
- Shading is intentionally unchanged in this task; the observable outcome is authored inputs
  loaded, uploaded with correct color spaces, and visible in the capture schema. In-memory glTF
  fixtures cover each new input; the fetched Helmet case asserts its MR, emissive, and occlusion
  images load.

### T10: deterministic IBL generation

- **Files:** create `Source/Engine/Ibl.h`, `Source/Engine/Ibl.cpp`; modify `Source/Engine/Scene.h`,
  `Source/Engine/Scene.cpp`; tests `Tests/EngineIblTests.cpp`.
- **Produces:** CPU-side deterministic generation at scene build: `computeIrradiance(cubemap)`
  (cosine convolution), `prefilterSpecular(cubemap)` (GGX importance-sampled chain, per-mip
  roughness), `computeDfgLut(size)` (split-sum DFG, `RG16Float`). Fixed sample counts and seeds
  are named constants; outputs upload as `RGBA16Float`/`RG16Float` textures attached to the Scene.
- Tests first: a constant environment yields irradiance equal to its radiance and a constant
  prefiltered chain; DFG LUT corner values match published split-sum references within tolerance;
  two runs are byte-identical.

### T11: GGX direct lighting and IBL shading

- **Files:** rewrite `Shaders/Lighting.slang`; modify `Shaders/ScenePass.slang`,
  `Source/Render/Renderer.cpp`, `Source/Engine/MaterialLab.cpp`; tests `Tests/GpuRendererTests.cpp`,
  `Tests/RenderTests.cpp`.
- **Produces:** GGX normal distribution, height-correlated Smith visibility, Schlick Fresnel with
  `F0 = mix(0.04, baseColor, metallic)`, energy-conserving Lambert diffuse, perceptual roughness
  squared to α; an inverse-transpose normal matrix in `ObjectUniforms` (CPU-computed) with
  orthonormalized tangent frames; diffuse and specular IBL from T10 assets with the DFG split-sum;
  emissive and occlusion applied; the ad-hoc sky-cube reflection term removed. MaterialLab's sphere
  grid becomes the roughness × metallic sweep.
- Exit-gate tests: furnace (uniform white environment, albedo 1, direct lights off) reads back the
  HDR target energy-bounded at 1.0 within tolerance across all spheres; dielectric and conductor
  sphere probes match a CPU-reference BRDF evaluation at pinned angles; CPU-vs-shader edge cases at
  grazing view, roughness limits, and metallic 0/1. Sponza and Helmet render from authored inputs;
  screenshot references regenerate as the new baseline.

## Stage 4 — Close-out

### T12: documentation, evidence, and gate audit

- **Files:** modify `docs/frame-pipeline.md`, `docs/architecture/overview.md`, `AGENTS.md`,
  `README.md`; create `docs/milestones/m4.md`.
- Rewrite the frame walkthrough for the graph-driven HDR frame; update the architecture overview
  and the AGENTS hard rule about the clear color (now scene-linear); refresh public copy within the
  public-facing rule (shipped behavior first, plain future themes, no milestone numbers). Gallery
  captures regenerate with recorded hashes.
- Run the complete exit-gate checklist in one session on Metal 4 hardware and record the evidence
  in `docs/milestones/m4.md`; extract durable decisions to ADRs if any emerged; remove this plan
  from the published baseline per the documentation lifecycle.

## Exit-gate map

| Gate item | Proven by |
|---|---|
| Helmet and Sponza use authored material inputs | T9, T11 |
| mip test passes | T8 |
| furnace and dielectric/conductor tests pass | T11 |
| depth-reconstruction test passes | T7 |
| gradient and known-color tests pass | T6 |
| scene shaders do not manually encode sRGB | T6 |
| undeclared graph use fails validation | T3, T4 |
| unchanged passes match the M3 reference | T4 |
| every pass reports a visible GPU timestamp | T1, T4 |

## Risks

- Metal 4 counter sampling: metal-cpp coverage must be verified before the RHI timing contract is
  shaped; T1 starts with that discovery.
- Binding budget: raising the texture bind capacity happens in T2; T9 owns the final documented
  slot map so the per-draw and per-pass sets never collide.
- The migrated PCSS path carries a known unit bug; T4 preserves it bit-for-bit for the equivalence
  gate, T7 re-tunes only the bias, and the fix itself stays deferred.
- The byte-identical migration test in T4 requires exact clear-color and constant parity; no
  image-affecting change may land between T4's start and its equivalence gate.
- FP16 readback precision: furnace and known-color tolerances are stated per test against
  half-precision quantization, not guessed.
