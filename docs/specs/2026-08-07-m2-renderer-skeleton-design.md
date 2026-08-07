# M2 — Renderer Skeleton — Design Spec

**Date**: 2026-08-07
**Status**: Implemented — 2026-08-07
**Parent spec**: [2026-08-07-luminex-upgrade-design.md](2026-08-07-luminex-upgrade-design.md) (§8, M2)
**Inputs**: [docs/plans/m2-backlog.md](../plans/m2-backlog.md) (all items triaged into scope, see §8)

## 1. Purpose & scope

M2 turns `lmx::render` from a placeholder into a real module: mesh/camera/uniform plumbing, a
depth buffer, and an ImGui overlay — per the parent spec. Scope decisions made for this
milestone:

- **Scene**: procedural ground plane + 3 cubes (one rotating). No asset loading — that is M3.
- **Camera**: fly camera — right-mouse-drag to look, WASD + Q/E (down/up) to move while
  dragging.
- **UI**: ImGui **with docking** — Unity-like default layout: central Viewport window, right-side
  Inspector panel (stats, camera, per-object transforms). Layout re-dockable at runtime,
  persisted by imgui.ini.
- **Viewport**: the scene renders to an **offscreen render target** (color + depth) that the
  Viewport dock window displays as an image ("true editor viewport"). Chosen over a passthru
  dockspace because it is the layout actually asked for, and its hard parts — render-to-texture,
  explicit barrier, native-texture handoff — are exactly the machinery M3 shadow mapping needs,
  arriving one milestone early with a trivial scene to debug on.
- **Backlog**: every item in `m2-backlog.md` is in scope (§8).

Non-goals: scene graph, materials, textures, asset loading, lighting beyond one fixed
directional light, left-side hierarchy panel (earns its place with M3 content).

## 2. Frame flow

1. App pumps SDL events → ImGui SDL3 backend first; camera input only while the Viewport window
   is active (right-mouse-drag).
2. ImGui builds the UI: fullscreen dockspace, Inspector docked right, central Viewport window
   reports its panel size and displays the scene color texture.
3. Viewport-size changes are **debounced**: during a dock drag the old render target stretches;
   once the size is stable for a few frames, `waitIdle` + recreate. No deferred-release
   machinery is added for this — a dock drag does not justify it (revisit when something does).
   The recreate runs at the **top** of the frame, before the UI records the Viewport image's
   texture ID for that frame — recreating after that recording would hand ImGui's draw list an
   identifier for a texture that no longer exists (Task 10, Amendment A5b).
4. `Device::beginFrame()` → **scene pass** into the offscreen RT (clear color + depth, draw
   items, depth-tested) → **textureBarrier** (scene color: render-target write → fragment read)
   → **UI pass** onto the swapchain drawable (ImGui draw data; the Viewport window samples the
   scene texture) → `endFrame(present)`.

`App --screenshot` and the GPU smoke test run the scene pass only (fixed size, no ImGui, no
swapchain), unchanged in shape from M1.

## 3. `lmx::render` components

All SDL-free and Metal-free; `Source/Render/`.

| Unit | Responsibility |
|---|---|
| `Camera` | Fly-camera state (position, yaw/pitch, fov, near/far) → `viewMatrix()`, `projectionMatrix(aspect)`. Consumes abstract inputs (move vector, look delta) — the App owns SDL mapping. Unit-testable. |
| `Mesh` | GPU mesh: vertex + index buffer handles, index count. Procedural factories `makeCube()` / `makePlane()` emit positions, normals, colors (CPU data) uploaded via `Device`. |
| `Renderer` | Owns scene color+depth targets (BGRA8Unorm + D32Float), the mesh pipeline, and `render(CommandList&, const Camera&, span<const DrawItem>, bool barrierForSampling = true)` with `DrawItem = {const Mesh*, glm::mat4 model, glm::vec4 baseColor}`. Explicit `resize(w, h)`; the App debounces. |

No scene graph, no material system — a draw list is the whole abstraction (ADR 0004).

