# RojoRHI

**Status**: Implemented

RojoRHI is Luminex's dependency-free rendering hardware interface, mounted at the repository
root rather than under `Source/`. Engine, Scenes, Render and App consume its public value API;
Core and Asset remain independent. The [module contract](../conventions/modules.md) owns which
units may include its headers. This page describes the consumer boundary; the component's
[architecture](../../RojoRHI/docs/architecture/overview.md) owns backend implementation details.

## Component boundary

`RojoRHI/` mounts the Apache-2.0 `rojo-rhi` repository as a git submodule. Its pin must be reachable
from that repository's `main`; `Tools/check_submodule_pin.py` enforces this. A Luminex commit never
edits files inside the mount: changes land upstream first, then arrive through a pin bump
([ADR 0024](../decisions/0024-rhi-relocation-to-rojorhi.md)).

The root includes `RojoRHI/xmake/targets.lua`, linking `RojoRHI` for the core API and
`RojoRHIImGui` for the app-shell adapter. The component still declares its frozen Legacy targets,
but no Luminex target links them. Asset and TextureBake have no RHI include permission or
dependency. Hosted CI selects `--rojorhi_backend=null`; Metal 4 builds and GPU execution use the
local device. The component's own checkers run in its repository and CI, separately from Luminex
policy. See the [component build](../../RojoRHI/AGENTS.md) for standalone commands.

On this integration branch, `xmake/port.lua` selects a scene-clear → display → UI frame.
SCENE, VISIBILITY, LIGHTS, POST and TEMPORAL remain pending, and BENCH keeps FrameDataBench
dormant. The retained stage algorithms in the [frame walkthrough](frame-pipeline.md) describe
those families, not the active subset. Screenshots retain neutral lighting metadata; headless
and editor measurement report lighting diagnostics unavailable while LIGHTS is pending.
Minimal-frame timings do not establish full-renderer parity or cost acceptance.

## Public interface

`RojoRHI/Include/rojoRHI/RHI.h` is the dependency-free public core header. Handles such as
`Texture`, `Buffer`, `Pipeline`, `Queue` and `Fence` are values; they do not own objects or expose
backend types. Creation returns a checked result. Debug valid-usage checks diagnose misuse;
Release callers must still satisfy the same interface contracts.

`Engine/Gpu/GpuContext` owns the device, graphics queue, frame and idle fences, shader loading and
copied retired timings. It outlives every GPU owner. `GpuOwned<T>` destroys a handle through its
device; native release is deferred. `GpuTexture` retains its descriptor and sampled/per-mip
storage views, releasing views before the texture. `TextureRef` borrows the handle, sampled view,
extent, format, mip/layer counts and immutable creation layout. Creation with initial data means
ShaderRead; otherwise it means Undefined. Neither fact substitutes for current physical state.

`GpuBuffer` retains its descriptor, mapped pointer and GPU `Addr` where its memory class permits
one. Three paced `Memory::Upload` buffers per scene-table kind receive CPU writes through
`cpuPointer()` only after their slot retires. Mapping performs no synchronization. Persistent
shader data is addressed through `Addr`; transient constants use `uploadFrameData` and
`pushRoot`, with module-owned bindings and view/sampler indices described in [shaders](shaders.md).

Readback uses `Render/Gpu/Readback`: recorded tickets preserve causal failures, `resolve` returns
`Result<void>`, and `readbackNow` returns checked bytes. Textures supply exact current table/range
states; copies restore those frontiers and de-pad rows. Buffers supply an explicit `SyncPoint`.
Mapped Upload/Readback reads wait without consuming a copy frame; staged GPU reads require source
lifetime until retirement. A Readback buffer is never used as a copy source.

## Metal 4 backend

Luminex selects Metal 4 on macOS and requires the offline Metal toolchain. The mounted component
owns native translation, frame arenas, residency, deferred destruction, scope timestamps and
capture. Its [Metal backend](../../RojoRHI/docs/architecture/metal4-backend.md) and
[recording contract](../../RojoRHI/docs/architecture/metal4-recording.md) describe those mechanisms.
No backend handle enters Luminex's core or engine interfaces.

`RojoRHIImGui` exposes `<rojoRHI/ImGuiAdapter.h>` only to app-shell. `EditorSurfaces.mm` supplies
`SurfaceCallbacks`: `createSurface` resolves SDL's window ID, creates an `SDL_MetalView`, and
returns its layer; `destroySurface` releases the matching view. The adapter owns detached-window
swapchains and synchronization, while SDL owns the native windows. UI texture IDs come from
`imgui::textureId` using the owner's sampled view.

