# RojoRHI

**Status**: Implemented

RojoRHI is Luminex's rendering hardware interface: a dependency-free API over Metal 4, with D3D12
as a design target. It is a repository-root component rather than a `Source/` unit. Engine and
Render link it directly, outside the Core-based stack; which Luminex units may include which
RojoRHI headers is the `externals` contract in [modules](../conventions/modules.md) (Asset and
TextureBake see only `Format.h` and `TextureDesc.h`). RojoRHI's own [architecture
page](../../RojoRHI/docs/architecture/overview.md) owns its directory layout, its standalone
`xmake -P RojoRHI` build, and its private base that replaces a Core dependency. This page covers
the Luminex side of the boundary, and the parts of the public interface and Metal 4 backend that
RojoRHI's page leaves out.

## Component boundary

`RojoRHI/` mounts the separately published, Apache-2.0 `rojo-rhi` repository as a git submodule,
pinned to a commit reachable from that repository's `main`; `Tools/check_submodule_pin.py` fails
the policy check otherwise. Under the two-repository rule, a Luminex commit never edits a file
under `RojoRHI/`; an RHI change is a `rojo-rhi` commit that reaches Luminex as a pin bump
([ADR 0024](../decisions/0024-rhi-relocation-to-rojorhi.md)). The repository root includes only
`RojoRHI/xmake/targets.lua` from the component. RojoRHI's own `xmake.lua`, which the root never
includes, carries the standalone root settings and includes `xmake/setup.lua`, `xmake/tasks.lua`
and `xmake/targets.lua`; `targets.lua` itself includes the component's `shaders.lua` rule, so a
host that includes it gets the rule with the targets. Through `targets.lua` the root build compiles
the RojoRHI targets, and the root `xmake test` runs the `RojoRHITests/unit` and `RojoRHITests/gpu`
groups beside Luminex's own. The root `policy` task checks only the pin; RojoRHI's own checkers and
format task run from inside the mount (`-P .`) and in `rojo-rhi`'s CI.

RojoRHI grows only when a rendering feature supplies a real portability requirement. Metal 4 is the
first implementation, and no accepted contract exposes a native handle to callers
([RojoRHI ADR 0003](../../RojoRHI/docs/decisions/0003-thin-rhi.md)). D3D12 is the intended second
production backend; Vulkan remains research evidence rather than a planned target
([RojoRHI ADR 0004](../../RojoRHI/docs/decisions/0004-second-backend-target.md)).

## Public interface

RojoRHI's public headers live under `RojoRHI/Include/rojoRHI/`; each leaf compiles alone, split by
owner concept: `GpuAddress.h`, `Format.h`, `Buffer.h`, `Texture.h`, `Heap.h`, `Sampler.h`,
`ShaderLibrary.h`, `GraphicsPipeline.h`, `ComputePipeline.h`, `Indirect.h`, `RenderPass.h`,
`CommandList.h`, `TemporalScaler.h`, `Swapchain.h`, and `Device.h`, plus `Result.h` and
`Message.h`, behind an includes-only `RHI.h` umbrella that declares no parallel surface.
`TextureDesc.h` is reached through `Texture.h`; `Validate.h` and `CaptureSchema.h` stand outside
the umbrella, and `rojoRHI/Metal4/` holds two backend-scoped headers (`Metal4Capture.h`,
`Metal4FrameData.h`) that carry plain diagnostic values. Together the public headers expose
API-neutral resource, pipeline, command, synchronization, capture, and domain-owned error
contracts, with no Metal or ImGui type reachable from any of them.

Transient per-frame parameter delivery is one typed operation, `CommandList::bindFrameData(slot,
value)`: it allocates, copies, and binds the caller's block in one call and returns a `GpuAddress`,
a standard-layout, arithmetic-free value naming the block's GPU location for the open frame. Data
that survives the frame is bound as a buffer through `bindBuffer`, not copied through the
frame-data arena. [ADR 0010](../decisions/0010-execution-model-partial-reshape.md) chose this
address-first path while keeping the object-shaped resource, pass, pipeline, residency, and barrier
model, and the render graph's logical ownership, restated for the component as
[RojoRHI ADR 0006](../../RojoRHI/docs/decisions/0006-execution-model.md).