**Amendment (2026-08-07, Task 8): `render()` gained `bool barrierForSampling = true`.** As
originally specified, `render()` always ended with the `RenderTarget → ShaderRead` barrier — but
that is unsatisfiable for an offscreen-only frame: `textureBarrier` records the barrier as pending
and `endFrameReset` aborts if no later pass consumes it (§5), so a bare `render()` + `endFrame()`
is a guaranteed abort (verified by running it). Requiring every caller to add a dummy consuming pass
just to satisfy the assert would burden the `--screenshot` path, which has no UI pass either. So the
barrier became a parameter, default `true` (the App path, which always has a UI pass to consume
it); `false` means "no later pass samples the target this frame."

## 4. RHI growth (`RHI.h`)

Each addition is demanded by a concrete M2 feature:

- `CommandList::setUniforms(uint32_t slot, const void* data, uint64_t size)` — copies into a
  backend-owned per-frame transient uniform ring, binds `{buffer, offset}` at the
  argument-table slot. Ring overflow is a fatal `LMX_ASSERT` (grow the constant when hit).
- `CommandList::drawIndexed(Buffer& indexBuffer, uint32_t indexCount, uint32_t firstIndex = 0)`
  — indices are uint32 only; Metal 4 takes the index buffer per draw, so no bind state is
  modeled.
- `CommandList::textureBarrier(Texture&, TextureUse from, TextureUse to)` with
  `enum class TextureUse { RenderTarget, ShaderRead }`. The "explicit barriers" v1 omission
  ends here (multi-pass now exists); the RHI.h omissions comment updates accordingly.
- `CommandList::bindTexture(uint32_t slot, Texture& texture)` — binds a texture for shader reads
  at an argument-table **texture** slot (its own index space; texture slot 0 and buffer slot 0
  coexist). Valid only inside a render pass; the texture must have been created with
  `sampled = true`. See the amendment below.
- `RenderPassDesc` += `Texture* depthTarget = nullptr`, `float clearDepth = 1.0f`.
- `GraphicsPipelineDesc` += `Format depthFormat` (`Unknown` = no depth attachment),
  `bool depthTestEnable`, `bool depthWriteEnable`.
- `TextureDesc` += `bool sampled = false` (scene color RT is `renderTarget + sampled`).
- `validate(TextureDesc)` += reject `renderTarget` with a format that is neither
  color-renderable nor depth (closes the M1 abort-on-nil door — backlog).
- Vertex data stays **bindless vertex-pulling** (`StructuredBuffer` at slot 0) as in M1; no
  vertex-layout descriptors are introduced.

ImGui's native needs (`MTLTexture` behind `ImTextureID`, the render encoder) do not appear in
`RHI.h` — they live in backend-internal `Metal4ImGui.h` (§5), following the `Metal4Capture`
precedent.

**Amendment (2026-08-07, Task 6): `bindTexture` added.** The list above shipped without it, on the
assumption that the only M2 consumer of a sampled texture was ImGui's Viewport image — and ImGui
samples through its own Metal 4 backend, not through `RHI.h`. That assumption leaves §7's
"barrier path — render → barrier → sample → readback" GPU test **inexpressible through this RHI**:
there is no way to bind the scene render target for a shader read, so the one edge `textureBarrier`
implements would ship with no executable proof. `bindTexture` is the argument-table texture half
that closes it, and is the same call M3's shadow-map sampling needs. Added per the same rule as the
rest of this section — one concrete feature demands it — rather than as speculative surface.

## 5. Metal 4 backend changes

- **One argument table per frame in flight** (backlog hard gate): `m_argumentTable` becomes
  `std::array<…, kFramesInFlight>`; `beginFrame` selects the frame's table. Lands **before**
  the first dynamic binding so the documented data race never exists.
- **Uniform ring**: one buffer per frame in flight (256 KiB initial), bump-allocated by
  `setUniforms`, reset in `beginFrame`, offsets 256-byte aligned (alignment requirement
  verified against Metal docs during implementation). Ring buffers join the residency set at
  creation.
- **Barrier**: `textureBarrier` maps to Metal 4's stage-scoped barrier between the two render
  passes in the long-lived command buffer (fragment-read after render-target write; reference:
  Apple `managing-metal4-synchronization`).
- **Per-draw autorelease pool removed** (backlog): pools move to frame scope before draw counts
  grow from 1 to N.
- **Texture usage** derives from desc: `renderTarget` + format class picks color vs depth
  attachment usage; `sampled` adds shader-read.
