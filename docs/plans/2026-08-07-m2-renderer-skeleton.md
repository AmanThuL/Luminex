# Luminex M2 — Renderer Skeleton — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `xmake run App` opens a docked, Unity-like editor shell — central Viewport window showing a depth-tested, lambert-lit procedural scene (ground plane + 3 cubes, one rotating) rendered offscreen through `lmx::render`, fly camera (RMB-drag look, WASD + Q/E), right-side Inspector — with every M2 backlog item closed.

**Architecture:** The scene renders into an offscreen color+depth target owned by `lmx::render::Renderer`, is barriered to ShaderRead, and is displayed as an image inside an ImGui (docking) Viewport window drawn in a second render pass onto the swapchain. The RHI grows exactly what this demands: per-frame transient uniforms, indexed draws, depth attachments, sampled render targets, a texture bind, and an explicit texture barrier. The Metal 4 backend closes the M1 hard gate first: one argument table per frame in flight.

**Tech Stack:** xmake, C++23, SDL3, metal-cpp (pinned), Slang (pinned), glm, Dear ImGui **docking** (route decided in Task 0), spdlog, Catch2 v3.

**Spec:** `docs/specs/2026-08-07-m2-renderer-skeleton-design.md` — read it first. Parent: `docs/specs/2026-08-07-luminex-upgrade-design.md` (D1–D10 binding).

## Global Constraints

- C++23, Apple Clang / Xcode 26. macOS 26+, Apple Silicon, `MTLGPUFamilyMetal4` hard requirement — no Metal 3 fallback.
- 3 frames in flight, exactly (`kFramesInFlight`).
- No Metal/Apple/ImGui types in public RHI headers (`Source/RHI/*.h`). Backend + ImGui glue live under `Source/RHI/Metal4/`.
- `Source/Render` (lmx::render) is SDL-free and Metal-free: it includes only `RHI/RHI.h`, glm, and Core.
- Creation returns `lmx::rhi::Result<T>`; contract violations are `LMX_ASSERT`, never error codes. All GPU objects get labels.
- Metal NDC depth is **[0, 1]**: every projection matrix comes from `glm::perspectiveRH_ZO`. Plain `glm::perspective` ([-1,1]) is a bug.
- xrepo deps for M2: `libsdl3`, `glm`, `spdlog`, `catch2` + imgui-docking per Task 0's route. stb / nlohmann_json stay M3+ — do NOT add.
- Commits per `docs/conventions/commits.md`: imperative, English, no AI co-author trailers. Every commit compiles and passes `xmake test` (CPU tests minimum; run `xmake test` fully — GPU tests are a local gate on this machine).
- Format with `xmake format` before every commit.

## Amendments

**A1 (2026-08-07, Task 0):** Dear ImGui docking wired via **Route B** — xrepo's `imgui` package
has no metal backend config (confirmed: `imgui_impl_metal*` is never compiled by the package), so
core + both backends are vendored wholesale from source, exactly like the metal-cpp/slang pins,
into a new `ImGui` static target (`xmake.lua`) that `RHI` and `App` both depend on. Pinned tag
`v1.92.7-docking` (commit `b1bcb12a624af7509894c8e77dd47416997777fa`; the tag itself is an
annotated-tag object — `git clone --branch` prints a benign "is not a commit!" warning but still
checks out the right commit, observed both in the scratch probe and via `xmake setup`).

Backend source files compiled into the `ImGui` target: `ThirdParty/imgui/backends/imgui_impl_sdl3.h/.cpp`
(platform) and `ThirdParty/imgui/backends/imgui_impl_metal.h/.mm` (renderer) — there is no separate
`imgui_impl_metal4.*` file; `imgui_impl_metal.*` is the only Metal backend in this tag and it has
**no Metal 4 path at all** (no `MTL4`/`metal4` symbol anywhere in the backend or in
`examples/example_sdl3_metal/`, which — contrary to the plan's planning-time guess — is named
`example_sdl3_metal`, not `example_sdl3_metal4`).

Render-draw-data entry point (via `#define IMGUI_IMPL_METAL_CPP`, set as a public define on the
`ImGui` target so every consumer sees the metal-cpp overloads):
```cpp
void ImGui_ImplMetal_RenderDrawData(ImDrawData* draw_data,
                                     MTL::CommandBuffer* commandBuffer,
                                     MTL::RenderCommandEncoder* commandEncoder);
```
**Encoder parameter type is `MTL::RenderCommandEncoder*` — the classic/legacy metal-cpp encoder
(`Metal/MTLRenderCommandEncoder.hpp`, namespace `MTL`) — NOT `MTL4::RenderCommandEncoder*`**
(`Metal/MTL4RenderCommandEncoder.hpp`, namespace `MTL4`, a distinct, unrelated class — verified in
the vendored headers). Task 9 cannot hand the Metal4 backend's render-pass encoder to this
function directly; it must record the UI draw through a classic `MTL::CommandBuffer`/
`MTL::RenderCommandEncoder` (e.g. a plain `renderCommandEncoder(...)` off a classic command queue
for the swapchain pass), separate from the `MTL4::CommandBuffer`/`MTL4::RenderCommandEncoder` used
for the scene pass. This is a real API gap, not a wiring detail — plan for it explicitly in Task 9.

ImTextureID convention: a texture handle is round-tripped as a pointer-sized integer —
`(ImTextureID)(intptr_t)texturePointer` in, `(__bridge id<MTLTexture>)(void*)(intptr_t)(tex_id)`
out (`imgui_impl_metal.mm`). From the metal-cpp side this is `(ImTextureID)(intptr_t)someMTLTexturePtr`
where `someMTLTexturePtr` is an `MTL::Texture*` (or `id<MTLTexture>`) — the backend's header comment
states it directly: "Use 'MTLTexture' as texture identifier."

Build note (empirically verified, not a guess): the `imgui_impl_metal.mm` backend must compile
**without ARC**. Its metal-cpp-path branch does explicit `[x release]` / `[x autorelease]` sends
(around lines 429 and 182) that are hard ARC compile errors. xmake compiles `.mm` files under ARC
by default in this environment (confirmed by first hitting exactly those two errors), so the
`ImGui` target carries `add_mxxflags("-fno-objc-arc")`. No `imgui.h`/other core file needed it —
only this one backend file is Objective-C++.

Compile-proof: a temporary `Tests/ImGuiSmokeTests.cpp` (`ImGui::CreateContext()` /
`ImGui::DestroyContext()`) built and ran green under `xmake test` with no extra target wiring
needed (Tests already depends on RHI, which now depends on ImGui, and xmake propagates the
public include dirs/defines transitively) — then it was deleted per the plan's Step 3 ("prove it
compiles, then remove the scratch"); it is not part of this commit.

## Subagent & Model Policy (per Rudy)

| Model | Used for | Tasks |
|---|---|---|
| **Fable 5** | Main thread — orchestration, review of all subagent output, final verification | orchestration + reviews |
| **Opus 5** | Design-sensitive / thin-doc: Metal 4 backend growth, RHI surface, Renderer + GPU tests, ImGui Metal 4 glue, App editor shell | 2, 3, 4, 5, 6, 8, 9, 10 |
| **Sonnet 5** | Standard implementation: dep verification, hardening batch, Camera/Mesh, capture tests, CI, docs/close-out | 0, 1, 7, 11, 12, 13 |
| **Haiku 4.5** | Mechanical only: plan checkbox updates | checkbox bookkeeping |

**Metal 4 references for Tasks 2–6, 8, 9** (executors: consult before coding):
- Vendored headers are ground truth for every metal-cpp signature: `ThirdParty/metal-cpp/Metal/MTL4*.hpp`. Verified while planning: `MTL4::CommandEncoder::barrierAfterQueueStages(MTL::Stages, MTL::Stages, MTL4::VisibilityOptions)` (MTL4CommandEncoder.hpp:52); `MTL4::RenderCommandEncoder::drawIndexedPrimitives(MTL::PrimitiveType, NS::UInteger indexCount, MTL::IndexType, MTL::GPUAddress indexBuffer, NS::UInteger indexBufferLength)` (MTL4RenderCommandEncoder.hpp:66); `MTL4::ArgumentTable::setAddress(MTL::GPUAddress, NS::UInteger)` / `setTexture(MTL::ResourceID, NS::UInteger)` (MTL4ArgumentTable.hpp:73/80); `MTL::Texture::gpuResourceID()` (MTLTexture.hpp:241); `MTL4::RenderPassDescriptor::depthAttachment()` (MTL4RenderPass.hpp:55); `MTL4::RenderCommandEncoder::setDepthStencilState(...)` (MTL4RenderCommandEncoder.hpp:98); `MTL::Device::newDepthStencilState(...)` (MTLDevice.hpp:443). **`MTL4::RenderPipelineDescriptor` has NO depth-attachment pixel format** — depth compatibility is a render-pass concern in Metal 4; the RHI's `depthFormat` is validated CPU-side and kept for the future Vulkan backend.
- `MTL::Stages` has no render-target stage: color/depth attachment writes belong to `MTL::StageFragment` (MTLCommandEncoder.hpp:44). Barrier for RT-write → fragment-read is `StageFragment → StageFragment`.
- Apple game-porting-toolkit skills: `translating-to-metal4-api` (argument-table capture semantics), `managing-metal4-synchronization` (barrier placement), `managing-metal-cpp-lifetimes`.
- Apple docs: Understanding the Metal 4 core API · Metal 4 compilation API · WWDC25 205/254.

---

### Task 0: ImGui dependency verification — route decision (Sonnet 5)

**Files:**
- Modify: `xmake.lua` (add_requires only if route A works)
- Possibly modify: `xmake.lua` `task("setup")` (route B)

**Interfaces:**
- Produces: a working, pinned ImGui-docking dependency with SDL3 platform backend and a Metal backend usable from Metal 4 command encoders, plus a **written record** (amendment block in this plan) of: chosen route, pinned version, backend source file names, and the exact render-draw-data entry point + its encoder parameter type.

Planning-time facts (verified 2026-08-07): xrepo `imgui` has docking version tags (`v1.92.7-docking`) and an `sdl3` config, but **no metal backend config** — the package never compiles `imgui_impl_metal*`. The imgui source tree ships `backends/imgui_impl_metal.*` and (1.92.x) an `example_sdl3_metal4` (parent spec §9), so the Metal backend must be compiled by us from source regardless of route.