## Luminex-side integration

`Render/Common/RhiLog` installs `setMessageCallback` and forwards severity/text messages into
spdlog and Console. Core has no dependency on RojoRHI, and RojoRHI has none on Core.

The windowed loop keeps surface creation, resizing and platform-window lifecycle outside device
frames. Its order is:

1. Poll SDL events, applying every positive main pixel-size event to `Swapchain::resize`, including
   same-size events. Query the current pixel extent again and resize if it differs from the last
   resized extent. Apply pending scene-target resize before building UI.
2. Call `imgui::newFrame`, `ImGui_ImplSDL3_NewFrame`, `ImGui::NewFrame`, build UI and `ImGui::Render`.
3. Call `GpuContext::beginFrame`, which opens the device frame and acquires a graphics list, then
   acquire the main drawable. A zero extent or null image still submits the list and ends the
   frame, joins retired timings, cancels a contiguous interactive measurement and retains capture
   intent. The attempt consumes a real frame number and counts as skipped; nothing is presented.
4. After successful acquire, start a requested capture, prepare scene data and declare the graph.
   Import the drawable as a value ref in Undefined and sink it as Present. Call `imgui::prepare`
   before graph execution; the graph's UI pass calls `imgui::render` inside its render pass.
5. Record `imgui::renderPlatformWindows` on the same list, call `gpu.submit`, `gpu.present(main)`,
   `imgui::presentPlatformWindows(gpu.graphicsQueue())`, then `gpu.endFrame` and join timings.
6. For a successful capture, wait for its submitted frame and end it exactly once. Call
   `ImGui::UpdatePlatformWindows` outside the frame, on normal and skipped paths alike.

SDL can change the Metal layer's `drawableSize` synchronously inside `SDL_SetWindowSize`, before
the pixel-size event arrives. The per-attempt extent comparison closes that gap. Luminex never
writes `drawableSize` directly. The adapter sizes detached swapchains from the actual platform
window size; the host needs no workaround on ImGui viewport fields.

Every acquired list is submitted and every opened frame ends, including preparation failures.
Shutdown waits for GPU work, destroys detached windows and shuts down the adapter before its
ImGui context, then releases renderer/pool owners and the main swapchain before the GPU context.
The main SDL Metal view and native window outlive context teardown.

`GpuContext::submit` signals the frame fence with its frame number. Present occurs after submit
and before `endFrame`; `submitAndEndFrame` combines those operations for headless or skipped
frames. `waitIdle` uses a separate empty submission and idle fence outside a frame. `endFrame`
copies retired scope timings; App joins them immediately by their actual retired ID after every
frame end, including private readback-copy frames. Render-owned sequence/temporal ordinals remain
separate from these pacing IDs. Measurement drains submitted tails before validating complete
joins; schema 5 continues to report the timed-pass sum.

## Tests

Pure graph tests inspect values and lowered commands; device cases use real owning contexts.
Named `Tests/unit` and `Tests/gpu` groups separate hosted Null checks from hardware execution;
Metal `Tests/unit` also contains device cases. Hosted `build-test` uses Release Null; the `tracker`
job configures Debug Null and builds Tests, then requires each of the two Renderer tracker names
exactly once in Catch JSON discovery before executing `[tracker]`. The alias case alone cannot
satisfy discovery. Release tracker cases skip before device creation and are not passing executions.

For local tracker execution, configure `xmake f -m debug -y -P . --rojorhi_backend=null`, build
`xmake build -P . Tests`, then run `./Tests "[tracker]"` from
`build/macosx/arm64/debug/test`. The minimal-frame case covers empty and material-lab scenes; the
cull/restore case omits the display read on the third of four real frames and checks that both
logical and physical state persist before the read returns. Its UI-shaped draw reads display
through FullscreenSample into a persistent exported offscreen target, without presentation.

The retained frozen component suite uses `RojoRHILegacyTests`, separately from current consumer
acceptance. The [test ledger](../milestones/r/r5-tests.md) records pending families, explicit
replacements and measured acceptance limits.

Tracker observations come from RHI commands and truthful per-command shader resource declarations,
not graph read declarations alone. Nested frame-arena data without exposed handles and opaque
adapter texture-ID shader reads are not fully declared; a clean tracker run alone cannot prove
complete shader coverage. Physical-state tests and native GPU evidence cover complementary risks.