- **`Metal4ImGui.{h,cpp}`**: wraps imgui's Metal 4 backend (init/new-frame/render) against our
  device and command list; exposes `imguiTextureID(Texture&)` for the Viewport image. The SDL3
  platform half of imgui is API-agnostic and called by the App directly.

## 6. Shaders

`Shaders/Mesh.slang`: `StructuredBuffer<Vertex>` (packed pos/normal/color) at slot 0;
`ConstantBuffer<ObjectUniforms> { float4x4 mvp; float4x4 model; float4 baseColor; }` at slot 1;
one `setUniforms` per draw item. Fragment: lambert against a fixed directional light (normals
keep the cubes readable). Same slang→MSL→metallib rule; the slang `device`-pointer warning
(backlog, upstream) carries over, still tracked.

**Amendment (2026-08-07, Task 10): `Triangle.slang` survives, as a test oracle.** This section
originally said "`Triangle.slang` retires with the triangle." It does not: three GPU cases in
`Tests/GpuSmokeTests.cpp` (uniform-ring offset, uniform-ring slot reuse across frames, depth-test
rejection of a coplanar draw) render it as their *oracle*, because a three-vertex shader's expected
image is derivable by hand — which is what makes a dropped ring offset or a dead depth test surface
as a wrong *colour* rather than a slightly different lambert term. `Mesh.slang` cannot do that job
without becoming a second copy of `Triangle.slang`, so the original shader stays. What did retire:
the M1 GPU case `offscreen triangle renders expected pixels` (superseded by the Renderer cases) and
every `TriangleAssets` remnant in the App.

## 7. Testing & CI

- **Unit** (`Tests/unit`, CI-run): camera matrices (known pose → expected view/proj), mesh
  factories (counts, winding, AABB), new validation rules, `Format::Unknown` pin with
  `contains("Unknown")` (backlog), capture-guard test via `MTL_CAPTURE_ENABLED` plumbing
  (backlog).
- **GPU smoke** (local gate, as M1): offscreen scene render — corner pixel == clear color,
  center != clear color; **depth-order test** — near and far geometry drawn far-first and
  near-first must both resolve to the near color; **barrier path** — render → barrier → sample
  → readback.
- **App nits** (backlog): skip bare `--` in arg parsing; bounds-check `logPixel`.
- **CI**: clang-tidy non-blocking job (closes the D8 deferral), `~/.xmake/packages` cache,
  shader-outdir rule guard (backlog). Format + unit-test jobs unchanged.

## 8. Backlog disposition (from m2-backlog.md)

Every item is in scope for M2: argument table per frame (§5), renderTarget format validation
(§4), `Format::Unknown` pin (§7), shader-outdir rule guard (§7), capture-guard test (§7),
per-draw autorelease pool (§5), arg-parsing/logPixel nits (§7). Device-dtor pool ordering and
`newFunction()` function-constants notes remain watch-items (nothing in M2 changes them). The
slang direct-metallib collapse (ADR 0003) stays blocked on upstream issues #12325/#12096.

## 9. Dependencies & risks

- xrepo `imgui` must provide **docking + SDL3 + Metal 4 backends together** (docking is
  normally a version tag/config). Verifying the exact combination is implementation task #1;
  fallback is pinning imgui into `ThirdParty/` via `xmake setup`, which the parent spec allows
  for non-xrepo deps.
- New xrepo dep: `imgui` only. `glm` (already in M1's dep list) gets its first real use.

## 10. Definition of Done

`xmake run App` opens the docked layout — Viewport shows the lit scene, fly camera works,
Inspector edits apply live; `xmake test` green including new unit + GPU depth/barrier tests;
format/tidy clean; CI green with the new tidy + cache jobs; every §8 backlog item closed or
explicitly re-deferred with a reason; parent spec §8 status, CLAUDE.md, and README updated at
the milestone boundary (CLAUDE.md update policy).

**As of 2026-08-07:** all automated gates are green — `xmake format --check`, `xmake -y`,
`xmake test`, and `MTL_DEBUG_LAYER=1 LMX_MAX_FRAMES=300 xmake run App` (validation-clean, exercises
the resize drills). **Interactive verification is pending Rudy's closing gate** — fly-around,
dock-drag, Inspector edits, and CI green on the PR — and is not claimed by this document.