`BufferDesc::cpuWrite` enables a checked `Buffer::write(offset, data, size)` host upload into
host-visible memory: the call requires non-null data and a nonempty in-bounds range, and the caller
must prove every GPU reader and writer of that range has already retired. The Metal 4 backend
copies into shared storage; a device-private placed buffer rejects the flag. Paced scene tables are
the first consumer.

`RenderPassDesc` and `GraphicsPipelineDesc` support up to `kMaxExtraColorTargets` (3) additional
colour attachments beyond the primary, validated for colour-renderable formats and a matching
extent; `RG16Float` and `R8Unorm` are colour-renderable and CPU-readable, which is what
motion-vector and reactive-weight targets need. `RenderPassDesc` also carries an origin-anchored
`renderAreaWidth`/`Height` (default 0/0, meaning the whole attachment), validated against every
attachment and encoded as an explicit Metal 4 viewport and scissor when non-zero.

`DeviceCapabilities::temporalScaler` reports an optional reconstruction algorithm and its
input/output scale interval; `TemporalScaler` owns private reconstruction history, and the
between-pass `CommandList::temporalScale` consumes neutral frame parameters. `R16Float` is sampled
and storage-writable for the exposure texel; no MetalFX type enters a public header.

## Metal 4 backend

`RojoRHI/Backends/Metal4` implements the current backend with private metal-cpp headers, keeping
three frames in flight ([RojoRHI ADR 0002](../../RojoRHI/docs/decisions/0002-metal4-first.md)) over
argument tables whose texture slots clear before each render or compute pass. A per-frame-slot
growable frame-data page arena backs `bindFrameData`: normal pages are 256 KiB, an oversize request
rounds up to that page quantum, and pages stay mapped and resident until device destruction, so a
slot's high-water mark becomes its reused capacity instead of being released. The backend also owns
residency tracking, shared-event pacing, render, compute and copy pass encoders, indirect draws and
dispatches, untracked placement heaps whose resources are created at explicit offsets, per-pass GPU
timing, and capture support.

Its MetalFX temporal scaler translates reciprocal scale units, hands work across opaque encoders
through a public fence, and retains state in every encoded frame slot until retirement;
CPU-readable outputs use a creation-time private scratch buffer copied inside the same timed call.

The optional `RojoRHIMetal4ImGui` target owns the ImGui adapter, with sources under
`RojoRHI/Backends/Metal4/ImGui/Source/`, its ImGui-dependent public extension header, and the
dependency on Dear ImGui; the core RHI does not inherit any of them
([RojoRHI ADR 0003](../../RojoRHI/docs/decisions/0003-thin-rhi.md)).

## Luminex-side integration

RojoRHI has no dependency on Core, and Core has none on RojoRHI (its private base is on RojoRHI's
page). Diagnostics leave
through `rojoRHI/Message.h`'s severity/text callback, which writes to stderr when unset
([RojoRHI ADR 0003](../../RojoRHI/docs/decisions/0003-thin-rhi.md)). `Render/Common/RhiLog`
installs the process-wide sink and forwards each message into spdlog and the Console, mapping
Info, Warning, and Error onto the matching project log level, for App and Luminex's `Tests` binary.

## Tests

- `RojoRHI/Tests` (`RojoRHITests`) holds RojoRHI's own contract and GPU conformance suite; it links
  only the `RojoRHI` target and no other project library.
- `Tests/Render/Renderer/CaptureSchemaTests` also holds four cases asserting RojoRHI's
  `CaptureSchema` contract from the Luminex side.
- `Tests/Engine/Scene/GpuSceneTableAbiTests` also holds two cases asserting the scene-table upload
  contract at the RojoRHI boundary.