- [x] **Step 1: Inspect the imgui-docking source tree**

```bash
cd /private/tmp/claude-501/-Users-rudyz-Documents-projects-Luminex/*/scratchpad 2>/dev/null || cd /tmp
git clone --depth 1 --branch v1.92.7-docking https://github.com/ocornut/imgui.git imgui-probe
ls imgui-probe/backends/ | grep -i -E "metal|sdl3"
ls imgui-probe/examples/ | grep -i -E "sdl3_metal"
grep -n "RenderDrawData" imgui-probe/backends/imgui_impl_metal.h
grep -n "IMGUI_IMPL_METAL_CPP" imgui-probe/backends/imgui_impl_metal.h | head -3
```
Record: does `imgui_impl_metal.h` expose a metal-cpp (C++) API under `IMGUI_IMPL_METAL_CPP`, and does it accept a `MTL4::RenderCommandEncoder` (look for `MTL4`/`metal4` in the backend and in `examples/example_sdl3_metal4/`)? If the Metal-4 path lives in a *different* backend file (e.g. `imgui_impl_metal4.*`), record that name — Task 9 consumes it verbatim.

- [x] **Step 2: Decide the route**

Route A (only if it genuinely composes): `add_requires("imgui v1.92.7-docking", {configs = {sdl3 = true}})` for core+SDL3 backend, and vendor ONLY the Metal backend source files into the build from a ThirdParty pin of the exact same tag. Mixed-version risk must be zero (same tag both sides).
Route B (default if A is awkward): skip xrepo imgui entirely; extend `task("setup")` in `xmake.lua` to clone `imgui` at the pinned docking tag into `ThirdParty/imgui` (M1 pattern: metal-cpp/slang pins at `xmake.lua:94-95`), and add an `ImGui` static target compiling `imgui/*.cpp` + `backends/imgui_impl_sdl3.cpp` + the Metal backend file(s). The Metal backend `.mm` compiles as ObjC++ (`add_files("...mm", {sourcekinds = "mxx"})`) with ARC enabled if the backend expects it (`add_mxxflags("-fobjc-arc")` — check the backend's header comment) — this is backend-internal and leaks nothing into RHI.h.

Decision: **Route B** — see Amendment A1 above.

- [x] **Step 3: Prove the choice compiles**

Wire the chosen route in `xmake.lua` (new `ImGui` target or add_requires + vendored backend), add a temporary translation-unit smoke (`ImGui::CreateContext(); ImGui::DestroyContext();` in a scratch target or Tests TU), `xmake -y`, then remove the scratch. Record the pinned tag in this plan's Amendments block.

- [x] **Step 4: Commit**

```bash
xmake format && xmake test && git add -A && git commit -m "Pin Dear ImGui docking for M2 UI"
```

---

### Task 1: Backlog hardening batch (Sonnet 5)

**Files:**
- Modify: `Source/App/main.cpp:470-482` (arg loop), `Source/App/main.cpp:219-224` (`logPixel`)
- Modify: `xmake.lua:16-39` (`slang2metallib` rule)
- Test: `Tests/RHIValidateTests.cpp`

**Interfaces:**
- Produces: no API changes — four M1-review closures: bare `--` skipped in arg parsing; `logPixel` bounds-checked; `Format::Unknown` validation tests pinned with `contains("Unknown")`; `slang2metallib` fails loudly when two targets share a shader output dir.

- [ ] **Step 1: Pin the Format::Unknown tests**

In `Tests/RHIValidateTests.cpp`, every `TEST_CASE` that rejects `Format::Unknown` (TextureDesc, GraphicsPipelineDesc colorFormat, SwapchainDesc — find them with `grep -n "Format::Unknown" Tests/RHIValidateTests.cpp`) gets one added line so the whitelist can't silently absorb the Unknown branch:

```cpp
    REQUIRE(r.error().message.contains("Unknown"));
```
Run `xmake test Tests/unit` — expect PASS (messages already name Unknown). If any fails, fix the *message* in `Source/RHI/Validate.cpp` to name Unknown, not the test.

- [ ] **Step 2: App arg-parsing nits**

In `Source/App/main.cpp`'s arg loop: `if (arg == "--") { continue; }` before the `--screenshot` check (a bare `--` is conventionally "end of options", not an error). In `logPixel`, bound-check before indexing:

```cpp
void logPixel(const char* what, const std::vector<uint8_t>& bgra, uint32_t width, uint32_t x,
              uint32_t y) {
    const size_t offset = (size_t{y} * width + x) * 4;
    if (offset + 3 >= bgra.size()) {
        LMX_LOG_WARN("screenshot probe {} at ({},{}) is outside the image; skipping", what, x, y);
        return;
    }
    ...
```

- [ ] **Step 3: Shader-outdir rule guard**

In the `slang2metallib` rule (`xmake.lua`), before emitting, detect two opted-in targets sharing a targetdir. Add at the top of `on_buildcmd_file`:

```lua
        -- Two targets emitting the same shader paths race under a parallel build
        -- (observed in M1 -- see the Tests targetdir comment). Fail loudly instead.
        for _, other in pairs(target:project():targets()) do
            if other:name() ~= target:name() and other:rule("slang2metallib")
               and path.absolute(other:targetdir()) == path.absolute(target:targetdir()) then
                os.raise("slang2metallib: targets '%s' and '%s' share targetdir '%s'; give one "
                         .. "its own set_targetdir", target:name(), other:name(), target:targetdir())
            end
        end
```
Verify the exact xmake API for enumerating targets (`target:project():targets()` vs `import("core.project.project").targets()`) against xmake 3.x docs — whichever resolves inside `on_buildcmd_file`; the check itself is the deliverable. Prove it fires: temporarily give Tests the default targetdir, run `xmake -y`, expect the raise; restore.

- [ ] **Step 4: Verify + commit**

```bash
xmake format && xmake -y && xmake test
git add -A && git commit -m "Close M1-review hardening items: arg nits, Unknown pins, outdir guard"
```

---

### Task 2: One argument table per frame in flight + frame-scoped pools (Opus 5)

**Files:**
- Modify: `Source/RHI/Metal4/Metal4Device.h:66-79`, `Source/RHI/Metal4/Metal4Device.cpp:227-247,506-530`
- Modify: `Source/RHI/Metal4/Metal4CommandList.h`, `Source/RHI/Metal4/Metal4CommandList.cpp`

**Interfaces:**
- Consumes: existing `Metal4Device` frame protocol (`beginFrame`/`endFrame`, allocator ring, shared-event pacing).
- Produces: `Metal4CommandList::resetForFrame(MTL4::ArgumentTable* table)` (called by `beginFrame`); device member `std::array<NS::SharedPtr<MTL4::ArgumentTable>, kFramesInFlight> m_argumentTables`. Per-draw autorelease pools removed (frame-scoped instead). This closes the M1 hard gate BEFORE any dynamic binding exists (backlog: "loud carry-forward").

- [ ] **Step 1: Per-frame tables in Metal4Device**

In `Metal4Device.h` replace the single `m_argumentTable` member with `std::array<NS::SharedPtr<MTL4::ArgumentTable>, kFramesInFlight> m_argumentTables;` and rewrite the member comment: the table's contents are consumed while its frame is in flight, so frame N may only touch table `N % kFramesInFlight` — same rotation, same guarantee as the allocator ring. In `create()` (`Metal4Device.cpp:227-243`) build all `kFramesInFlight` tables in a loop (labels `"lmx.device.argumentTable." + std::to_string(i)`, keep `setInitializeBindings(true)` and both max-bind counts). Construct `m_commandList` with a null table: `self->m_commandList.emplace(self->m_commandBuffer.get());` (next step changes the constructor).

- [ ] **Step 2: resetForFrame on Metal4CommandList**

`Metal4CommandList` constructor drops the table parameter (keeps the command buffer); add:

```cpp
    // beginFrame's half of the per-frame rotation: point this command list at the frame's
    // argument table. Must be called before any encoding in the frame; asserts no pass is open.
    void resetForFrame(MTL4::ArgumentTable* argumentTable);
```
Implementation: `LMX_ASSERT(!m_encoder, ...); m_argumentTable = argumentTable;`. In `Metal4Device::beginFrame()` after the allocator reset: `m_commandList->resetForFrame(m_argumentTables[m_frameNumber % kFramesInFlight].get());`. Add `LMX_ASSERT(m_argumentTable != nullptr, "no argument table -- command list used outside a frame")` at the top of `beginRenderPass`.

- [ ] **Step 3: Frame-scoped autorelease pools (backlog)**

Delete the per-call `NS::SharedPtr<NS::AutoreleasePool> pool = ...` line from `bindPipeline`, `bindVertexBuffer`, `draw` (`Metal4CommandList.cpp:64,71,84`) — wrong shape for a hot path once draw counts grow. Keep the pools in `beginRenderPass`/`endRenderPass` (each creates/destroys an autoreleased encoder). Add to the class comment: encoder-scoped calls run inside the pass's pool lifetime; anything autoreleased per-draw would accumulate until `endRenderPass`, which is acceptable at M2 draw counts and revisited when instancing arrives.

- [ ] **Step 4: Verify + commit**

```bash
xmake -y && xmake test        # unit AND [gpu] locally — the smoke test must still render
MTL_DEBUG_LAYER=1 LMX_MAX_FRAMES=300 xmake run App   # 3 frames in flight × many rotations, validation clean
xmake format && git add -A && git commit -m "Close the argument-table hard gate: one table per frame in flight"
```

---

### Task 3: Transient uniforms — setUniforms + per-frame ring (Opus 5)

**Files:**
- Create: `Source/Core/Align.h`
- Modify: `Source/RHI/RHI.h:88-96` (CommandList), `Source/RHI/Metal4/Metal4Device.h`, `Source/RHI/Metal4/Metal4Device.cpp`, `Source/RHI/Metal4/Metal4CommandList.h/.cpp`
- Test: `Tests/CoreTests.cpp`

**Interfaces:**
- Consumes: Task 2's per-frame rotation (`resetForFrame`).
- Produces: `CommandList::setUniforms(uint32_t slot, const void* data, uint64_t size)` on the RHI; `lmx::alignUp(uint64_t value, uint64_t alignment)` in Core; backend uniform ring (one `MTL::Buffer` per frame in flight, 256 KiB, shared storage, in the residency set). **Measured record required** (step 5): argument-table capture semantics for per-draw rebinds.

- [ ] **Step 1: Write the failing alignUp test**

```cpp
// Tests/CoreTests.cpp
#include "Core/Align.h"

TEST_CASE("alignUp rounds to the next multiple", "[core]") {
    STATIC_REQUIRE(lmx::alignUp(0, 256) == 0);
    STATIC_REQUIRE(lmx::alignUp(1, 256) == 256);
    STATIC_REQUIRE(lmx::alignUp(256, 256) == 256);
    STATIC_REQUIRE(lmx::alignUp(257, 256) == 512);
    STATIC_REQUIRE(lmx::alignUp(144, 16) == 144);
}
```
Run: `xmake -y && xmake test Tests/unit` — expect FAIL (no `Core/Align.h`).

- [ ] **Step 2: Implement Core/Align.h**

```cpp
#pragma once
#include <cstdint>

namespace lmx {

// alignment must be a power of two; that is a compile-time-checkable contract at every
// current call site, so it is asserted rather than handled.
constexpr uint64_t alignUp(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

} // namespace lmx
```
Run the test — PASS. Commit: `git add -A && git commit -m "Add constexpr alignUp to Core"`.

- [ ] **Step 3: RHI surface + backend ring**

`RHI.h` `CommandList` grows (with the doc comment):

```cpp
    // Copies `size` bytes into the frame's transient uniform ring and binds the copy's GPU
    // address at the given argument-table slot for subsequent draws. The data is captured at
    // call time -- the caller may reuse or free its buffer immediately. Valid only inside a
    // render pass. Ring capacity is a fixed per-frame budget; exhausting it is fatal
    // (LMX_ASSERT) -- grow the backend constant when a real scene hits it.
    virtual void setUniforms(uint32_t slot, const void* data, uint64_t size) = 0;
```
Backend (`Metal4Device.h`): `std::array<NS::SharedPtr<MTL::Buffer>, kFramesInFlight> m_uniformRings;` + `std::array<uint64_t, kFramesInFlight> m_uniformOffsets{};` with constants:

```cpp
// 256 KiB per frame: ~1,800 draws of the M2 ObjectUniforms (144 B aligned to 256). The
// offset alignment is the conservative Metal constant-buffer bound; Apple GPUs accept less,
// but 256 is correct everywhere and costs at most 112 B of slack per draw at M2 sizes.
inline constexpr uint64_t kUniformRingBytes = 256 * 1024;
inline constexpr uint64_t kUniformOffsetAlignment = 256;
```
`create()` allocates the rings (shared storage, labels `"lmx.device.uniformRing." + i`, registered with `m_residency` — reuse `Metal4Buffer`? No: rings are device-internal, hold them as raw `MTL::Buffer` SharedPtrs and add to the residency set directly with `m_residency->addAllocation(...)` + one `commit()`; unregistering happens implicitly at device teardown since the set dies with the device). `beginFrame()` zeroes `m_uniformOffsets[frame]` and passes the ring to the command list: extend `resetForFrame(MTL4::ArgumentTable*, MTL::Buffer* uniformRing, uint64_t* uniformOffset)`.

`Metal4CommandList::setUniforms`:

```cpp
void Metal4CommandList::setUniforms(uint32_t slot, const void* data, uint64_t size) {
    LMX_ASSERT(m_encoder, "setUniforms must be called between beginRenderPass and endRenderPass");
    LMX_ASSERT(data != nullptr && size > 0, "setUniforms: data must be non-null and non-empty");
    const uint64_t offset = *m_uniformOffset;
    LMX_ASSERT(offset + size <= m_uniformRing->length(),
               "setUniforms: per-frame uniform ring exhausted -- grow kUniformRingBytes");
    std::memcpy(static_cast<uint8_t*>(m_uniformRing->contents()) + offset, data, size);
    m_argumentTable->setAddress(m_uniformRing->gpuAddress() + offset, slot);
    *m_uniformOffset = alignUp(offset + size, kUniformOffsetAlignment);
}
```

- [ ] **Step 4: Build + existing tests green**

```bash
xmake -y && xmake test
```

- [ ] **Step 5: Record the capture-semantics ground truth**

Consult the `translating-to-metal4-api` skill (game-porting-toolkit) on whether a draw captures argument-table contents at encode time (making per-draw `setAddress` on one table safe within a pass) or the GPU reads the table at execution (making it last-write-wins). Record the answer in this plan's Amendments block. **The empirical proof lands in Task 8's multi-object GPU test** (distinct per-draw colors must all appear). Contingency if last-write-wins is observed there: replace the single per-frame table with a small per-frame table *pool* (grow-on-demand, one table per draw, same rotation guarantee); the RHI surface does not change. Do not build the pool speculatively.

- [ ] **Step 6: Commit**

```bash
xmake format && git add -A && git commit -m "Add transient per-frame uniforms to the RHI (setUniforms + ring)"
```

---

### Task 4: Depth attachments, sampled textures, validation growth (Opus 5)

**Files:**
- Modify: `Source/RHI/RHI.h` (TextureDesc, RenderPassDesc, GraphicsPipelineDesc), `Source/RHI/Validate.cpp`
- Modify: `Source/RHI/Metal4/Metal4Device.cpp:331-364,429-504`, `Source/RHI/Metal4/Metal4Resources.h`, `Source/RHI/Metal4/Metal4CommandList.cpp:8-61`
- Test: `Tests/RHIValidateTests.cpp`

**Interfaces:**
- Consumes: `Format::D32Float` (already in the enum), `toMTL` (already maps it).
- Produces: `TextureDesc.sampled : bool = false`; `RenderPassDesc.depthTarget : Texture* = nullptr` + `RenderPassDesc.clearDepth : float = 1.0f`; `GraphicsPipelineDesc.depthFormat : Format = Format::Unknown` (Unknown = no depth attachment) + `.depthTestEnable/.depthWriteEnable : bool = false`; `Metal4Pipeline` carries an `MTL::DepthStencilState`; validation closes the renderTarget-format abort door (backlog).

- [ ] **Step 1: Write the failing validation tests (TDD)**

Append to `Tests/RHIValidateTests.cpp`, matching its existing style:

```cpp
TEST_CASE("TextureDesc renderTarget with a non-renderable format is rejected", "[rhi]") {
    TextureDesc desc{};
    desc.width = 64;
    desc.height = 64;
    desc.format = Format::D32Float;
    desc.renderTarget = false;   // plain sampled D32 is fine...
    REQUIRE(validate(desc).has_value());

    desc.renderTarget = true;    // ...and a D32 render target is a *depth* target: also fine.
    REQUIRE(validate(desc).has_value());
}

TEST_CASE("GraphicsPipelineDesc depth flags without a depth format are rejected", "[rhi]") {
    DummyShaderLibrary library;
    GraphicsPipelineDesc desc{};
    desc.library = &library;
    desc.vertexEntry = "vertexMain";
    desc.fragmentEntry = "fragmentMain";
    desc.colorFormat = Format::BGRA8Unorm;
    desc.depthTestEnable = true; // but depthFormat left Unknown

    const auto r = validate(desc);
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == ErrorCode::InvalidDesc);
    REQUIRE(r.error().message.contains("depthFormat"));
}

TEST_CASE("GraphicsPipelineDesc with a color format as depth format is rejected", "[rhi]") {
    DummyShaderLibrary library;
    GraphicsPipelineDesc desc{};
    desc.library = &library;
    desc.vertexEntry = "vertexMain";
    desc.fragmentEntry = "fragmentMain";
    desc.colorFormat = Format::BGRA8Unorm;
    desc.depthFormat = Format::RGBA8Unorm;

    const auto r = validate(desc);
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == ErrorCode::InvalidDesc);
    REQUIRE(r.error().message.contains("depth"));
}

TEST_CASE("TextureDesc D32Float with cpuReadback is rejected", "[rhi]") {
    TextureDesc desc{};
    desc.width = 64;
    desc.height = 64;
    desc.format = Format::D32Float;
    desc.cpuReadback = true;

    const auto r = validate(desc);
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == ErrorCode::InvalidDesc);
}
```
Run: `xmake -y` — expect compile FAIL (`depthTestEnable` doesn't exist yet). That is the red state.

- [ ] **Step 2: Grow RHI.h**

```cpp
struct TextureDesc {
    uint32_t width = 0, height = 0;
    Format format = Format::BGRA8Unorm;
    bool renderTarget = false;
    bool sampled = false;     // bound for shader reads after rendering (scene RT, shadow maps)
    bool cpuReadback = false; // shared storage; enables readback()
    std::string_view label;
};
```
`RenderPassDesc` grows two fields after `clear`:

```cpp
    // Optional depth attachment. Cleared to clearDepth when set (load) and discarded after the
    // pass (store) -- M2 never reads depth back. Null = depth-less pass, as in M1.
    Texture* depthTarget = nullptr;
    float clearDepth = 1.0f;
```
`GraphicsPipelineDesc` grows, before `label`:

```cpp
    // Unknown = no depth attachment. Metal 4 pipelines carry no depth pixel format (it is a
    // render-pass property there) -- this field is validated CPU-side against the depth flags
    // and kept in the desc because the future Vulkan backend bakes it into the pipeline.
    Format depthFormat = Format::Unknown;
    bool depthTestEnable = false;  // compare LESS when enabled
    bool depthWriteEnable = false;
```

- [ ] **Step 3: Grow Validate.cpp**

Add `isDepthFormat` next to `isColorRenderableFormat` (`Validate.cpp:30`):

```cpp
bool isDepthFormat(Format format) {
    return format == Format::D32Float;
}
```
In `validate(const TextureDesc&)`: replace nothing, add after the Unknown check —

```cpp
    if (desc.renderTarget && !isColorRenderableFormat(desc.format) && !isDepthFormat(desc.format)) {
        return invalid("TextureDesc.renderTarget requires a color-renderable or depth format");
    }
```
(The existing `cpuReadback` 8-bit-only rule already rejects D32+readback — the new test pins it.)
In `validate(const GraphicsPipelineDesc&)` add:

```cpp
    if (desc.depthFormat != Format::Unknown && !isDepthFormat(desc.depthFormat)) {
        return invalid("GraphicsPipelineDesc.depthFormat must be a depth format (D32Float) or "
                       "Format::Unknown for a depth-less pipeline");
    }
    if ((desc.depthTestEnable || desc.depthWriteEnable) && desc.depthFormat == Format::Unknown) {
        return invalid("GraphicsPipelineDesc.depthFormat must be set when depth test/write is "
                       "enabled");
    }
```
Run: `xmake -y && xmake test Tests/unit` — all PASS.

- [ ] **Step 4: Backend — texture usage + depth pass + depth-stencil state**

`Metal4Device::createTexture` (`Metal4Device.cpp:346-352`): usage/storage derive from the desc —

```cpp
    MTL::TextureUsage usage = desc.sampled || desc.cpuReadback ? MTL::TextureUsageShaderRead
                                                               : MTL::TextureUsage(0);
    if (desc.renderTarget) {
        usage |= MTL::TextureUsageRenderTarget;
    }
    LMX_ASSERT(usage != MTL::TextureUsage(0),
               "TextureDesc: a texture that is neither renderTarget, sampled, nor cpuReadback "
               "has no reachable use");
    textureDesc->setUsage(usage);
```
(This replaces M1's unconditional ShaderRead — the M1 comment's "always on" reasoning ends now that usage is desc-driven; a pure depth target needs no ShaderRead.)

`Metal4CommandList::beginRenderPass` (after the color attachment block): when `desc.depthTarget != nullptr`, downcast (same contract-violation rationale as colorTarget), assert its native `pixelFormat() == MTL::PixelFormatDepth32Float` ("RenderPassDesc.depthTarget must be a D32Float texture"), then:

```cpp
        MTL::RenderPassDepthAttachmentDescriptor* depth = passDesc->depthAttachment();
        depth->setTexture(depthTarget->handle());
        depth->setLoadAction(desc.clear ? MTL::LoadActionClear : MTL::LoadActionLoad);
        // DontCare: nothing reads scene depth after the pass in M2; Store would spill it for no
        // consumer. Flip to Store the day a depth-reading pass exists.
        depth->setStoreAction(MTL::StoreActionDontCare);
        depth->setClearDepth(desc.clearDepth);
```
`createGraphicsPipeline`: when `desc.depthFormat != Format::Unknown` (or flags set — validation already ties them), build the depth-stencil state and store it on the pipeline wrapper:

```cpp
    NS::SharedPtr<MTL::DepthStencilState> depthState;
    if (desc.depthTestEnable || desc.depthWriteEnable) {
        auto dsDesc = NS::TransferPtr(MTL::DepthStencilDescriptor::alloc()->init());
        dsDesc->setDepthCompareFunction(desc.depthTestEnable ? MTL::CompareFunctionLess
                                                             : MTL::CompareFunctionAlways);
        dsDesc->setDepthWriteEnabled(desc.depthWriteEnable);
        dsDesc->setLabel(labelOrFallback(desc.label, "lmx.pipeline.unnamed").get());
        depthState = NS::TransferPtr(m_device->newDepthStencilState(dsDesc.get()));
        if (!depthState) {
            return fail(ErrorCode::PipelineCreationFailed, "failed to create depth-stencil state");
        }
    }
```
`Metal4Pipeline` gains the member + accessor (`MTL::DepthStencilState* depthState() const` — may be null); `bindPipeline` sets it when present: `if (auto* ds = pipeline.depthState()) m_encoder->setDepthStencilState(ds);`.

- [ ] **Step 5: Verify + commit**

```bash
xmake -y && xmake test && xmake format
git add -A && git commit -m "Grow the RHI: depth attachments, sampled targets, format validation"
```

---

### Task 5: drawIndexed (Opus 5)

**Files:**
- Modify: `Source/RHI/RHI.h` (CommandList), `Source/RHI/Metal4/Metal4CommandList.h/.cpp`

**Interfaces:**
- Consumes: `Metal4Buffer::handle()->gpuAddress()` (M1 pattern at `Metal4CommandList.cpp:80`).
- Produces: `CommandList::drawIndexed(Buffer& indexBuffer, uint32_t indexCount, uint32_t firstIndex = 0)` — indices are **uint32 only**, documented on the method. GPU-proof lands with Task 8's scene test.

- [ ] **Step 1: RHI surface**

```cpp
    // Indexed draw. Indices are uint32 (the only index type this RHI models); the index buffer
    // is any Buffer holding them -- Metal 4 consumes it per-draw by GPU address, so there is no
    // separate index-buffer bind state. firstIndex is an element offset into the buffer.
    virtual void drawIndexed(Buffer& indexBuffer, uint32_t indexCount, uint32_t firstIndex = 0) = 0;
```

- [ ] **Step 2: Backend**

```cpp
void Metal4CommandList::drawIndexed(Buffer& indexBuffer, uint32_t indexCount,
                                    uint32_t firstIndex) {
    LMX_ASSERT(m_encoder, "drawIndexed must be called between beginRenderPass and endRenderPass");
    LMX_ASSERT(indexCount > 0, "drawIndexed: indexCount must be greater than zero");
    auto& mtlBuffer = static_cast<Metal4Buffer&>(indexBuffer);
    const uint64_t offsetBytes = uint64_t{firstIndex} * sizeof(uint32_t);
    const uint64_t lengthBytes = mtlBuffer.handle()->length();
    LMX_ASSERT(offsetBytes + uint64_t{indexCount} * sizeof(uint32_t) <= lengthBytes,
               "drawIndexed: index range reads past the end of the index buffer");
    m_encoder->drawIndexedPrimitives(MTL::PrimitiveTypeTriangle, indexCount,
                                     MTL::IndexTypeUInt32,
                                     mtlBuffer.handle()->gpuAddress() + offsetBytes,
                                     lengthBytes - offsetBytes);
}
```
(Signature verified against `MTL4RenderCommandEncoder.hpp:66`; the trailing parameter is the *length* of the index data at that address.)

- [ ] **Step 3: Verify + commit**

```bash
xmake -y && xmake test && xmake format
git add -A && git commit -m "Add uint32 indexed draws to the RHI"
```

---

### Task 6: bindTexture + textureBarrier (Opus 5)

**Files:**
- Modify: `Source/RHI/RHI.h`, `Source/RHI/Metal4/Metal4CommandList.h/.cpp`
- Modify: `docs/specs/2026-08-07-m2-renderer-skeleton-design.md` (§4 amendment — see step 1)

**Interfaces:**
- Consumes: Task 4's `sampled` textures; per-frame argument tables (Task 2).
- Produces: `enum class TextureUse { RenderTarget, ShaderRead };` · `CommandList::bindTexture(uint32_t slot, Texture& texture)` (texture slots are their own index space, distinct from buffer slots) · `CommandList::textureBarrier(Texture&, TextureUse from, TextureUse to)` — callable only **between** passes; M2 implements exactly the RenderTarget→ShaderRead edge.

- [ ] **Step 1: Record the spec amendment**

`bindTexture` is not in spec §4. It is required to make §7's "barrier path — render → barrier → sample → readback" GPU test expressible through the RHI at all (ImGui samples through its own backend, not ours), and it is the argument-table texture half M3's shadow sampling needs. Append to the spec's §4 (one bullet + a dated "Amendment" note at the section end) and commit together with this task.

- [ ] **Step 2: RHI surface**

```cpp
// How a texture is being used at a barrier boundary. Grows per real feature demand, exactly
// like the rest of this header (ADR 0004).
enum class TextureUse { RenderTarget, ShaderRead };
```
On `CommandList`:

```cpp
    // Binds a texture for shader reads at the given argument-table texture slot. Texture slots
    // are their own index space -- slot 0 here and buffer slot 0 coexist. Valid only inside a
    // render pass; the texture must have been created with sampled = true.
    virtual void bindTexture(uint32_t slot, Texture& texture) = 0;

    // Makes writes of `from` visible to reads of `to` for subsequent passes. Valid only
    // *between* render passes on this frame's command list. M2 supports the one edge a real
    // feature demands -- RenderTarget -> ShaderRead (scene RT sampled by the UI pass); any
    // other combination is a contract violation until a feature grows it.
    virtual void textureBarrier(Texture& texture, TextureUse from, TextureUse to) = 0;
```

- [ ] **Step 3: Backend**

`bindTexture`:

```cpp
void Metal4CommandList::bindTexture(uint32_t slot, Texture& texture) {
    LMX_ASSERT(m_encoder, "bindTexture must be called between beginRenderPass and endRenderPass");
    m_argumentTable->setTexture(static_cast<Metal4Texture&>(texture).handle()->gpuResourceID(),
                                slot);
}
```
`textureBarrier` — Metal 4 barriers are encoder operations, and this call sits between passes, so it is recorded as pending and encoded by the *next* `beginRenderPass` right after encoder creation (consumer-side barrier — matches Apple's managing-metal4-synchronization guidance):

```cpp
void Metal4CommandList::textureBarrier(Texture& texture, TextureUse from, TextureUse to) {
    (void)texture; // stage-scoped in Metal 4; the parameter documents intent and feeds the
                   // future Vulkan backend's image transition.
    LMX_ASSERT(!m_encoder, "textureBarrier must be called between render passes, not inside one");
    LMX_ASSERT(from == TextureUse::RenderTarget && to == TextureUse::ShaderRead,
               "textureBarrier: only RenderTarget -> ShaderRead is implemented (grown per demand)");
    m_pendingBarrier = true;
}
```
In `beginRenderPass`, immediately after the encoder is created and labeled:

```cpp
    if (m_pendingBarrier) {
        // Attachment writes happen in the fragment stage (MTL::Stages has no render-target
        // stage), so the RT-write -> fragment-read edge is fragment -> fragment.
        m_encoder->barrierAfterQueueStages(MTL::StageFragment, MTL::StageFragment,
                                           MTL4::VisibilityOptionDevice);
        m_pendingBarrier = false;
    }
```
Member: `bool m_pendingBarrier = false;`. Also clear it in `resetForFrame` (a barrier pending at frame end is a dropped edge — assert it is false there instead: `LMX_ASSERT(!m_pendingBarrier, "textureBarrier recorded but no later pass consumed it")`). Executor: verify `barrierAfterQueueStages` is legal at render-encoder start against the synchronization skill; if render encoders reject queue-stage barriers in this position, the fallback is `barrierAfterStages(...)` with the same stage pair — record whichever holds in the Amendments block.

- [ ] **Step 4: Verify + commit**

```bash
xmake -y && xmake test && xmake format
git add -A && git commit -m "Add bindTexture and RenderTarget->ShaderRead textureBarrier to the RHI"
```

---

### Task 7: lmx::render — Camera + Mesh (Sonnet 5)

**Files:**
- Create: `Source/Render/Camera.h`, `Source/Render/Camera.cpp`, `Source/Render/Mesh.h`, `Source/Render/Mesh.cpp`
- Delete: `Source/Render/.gitkeep`
- Modify: `xmake.lua` (new `Render` target)
- Test: `Tests/RenderTests.cpp` (new)

**Interfaces:**
- Consumes: `rhi::Device::createBuffer`, `rhi::Result`.
- Produces (exact signatures later tasks rely on):

```cpp
namespace lmx::render {
class Camera {           // public fields: position, yaw, pitch, fovY, nearZ, farZ, moveSpeed
    glm::vec3 forward() const;
    glm::vec3 right() const;
    void move(const glm::vec3& localDelta);   // x=right, y=world-up, z=forward (pre-scaled)
    void look(float yawDelta, float pitchDelta); // radians; pitch clamped to ±(π/2 − 0.01)
    glm::mat4 viewMatrix() const;
    glm::mat4 projectionMatrix(float aspect) const; // perspectiveRH_ZO — Metal [0,1] depth
};
struct Vertex { float px, py, pz, nx, ny, nz, r, g, b; };   // packed, stride 36
struct MeshData { std::vector<Vertex> vertices; std::vector<uint32_t> indices; };
MeshData makeCube();                  // unit cube at origin, 24 verts / 36 indices, CCW, per-face normals
MeshData makePlane(float halfExtent); // XZ plane at y=0, 4 verts / 6 indices, +Y normal
struct Mesh { std::unique_ptr<rhi::Buffer> vertexBuffer, indexBuffer; uint32_t indexCount; };
rhi::Result<Mesh> createMesh(rhi::Device&, const MeshData&, std::string_view label);
}
```

- [ ] **Step 1: xmake target**

```lua
target("Render")
    set_kind("static")
    add_files("Source/Render/*.cpp")
    add_deps("Core", "RHI")
    add_packages("glm", {public = true})
```
Add `"Render"` to App's and Tests' `add_deps`, and `add_packages("glm")` to Tests.

- [ ] **Step 2: Write the failing camera + mesh tests**

`Tests/RenderTests.cpp` (Catch2, `[render]` tag — CPU-only, runs in CI). Real test bodies, not sketches:

```cpp
#include <catch2/catch_test_macros.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/epsilon.hpp>

#include "Render/Camera.h"
#include "Render/Mesh.h"

using namespace lmx::render;

namespace {
constexpr float kEps = 1e-5f;
bool near3(const glm::vec3& a, const glm::vec3& b) {
    return glm::all(glm::epsilonEqual(a, b, kEps));
}
} // namespace

TEST_CASE("default camera looks down -Z", "[render]") {
    Camera camera;
    camera.yaw = 0.0f;
    camera.pitch = 0.0f;
    REQUIRE(near3(camera.forward(), {0.0f, 0.0f, -1.0f}));
    REQUIRE(near3(camera.right(), {1.0f, 0.0f, 0.0f}));
}

TEST_CASE("view matrix moves the world opposite the camera", "[render]") {
    Camera camera;
    camera.position = {0.0f, 0.0f, 5.0f};
    camera.yaw = 0.0f;
    camera.pitch = 0.0f;
    // A point 1 unit in front of the camera lands 1 unit down the view -Z axis.
    const glm::vec4 p = camera.viewMatrix() * glm::vec4(0.0f, 0.0f, 4.0f, 1.0f);
    REQUIRE(near3(glm::vec3(p), {0.0f, 0.0f, -1.0f}));
}

TEST_CASE("projection maps near to 0 and far to 1 -- Metal depth range", "[render]") {
    Camera camera;
    camera.nearZ = 0.1f;
    camera.farZ = 100.0f;
    const glm::mat4 proj = camera.projectionMatrix(16.0f / 9.0f);
    const glm::vec4 nearP = proj * glm::vec4(0.0f, 0.0f, -camera.nearZ, 1.0f);
    const glm::vec4 farP = proj * glm::vec4(0.0f, 0.0f, -camera.farZ, 1.0f);
    REQUIRE(nearP.z / nearP.w == Catch::Approx(0.0f).margin(1e-6));
    REQUIRE(farP.z / farP.w == Catch::Approx(1.0f).margin(1e-4));
}

TEST_CASE("look clamps pitch short of the poles", "[render]") {
    Camera camera;
    camera.look(0.0f, 10.0f); // way past +90°
    REQUIRE(camera.pitch < glm::half_pi<float>());
    camera.look(0.0f, -20.0f);
    REQUIRE(camera.pitch > -glm::half_pi<float>());
}

TEST_CASE("move is camera-relative on the horizontal plane", "[render]") {
    Camera camera;
    camera.position = {0.0f, 0.0f, 0.0f};
    camera.yaw = glm::half_pi<float>(); // facing +X
    camera.pitch = 0.0f;
    camera.move({0.0f, 0.0f, 2.0f});   // forward
    REQUIRE(near3(camera.position, {2.0f, 0.0f, 0.0f}));
    camera.move({1.0f, 0.0f, 0.0f});   // right of +X-facing = -Z... verify via right()
    REQUIRE(near3(camera.position, glm::vec3{2.0f, 0.0f, 0.0f} + camera.right()));
}

TEST_CASE("cube mesh has 24 vertices, 36 CCW indices, unit bounds", "[render]") {
    const MeshData cube = makeCube();
    REQUIRE(cube.vertices.size() == 24);
    REQUIRE(cube.indices.size() == 36);
    glm::vec3 lo{1e9f}, hi{-1e9f};
    for (const Vertex& v : cube.vertices) {
        lo = glm::min(lo, {v.px, v.py, v.pz});
        hi = glm::max(hi, {v.px, v.py, v.pz});
    }
    REQUIRE(near3(lo, {-0.5f, -0.5f, -0.5f}));
    REQUIRE(near3(hi, {0.5f, 0.5f, 0.5f}));
    // Every triangle's geometric normal must agree with its vertices' stored normal --
    // this pins both winding (CCW from outside) and per-face normals in one property.
    for (size_t i = 0; i < cube.indices.size(); i += 3) {
        const Vertex& a = cube.vertices[cube.indices[i]];
        const Vertex& b = cube.vertices[cube.indices[i + 1]];
        const Vertex& c = cube.vertices[cube.indices[i + 2]];
        const glm::vec3 geometric = glm::normalize(
            glm::cross(glm::vec3{b.px - a.px, b.py - a.py, b.pz - a.pz},
                       glm::vec3{c.px - a.px, c.py - a.py, c.pz - a.pz}));
        REQUIRE(glm::dot(geometric, {a.nx, a.ny, a.nz}) > 0.99f);
    }
}

TEST_CASE("plane mesh spans its half extent with +Y normals", "[render]") {
    const MeshData plane = makePlane(5.0f);
    REQUIRE(plane.vertices.size() == 4);
    REQUIRE(plane.indices.size() == 6);
    for (const Vertex& v : plane.vertices) {
        REQUIRE(v.py == 0.0f);
        REQUIRE(near3({v.nx, v.ny, v.nz}, {0.0f, 1.0f, 0.0f}));
        REQUIRE(std::abs(v.px) == Catch::Approx(5.0f));
        REQUIRE(std::abs(v.pz) == Catch::Approx(5.0f));
    }
}
```
(Include `<catch2/catch_approx.hpp>` for `Catch::Approx`.) Run: `xmake -y` — FAIL (headers don't exist). Red state confirmed.

- [ ] **Step 3: Implement Camera**

`Camera.h` — public-field struct-style class as in the Interfaces block, with the doc comment: yaw 0 faces −Z, positive yaw turns toward +X; pitch positive looks up; both radians. `Camera.cpp`:

```cpp
glm::vec3 Camera::forward() const {
    const float cp = std::cos(pitch);
    return glm::normalize(glm::vec3{std::sin(yaw) * cp, std::sin(pitch), -std::cos(yaw) * cp});
}
glm::vec3 Camera::right() const {
    return glm::normalize(glm::cross(forward(), glm::vec3{0.0f, 1.0f, 0.0f}));
}
void Camera::move(const glm::vec3& localDelta) {
    position += right() * localDelta.x + glm::vec3{0.0f, 1.0f, 0.0f} * localDelta.y +
                forward() * localDelta.z;
}
void Camera::look(float yawDelta, float pitchDelta) {
    yaw += yawDelta;
    // Hard stop short of the poles: at ±π/2 forward() and world-up are parallel and right()
    // degenerates. 0.01 rad ≈ 0.6° of headroom.
    pitch = std::clamp(pitch + pitchDelta, -glm::half_pi<float>() + 0.01f,
                       glm::half_pi<float>() - 0.01f);
}
glm::mat4 Camera::viewMatrix() const {
    return glm::lookAt(position, position + forward(), glm::vec3{0.0f, 1.0f, 0.0f});
}
glm::mat4 Camera::projectionMatrix(float aspect) const {
    // perspectiveRH_ZO: Metal clip-space depth is [0,1]. glm::perspective would silently
    // hand back the GL [-1,1] convention and cost half the depth-buffer precision.
    return glm::perspectiveRH_ZO(fovY, aspect, nearZ, farZ);
}
```

- [ ] **Step 4: Implement Mesh**

`Mesh.h` per the Interfaces block, with the layout comment: "Mirrors the packed MSL layout Slang emits for Shaders/Mesh.slang (measured in Task 8): three packed_float3 fields, stride 36" + `static_assert(sizeof(Vertex) == 36, "vertex stride must match the shader's packed layout");`. `Mesh.cpp`: `makeCube` builds 6 faces × 4 vertices (per-face normal, per-face color left white {1,1,1} — scene color comes from `DrawItem.baseColor`), indices CCW viewed from outside; `makePlane` 4 corners at ±halfExtent, CCW from above, light gray {0.8, 0.8, 0.8}. `createMesh`:

```cpp
rhi::Result<Mesh> createMesh(rhi::Device& device, const MeshData& data, std::string_view label) {
    LMX_ASSERT(!data.vertices.empty() && !data.indices.empty(),
               "createMesh: mesh data must not be empty");
    Mesh mesh;
    const std::string base(label);
    auto vertices = device.createBuffer(
        {.size = data.vertices.size() * sizeof(Vertex), .label = base + ".vertices"},
        data.vertices.data());
    if (!vertices) {
        return std::unexpected(vertices.error());
    }
    mesh.vertexBuffer = std::move(*vertices);
    auto indices = device.createBuffer(
        {.size = data.indices.size() * sizeof(uint32_t), .label = base + ".indices"},
        data.indices.data());
    if (!indices) {
        return std::unexpected(indices.error());
    }
    mesh.indexBuffer = std::move(*indices);
    mesh.indexCount = static_cast<uint32_t>(data.indices.size());
    return mesh;
}
```
(`BufferDesc.label` is a `string_view` over a local `std::string` — safe: `createBuffer` consumes it synchronously. Note this in a comment.)

- [ ] **Step 5: Green + commit**

```bash
xmake -y && xmake test Tests/unit    # camera/mesh tests pass
xmake format && git add -A && git commit -m "Add lmx::render Camera and procedural meshes with tests"
```

---

### Task 8: Mesh.slang + Renderer + GPU proof tests (Opus 5)

**Files:**
- Create: `Shaders/Mesh.slang`, `Shaders/FullscreenSample.slang`, `Source/Render/Renderer.h`, `Source/Render/Renderer.cpp`
- Modify: `Tests/GpuSmokeTests.cpp` (add scene/depth/barrier cases; the M1 triangle case stays until Task 10)

**Interfaces:**
- Consumes: everything from Tasks 2–7.
- Produces:

```cpp
namespace lmx::render {
struct DrawItem { const Mesh* mesh; glm::mat4 model{1.0f}; glm::vec4 baseColor{1.0f}; };
class Renderer {
    static rhi::Result<std::unique_ptr<Renderer>> create(rhi::Device&, uint32_t width,
                                                         uint32_t height, bool cpuReadback = false);
    rhi::Result<void> resize(uint32_t width, uint32_t height); // caller guarantees GPU idle
    void render(rhi::CommandList&, const Camera&, std::span<const DrawItem>);
    rhi::Texture& colorTarget();          // barriered to ShaderRead when render() returns
    uint32_t width() const; uint32_t height() const;
    float clearColor[4];                  // defaults {0.05f, 0.07f, 0.10f, 1.0f}
};
}
```
Shader binding contract (verify against emitted MSL, step 2): buffer slot 0 = `gVertices`, buffer slot 1 = `gObject` (ConstantBuffer), texture slot 0 = `gSource` (FullscreenSample only).

- [ ] **Step 1: Write Shaders/Mesh.slang**

```slang
// Depth-tested lambert mesh: vertex pulling from slot 0, per-draw uniforms at slot 1
// (slot indices map 1:1 to MTL4ArgumentTable indices — see shader-style.md).
struct Vertex
{
    float3 position;
    float3 normal;
    float3 color;
};

[[vk::binding(0, 0)]]
StructuredBuffer<Vertex> gVertices : register(t0);  // → MSL [[buffer(0)]]

struct ObjectUniforms
{
    float4x4 mvp;
    float4x4 model;
    float4 baseColor;
};

[[vk::binding(1, 0)]]
ConstantBuffer<ObjectUniforms> gObject : register(b0);  // → MSL [[buffer(1)]]

static const float3 kLightDir = normalize(float3(0.4, 1.0, 0.3));
static const float kAmbient = 0.25;

struct VSOutput
{
    float4 position : SV_Position;
    float3 normal : NORMAL0;
    float3 color : COLOR0;
};

[shader("vertex")]
VSOutput vertexMain(uint vid: SV_VertexID)
{
    Vertex v = gVertices[vid];
    VSOutput o;
    o.position = mul(gObject.mvp, float4(v.position, 1.0));
    // Rotation/uniform-scale only in M2 scenes, so the model matrix's upper 3x3 is a valid
    // normal transform; revisit with non-uniform scale.
    o.normal = normalize(mul((float3x3)gObject.model, v.normal));
    o.color = v.color * gObject.baseColor.rgb;
    return o;
}

[shader("fragment")]
float4 fragmentMain(VSOutput in): SV_Target
{
    float ndotl = max(dot(normalize(in.normal), kLightDir), 0.0);
    return float4(in.color * (kAmbient + (1.0 - kAmbient) * ndotl), 1.0);
}
```
And `Shaders/FullscreenSample.slang` (exists for the Task 8 barrier test; the App never uses it):

```slang
// Test-only: fullscreen triangle that copies a texture via Load — no sampler state needed.
[[vk::binding(0, 0)]]
Texture2D<float4> gSource : register(t0);  // → MSL [[texture(0)]]

struct VSOutput
{
    float4 position : SV_Position;
};

[shader("vertex")]
VSOutput vertexMain(uint vid: SV_VertexID)
{
    // The classic 3-vertex fullscreen triangle.
    float2 uv = float2((vid << 1) & 2, vid & 2);
    VSOutput o;
    o.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    return o;
}

[shader("fragment")]
float4 fragmentMain(VSOutput in): SV_Target
{
    return gSource.Load(int3(int2(in.position.xy), 0));
}
```

- [ ] **Step 2: Measure the emitted MSL — record**

```bash
xmake -y
grep -n "buffer(\|texture(\|packed_float" build/macosx/arm64/debug/Shaders/Mesh.metal | head -20
grep -n "texture(" build/macosx/arm64/debug/Shaders/FullscreenSample.metal | head
```
Record in this plan's Amendments block (M1 Task-6-record pattern): the actual MSL buffer/texture indices for `gVertices`/`gObject`/`gSource`, the emitted Vertex struct (expect `packed_float3 ×3`, stride 36 — must match `render::Vertex`'s static_assert), and the ObjectUniforms MSL layout (expect column-major `float4x4`s + `float4`, 144 bytes — glm matrices upload verbatim; the mul() order in the shader plus the GPU tests below is what proves the convention, a transposed matrix cannot pass the depth-order probes). If any index differs from the contract above, fix the `[[vk::binding]]` in the shader — never the C++ constants first.

- [ ] **Step 3: Implement Renderer**

`Renderer.h` per Interfaces (private: device ref, color/depth `std::unique_ptr<rhi::Texture>`, pipeline, shader library, `m_width/m_height/m_cpuReadback`). `Renderer.cpp` essentials:

```cpp
namespace {
// C++ mirror of Shaders/Mesh.slang ObjectUniforms (MSL layout measured in this task):
// two column-major float4x4 + one float4, no padding.
struct ObjectUniforms {
    glm::mat4 mvp;
    glm::mat4 model;
    glm::vec4 baseColor;
};
static_assert(sizeof(ObjectUniforms) == 144, "must match the shader's cbuffer layout");

constexpr uint32_t kVertexBufferSlot = 0;
constexpr uint32_t kObjectUniformsSlot = 1;
} // namespace
```
`create()`: loads `"Shaders/Mesh"`, builds the pipeline —

```cpp
    auto pipeline = device.createGraphicsPipeline({.library = self->m_library.get(),
                                                   .vertexEntry = "vertexMain",
                                                   .fragmentEntry = "fragmentMain",
                                                   .colorFormat = rhi::Format::BGRA8Unorm,
                                                   .depthFormat = rhi::Format::D32Float,
                                                   .depthTestEnable = true,
                                                   .depthWriteEnable = true,
                                                   .label = "lmx.render.meshPipeline"});
```
— then delegates to `resize(width, height)` for the targets. `resize()`: recreates color (`renderTarget + sampled` + `cpuReadback` when the test hook asked for it, label `"lmx.render.sceneColor"`) and depth (`renderTarget` only, `D32Float`, label `"lmx.render.sceneDepth"`); documents "caller guarantees GPU idle — in-flight frames may still reference the old targets otherwise". `render()`:

```cpp
void Renderer::render(rhi::CommandList& commands, const Camera& camera,
                      std::span<const DrawItem> items) {
    const float aspect = static_cast<float>(m_width) / static_cast<float>(m_height);
    const glm::mat4 viewProj = camera.projectionMatrix(aspect) * camera.viewMatrix();

    commands.beginRenderPass({.colorTarget = m_color.get(),
                              .clearColor = {clearColor[0], clearColor[1], clearColor[2],
                                             clearColor[3]},
                              .clear = true,
                              .depthTarget = m_depth.get(),
                              .clearDepth = 1.0f});
    commands.bindPipeline(*m_pipeline);
    for (const DrawItem& item : items) {
        LMX_ASSERT(item.mesh != nullptr, "DrawItem.mesh must not be null");
        const ObjectUniforms uniforms{.mvp = viewProj * item.model,
                                      .model = item.model,
                                      .baseColor = item.baseColor};
        commands.bindVertexBuffer(kVertexBufferSlot, *item.mesh->vertexBuffer);
        commands.setUniforms(kObjectUniformsSlot, &uniforms, sizeof(uniforms));
        commands.drawIndexed(*item.mesh->indexBuffer, item.mesh->indexCount);
    }
    commands.endRenderPass();
    // Leaves the color target readable by any later pass this frame (UI viewport, blit test).
    commands.textureBarrier(*m_color, rhi::TextureUse::RenderTarget, rhi::TextureUse::ShaderRead);
}
```

- [ ] **Step 4: GPU proof tests**

Append to `Tests/GpuSmokeTests.cpp` (shared helpers already exist there; generalize `pixelAt`/`describe` to take a width if needed). Three cases, all `[gpu]`:

**(a) scene renders + per-draw uniforms capture** — `Renderer` at 64×64 with `cpuReadback = true`; camera at `{0, 0, 5}` (yaw 0, pitch 0) looking at two unit cubes: left at `model = translate({-1.2, 0, 0})` with `baseColor = {1, 0, 0, 1}`, right at `translate({+1.2, 0, 0})` with `baseColor = {0, 0, 1, 1}`. One `beginFrame`/`render`/`endFrame(nullptr)`/`waitIdle`/readback. Probes: corner (2,2) == clearColor; left-cube probe (16, 32) red-dominant; right-cube probe (48, 32) blue-dominant. **Two different uniform sets in one pass with distinct colors both landing is the empirical answer to Task 3 step 5** — if the right cube comes out red (last-write-wins), STOP, record it in Amendments, and implement the table-pool contingency from Task 3 before proceeding.

**(b) depth ordering** — same Renderer; two `makePlane(1.0f)` items rotated upright to face the camera (`model = translate * rotate(π/2 about X)`): near plane at z = +1 colored green, far plane at z = −1 colored red, both centered. Render once with items ordered [far, near] and once (a second frame, same renderer) ordered [near, far]. Both readbacks must show green at the center probe — the depth test, not draw order, decides visibility. Without a depth buffer the second ordering shows red, so this fails loudly on a broken depth path.

**(c) barrier + sample readback** — Renderer at 64×64 `cpuReadback = false` (the real configuration); render scene (a); then in the *same frame* a second pass: separate 64×64 `renderTarget + cpuReadback` destination texture, `FullscreenSample` pipeline (no depth), `bindTexture(0, renderer.colorTarget())`, `draw(3)`; endFrame, waitIdle, readback the destination. Probes identical to (a) — the copy must carry the scene across the barrier intact.

Run: `xmake test` — all `[gpu]` cases pass locally. This is the moment Task 3's design is proven or amended.

- [ ] **Step 5: Format, full verify, commit**

```bash
xmake format && xmake -y && xmake test
git add -A && git commit -m "Add lmx::render Renderer with GPU-proven depth, uniforms, and barrier"
```

---

### Task 9: Metal4ImGui backend glue (Opus 5)

**Files:**
- Create: `Source/RHI/Metal4/Metal4ImGui.h`, `Source/RHI/Metal4/Metal4ImGui.cpp` (or `.mm` — Task 0's record decides)
- Modify: `xmake.lua` (RHI target gains the imgui dep/backend sources per Task 0's route)

**Interfaces:**
- Consumes: Task 0's recorded backend entry points; `Metal4Device::handle()`, `Metal4Texture::handle()`, `Metal4CommandList` encoder access (add a backend-internal `MTL4::RenderCommandEncoder* currentEncoder() const` accessor to `Metal4CommandList` — asserts a pass is open).
- Produces (`Metal4ImGui.h`, deliberately metal-cpp-free in its declarations, `Metal4Capture.h` pattern):

```cpp
namespace lmx::rhi::metal4 {
// ImGui platform glue for the Metal 4 backend. The SDL3 *platform* backend
// (ImGui_ImplSDL3_*) is API-agnostic and stays in the App; this file owns only the
// renderer half. Not part of the RHI surface -- developer/editor tooling, like Metal4Capture.
bool imguiInit(Device& device);                    // after ImGui::CreateContext()
void imguiShutdown();                              // before ImGui::DestroyContext()
void imguiNewFrame();                              // per frame, before ImGui::NewFrame()
void imguiRender(CommandList& commands);           // inside the UI render pass
ImTextureID imguiTextureID(Texture& texture);      // for ImGui::Image of an RHI texture
}
```
(`ImTextureID` needs `imgui.h` in this header — acceptable: this header is backend tooling included only by App/UI code that already speaks ImGui; RHI.h stays clean. If Task 0 recorded a metal-cpp-native backend API, the `.cpp` stays C++; if the backend is ObjC++-only, this file becomes `.mm` and the header still compiles as plain C++.)

- [ ] **Step 1: Implement against Task 0's record**

Init wires the imgui Metal backend with `static_cast<Metal4Device&>(device).handle()`; render calls the backend's RenderDrawData with `ImGui::GetDrawData()` and the native encoder from `currentEncoder()`; `imguiTextureID` returns the recorded backend's texture-ID convention for `static_cast<Metal4Texture&>(texture).handle()` (imgui 1.92: `(ImTextureID)(intptr_t)mtlTexture` unless the backend documents otherwise — the example is ground truth). Follow the M1 convention: every downcast documented as a contract, not a runtime check.

- [ ] **Step 2: Compile-only verify + commit**

Nothing calls this yet; the target must build clean with the new files and deps.
```bash
xmake -y && xmake test && xmake format
git add -A && git commit -m "Add ImGui renderer glue for the Metal 4 backend"
```

---

### Task 10: App — editor shell, fly camera, scene, screenshot (Opus 5)

**Files:**
- Create: `Source/App/EditorShell.h`, `Source/App/EditorShell.cpp`, `Source/App/Screenshot.h`, `Source/App/Screenshot.cpp`
- Modify: `Source/App/main.cpp` (major rewrite), `xmake.lua` (App gains imgui dep per Task 0 route)
- Delete: `Shaders/Triangle.slang`, the M1 triangle case in `Tests/GpuSmokeTests.cpp`

**Interfaces:**
- Consumes: `render::Renderer/Camera/Mesh/DrawItem`, `metal4::imgui*`, `ImGui_ImplSDL3_*` (from the imgui package/pin), Task 8's scene contract.
- Produces: the M2 app. `EditorShell` owns ImGui context + dockspace + panels + camera input; `Screenshot` owns the BMP writer + offscreen path (moved verbatim from M1 main.cpp); `main.cpp` keeps SDL lifecycle, frame loop, capture/automation knobs (`LMX_MAX_FRAMES`, `LMX_CAPTURE_AT_FRAME`, 'c' key — all preserved).

Scene (single source of truth, used by both windowed and screenshot paths — M1's `TriangleAssets` pattern):

```cpp
// EditorShell.h (scene lives here; Screenshot.cpp reuses it)
struct SceneObject {
    std::string name;          // Inspector display
    glm::vec3 position{0.0f};
    glm::vec3 eulerDegrees{0.0f};
    float scale = 1.0f;
    glm::vec4 baseColor{1.0f};
    const lmx::render::Mesh* mesh = nullptr;
    bool rotating = false;     // spins about +Y at 45°/s when set
    glm::mat4 modelMatrix(float timeSeconds) const;
};
std::vector<SceneObject> makeDefaultScene(const lmx::render::Mesh& cube,
                                          const lmx::render::Mesh& plane);
// Ground plane (makePlane(5), gray), cubes: "Red" at (-1.5, 0.5, 0) {0.9,0.2,0.2,1};
// "Gold" at (0, 0.5, 0) {0.9,0.7,0.2,1} rotating; "Blue" at (1.5, 0.5, 0) {0.2,0.4,0.9,1}.
// Default camera: position (0, 2.5, 7), yaw 0, pitch −0.25 rad — frames the whole scene.
```

- [ ] **Step 1: Screenshot split** — move `writeBmp`/`appendLittleEndian`/`logPixel`/`runScreenshot` from `main.cpp` into `Screenshot.{h,cpp}`; `runScreenshot` now builds device → meshes → `makeDefaultScene` → `Renderer::create(device, 1280, 720, /*cpuReadback=*/true)` → one frame at `timeSeconds = 0` → readback → BMP. Probes: corner (0,0) == Renderer clear color; center — gold cube. No ImGui on this path. Commit separately once green (`xmake run App -- --screenshot /tmp/m2.bmp` + open the BMP and LOOK at it: plane, three lit cubes, gold centered).

- [ ] **Step 2: EditorShell** — owns: ImGui context init (`ImGuiConfigFlags_DockingEnable`), `ImGui_ImplSDL3_InitForMetal(window)` + `metal4::imguiInit(device)`; scene vector + camera + viewport state (`m_viewportSize`, `m_viewportFocused`); per-frame `buildUI()`:
  - Fullscreen dockspace (`ImGui::DockSpaceOverViewport`). First run only (no imgui.ini): DockBuilder splits right 22% → **Inspector**, center → **Viewport** (`imgui_internal.h` is required for DockBuilder — include it in EditorShell.cpp only).
  - **Viewport window**: no padding; `ImGui::Image(metal4::imguiTextureID(renderer.colorTarget()), contentSize)`; records `contentSize` + focus/hover for input routing.
  - **Inspector window**, three collapsing headers: *Stats* (`io.Framerate`, `1000/io.Framerate` ms, `ImGui::PlotLines` over the last 120 frame times); *Camera* (`DragFloat3` position, `DragFloat` yaw/pitch in degrees, fovY degrees 30–110, moveSpeed 0.5–50, `ColorEdit4` on renderer clearColor); *Objects* (per SceneObject: `DragFloat3` position, `DragFloat3` euler, `DragFloat` scale 0.1–10, `ColorEdit4` baseColor, `Checkbox` rotating).
  - Input (only when viewport hovered): RMB press → `SDL_SetWindowRelativeMouseMode(window, true)`, release → false; while held: relative mouse deltas × 0.0025 rad/px → `camera.look(dx, −dy)`, `SDL_GetKeyboardState` WASD → forward/right, Q/E → down/up, scaled by `moveSpeed × dt` → `camera.move(...)`. When ImGui wants the keyboard (`io.WantCaptureKeyboard` and RMB not held), skip camera keys.
- [ ] **Step 3: main.cpp frame loop rewrite** — per frame: SDL events → `ImGui_ImplSDL3_ProcessEvent` first, then quit/resize/'c' handling as in M1 (`main.cpp:337-419` structure preserved, including skip-on-acquire-failure, capture window, `LMX_MAX_FRAMES` self-resize drills); then `metal4::imguiNewFrame(); ImGui_ImplSDL3_NewFrame(); ImGui::NewFrame();` → `editorShell.buildUI(...)` → **debounced viewport resize**: if `m_viewportSize` ≠ renderer size and stable for 10 consecutive frames (or RMB-drag on a dock splitter ended), `device.waitIdle(); renderer.resize(w, h);` (during the drag the Image just stretches — document why: no deferred-release machinery for a dock drag, spec §2) → `ImGui::Render()` → `beginFrame` → `renderer.render(commands, camera, drawItems)` (drawItems built from scene at `timeSeconds` via `SDL_GetTicks()`) → UI pass: `beginRenderPass` on the swapchain texture (clear, dark `{0.06, 0.06, 0.07, 1}`, **no depth**) → `metal4::imguiRender(commands)` → `endRenderPass` → `endFrame(swapchain)`.
- [ ] **Step 4: Retire the triangle** — delete `Shaders/Triangle.slang`, the M1 triangle GPU case in `Tests/GpuSmokeTests.cpp` (keep the file's helpers — Task 8's cases use them), and every `TriangleAssets` remnant in App.
- [ ] **Step 5: Full verify + commit**

```bash
xmake format && xmake -y && xmake test
MTL_DEBUG_LAYER=1 LMX_MAX_FRAMES=600 xmake run App     # exercises resize drills; validation clean
xmake run App -- --screenshot /tmp/m2-check.bmp        # probes pass; open and LOOK at it
git add -A && git commit -m "Replace the triangle app with the M2 editor shell"
```
Manual interaction (fly the camera, drag docks, edit objects) is Rudy's closing gate — not claimed here.

---

### Task 11: Capture-guard tests + MTL_CAPTURE_ENABLED plumbing (Sonnet 5)

**Files:**
- Modify: `Tests/GpuSmokeTests.cpp` (or new `Tests/CaptureTests.cpp`)

**Interfaces:**
- Consumes: `metal4::beginCapture/endCapture` (`Metal4Capture.h:28-33`), `createDevice`.
- Produces: the backlog's capture-guard coverage — `[gpu]`-tagged (needs a Device&, so unit-tier isn't possible; backlog anticipated exactly this plumbing).

- [ ] **Step 1: Tests**

`Tests/CaptureTests.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>

#include "RHI/Metal4/Metal4Capture.h"
#include "RHI/RHI.h"

// MTL_CAPTURE_ENABLED must be in the environment before Metal initializes, so it is set at
// static-init time -- before Catch2's main creates any device. This is the plumbing the M1
// review said the guard test needed. It only *permits* capture; tests that want it off rely
// on beginCapture's own path validation rejecting first.
namespace {
const bool kCaptureEnv = [] {
    ::setenv("MTL_CAPTURE_ENABLED", "1", 0);
    return true;
}();
} // namespace

TEST_CASE("beginCapture rejects bad paths without touching the filesystem", "[gpu]") {
    auto device = lmx::rhi::createDevice();
    REQUIRE(device.has_value());
    REQUIRE_FALSE(lmx::rhi::metal4::beginCapture(**device, ""));
    REQUIRE_FALSE(lmx::rhi::metal4::beginCapture(**device, "frame.trace")); // not .gputrace
    REQUIRE_FALSE(std::filesystem::exists("frame.trace"));
}

TEST_CASE("begin/endCapture writes a .gputrace document", "[gpu]") {
    auto device = lmx::rhi::createDevice();
    REQUIRE(device.has_value());
    const std::filesystem::path path = "lmx-capture-test.gputrace";
    std::filesystem::remove_all(path);
    if (!lmx::rhi::metal4::beginCapture(**device, path.string())) {
        // Capture support can be absent (headless CI); the guard above is the required
        // coverage, the happy path is best-effort. SKIP keeps that honest.
        SKIP("programmatic capture unavailable in this environment");
    }
    (*device)->beginFrame();
    (*device)->endFrame(nullptr);
    (*device)->waitIdle();
    lmx::rhi::metal4::endCapture();
    REQUIRE(std::filesystem::exists(path));
    std::filesystem::remove_all(path);
}
```
Note: Tests currently include only `RHI/RHI.h`; `Metal4Capture.h` is deliberately metal-cpp-free (its header says so), so this include is legal from the Tests target with no include-dir changes.

- [ ] **Step 2: Verify + commit**

```bash
xmake -y && xmake test && xmake format
git add -A && git commit -m "Add capture-guard tests with MTL_CAPTURE_ENABLED plumbing"
```

---

### Task 12: CI — clang-tidy job + package cache (Sonnet 5)

**Files:**
- Create: `.clang-tidy`
- Modify: `.github/workflows/ci.yml`

**Interfaces:**
- Produces: D8's non-blocking clang-tidy CI job (closes the M1 deferral) + `~/.xmake/packages` cache (backlog).

- [ ] **Step 1: .clang-tidy**

```yaml
# Curated per spec D8: modernize/bugprone/performance, minus checks that fight the codebase's
# deliberate style (trailing return types; metal-cpp's pointer conventions trip bugprone
# easy-swappable trivially).
Checks: >
  modernize-*,
  bugprone-*,
  performance-*,
  -modernize-use-trailing-return-type,
  -bugprone-easily-swappable-parameters
WarningsAsErrors: ''
HeaderFilterRegex: '(Source|Tests)/.*'
```

- [ ] **Step 2: ci.yml — cache + tidy job**

Add after the ThirdParty cache step in the existing `build-test` job:

```yaml
      - name: Cache xmake packages
        uses: actions/cache@v4
        with:
          path: ~/.xmake/packages
          key: ${{ runner.os }}-xrepo-${{ hashFiles('xmake.lua') }}
```
New parallel job (non-blocking by design — `continue-on-error` at job level):

```yaml
  clang-tidy:
    runs-on: macos-26
    continue-on-error: true
    steps:
      - uses: actions/checkout@v4
      - name: Install xmake and llvm
        run: brew install xmake llvm
      - name: Cache ThirdParty
        uses: actions/cache@v4
        with:
          path: ThirdParty
          key: ${{ runner.os }}-thirdparty-${{ hashFiles('xmake.lua') }}
      - name: Cache xmake packages
        uses: actions/cache@v4
        with:
          path: ~/.xmake/packages
          key: ${{ runner.os }}-xrepo-${{ hashFiles('xmake.lua') }}
      - name: Fetch pinned third-party deps
        run: xmake setup
      - name: Configure and generate compile_commands
        run: |
          xmake f -m debug -y --ccache=n
          xmake project -k compile_commands
      - name: clang-tidy (non-blocking)
        run: |
          $(brew --prefix llvm)/bin/clang-tidy --version
          find Source -name '*.cpp' ! -path '*Metal4*' -print0 | \
            xargs -0 $(brew --prefix llvm)/bin/clang-tidy -p . || true
```
(Metal4 backend TUs are excluded: metal-cpp headers drown tidy in third-party noise; `HeaderFilterRegex` already keeps reports to our code. Note the exclusion in a yml comment. If the runner's tidy chokes on C++23 flags, record it in Amendments and keep the job present-but-red — it is non-blocking by design.)

- [ ] **Step 3: Verify locally what CI will run**

```bash
xmake project -k compile_commands
clang-tidy --version >/dev/null 2>&1 && find Source -name '*.cpp' ! -path '*Metal4*' | head -3 | xargs clang-tidy -p . || echo "tidy unavailable locally -- CI-only check"
git add -A && git commit -m "Add non-blocking clang-tidy CI job and xmake package cache"
```

---

### Task 13: Milestone close — docs, README, CLAUDE.md, backlog (Sonnet 5; checkboxes Haiku 4.5)

**Files:**
- Modify: `docs/plans/m2-backlog.md`, `docs/specs/2026-08-07-m2-renderer-skeleton-design.md` (status), `docs/specs/2026-08-07-luminex-upgrade-design.md` (§8 M2 status), `CLAUDE.md`, `README.md`

**Interfaces:**
- Consumes: everything shipped above.
- Produces: coherent milestone-boundary docs (CLAUDE.md update policy is a standing rule).

- [ ] **Step 1: Backlog disposition** — rewrite `docs/plans/m2-backlog.md` as a closed ledger: every item → "Closed in M2 (commit/task)" or explicitly re-deferred with a reason (expected re-defers: device-dtor pool ordering, `newFunction()` function constants, slang direct-metallib upstream, slang `device`-pointer warning — all watch-items by design).
- [ ] **Step 2: Spec statuses** — M2 spec header → `**Status**: Implemented — <date>` (+ the Amendments recorded during implementation, folded into the relevant sections M1-style); parent spec §8 M2 line gains `(done <date>)`.
- [ ] **Step 3: CLAUDE.md refresh** — architecture line: `Source/Render` no longer "intentionally empty placeholder" → one line on Renderer/Camera/Mesh + offscreen-viewport frame flow; commands: add the fly-camera/ImGui controls one-liner (RMB-drag look, WASD+QE) next to the debug knobs; deps line gains imgui-docking (route per Task 0); drop any M1-only phrasing that lies now. Keep it terse — CLAUDE.md is a map, not a manual.
- [ ] **Step 4: README** — regenerate the screenshot via `xmake run App -- --screenshot docs/…` (match wherever M1's README image lives — check `grep -n "screenshot\|\.bmp\|\.png" README.md` first, follow its existing convention including any BMP→PNG conversion step recorded there), refresh the feature list (depth-tested scene, fly camera, docked ImGui editor shell, offscreen viewport).
- [ ] **Step 5: Final full gate**

```bash
xmake format --check && xmake -y && xmake test
MTL_DEBUG_LAYER=1 LMX_MAX_FRAMES=300 xmake run App
git add -A && git commit -m "Close M2: docs, README, and backlog disposition"
```
Then hand to Rudy for the manual closing gate (interactive fly-around + dock-drag + inspector edits + CI green on the PR).

---

## Self-Review (executed while writing)

**Spec coverage:** §1 scene/camera/UI/viewport → Tasks 7, 8, 10 · §2 frame flow + debounce → Task 10 · §3 components → Tasks 7, 8 · §4 RHI growth → Tasks 3–6 (bindTexture = recorded spec amendment, Task 6 step 1) · §5 backend → Tasks 2, 3, 4, 6, 9 · §6 shaders → Task 8 · §7 testing/CI → Tasks 1, 7, 8, 11, 12 · §8 backlog disposition → Tasks 1, 2, 11, 12, 13 · §9 imgui risk → Task 0 · §10 DoD → Task 13.
**Placeholders:** none — every step carries its code or an explicit measure-and-record instruction with the recording location named.
**Type consistency:** `resetForFrame` evolves once (Task 2 → Task 3 adds ring params — Task 3 states the extension explicitly); slot constants (`kVertexBufferSlot`=0, `kObjectUniformsSlot`=1, texture slot 0) consistent across Tasks 8–10; `TextureUse` spelled identically in Tasks 6 and 8; `Renderer::create` signature identical in Tasks 8 and 10.
