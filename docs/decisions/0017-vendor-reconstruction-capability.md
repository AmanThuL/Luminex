# ADR 0017: Capability-selected temporal reconstruction

**Status**: Accepted (2026-09-12)

## Context

The engine already owns motion, jitter, depth, exposure, output-capacity allocations and two
interchangeable colour-history slots. A vendor algorithm must consume that contract without
changing scene rendering or the native reference kernels. The
[design](../specs/2026-09-12-m6.4-metalfx-temporal-adapter-design.md) supplies the boundary;
this decision records the backend findings and adapter conventions that implement it.

## Decision

`Device::capabilities()` exposes an immutable `DeviceCapabilities::temporalScaler` with
availability, a device-lifetime display name and supported input/output scale bounds.
`Device::createTemporalScaler` creates an owned `TemporalScaler`; the between-pass
`CommandList::temporalScale` consumes its descriptor-matching textures and frame parameters.
Public headers remain vendor-neutral. The Metal 4 backend implements the object with MetalFX;
unsupported devices and failed creation select `NativeTaa`. Creation failure is remembered for
the failed output extent and retried after resize, with one warning per failed attempt.

The backend queries `supportsMetal4FX`. Apple's scale bounds express output/input, so it exposes
their reciprocals in exchanged order and reverses that conversion when configuring a scaler.
The adapter clamps requested scale to the intersection of this capability and the engine's
`[0.5, 1.0]` interval. Input capacity stays at output extent; only the origin-anchored content
rectangle changes with render scale. Subpixel aspect differences from integer extent rounding
are accepted. Scaler creation is synchronous, with dynamic content and reactive masks enabled
and vendor auto-exposure disabled.

`PassKind::External` and `ExternalPassDesc` declare texture reads and writes for an RHI-owned
operation. `ExternalRead`/`ExternalWrite` participate in the same versioning, dependency,
culling, barrier, transient-lifetime and compiled-record rules as other texture uses. Execution
opens no render or compute scope around the callback. The external operation owns its encoder
scopes and GPU timing; `GraphDump` prints `external`, and the editor displays its own pass colour.
`Format::R16Float` adds the sampled, storage-writable exposure texel the adapter needs.

`TemporalResolve` keeps slot ownership, native kernels, diagnostics and terminal-use bookkeeping.
Its composed `VendorTemporalScaler` replaces only the reconstruction kernel step. A virtual
reconstruction interface would duplicate these shared responsibilities for two implementations
without a second ownership model; the internal kernel branch is the smaller boundary.

## Adapter conventions

The vendor path declares `lmx.pass.temporal.vendor.pack` and `lmx.pass.temporal.vendor`.
Packing reads engine motion, reactive weight and the exposure pair, then writes output-capacity
`lmx.render.vendorMotion` (`RG16Float`), `lmx.render.vendorReactive` (`R8Unorm`) and the 1×1
`lmx.render.vendorExposure` (`R16Float`) as one-frame transients. The external pass reads these
alongside scene colour and current depth, then writes this frame's engine colour slot.

| Parameter | Adopted mapping |
|---|---|
| Motion | Preserve UV deltas; multiply by `(-renderWidth, -renderHeight)` to point to the previous position in pixels |
| Invalid motion | Replace the infinity sentinel with zero motion and reactive weight 1 |
| Jitter | `jitterTexelOffset(jitterPixels)`, in input texels with positive y down |
| Content | Active render width and height inside output-capacity inputs |
| Depth | Reversed Z |
| Exposure texel | `1 / applied`, read from the GPU exposure pair |
| `preExposure` | 1 |
| Reset | Engine reset reason is not `None`, previous effective mode was not vendor, or scaler was recreated |

MetalFX's [exposure texture](https://developer.apple.com/documentation/metalfx/mtlfxtemporalscalerbase/exposuretexture)
multiplies the input for its working exposure. Its
[pre-exposure parameter](https://developer.apple.com/documentation/metalfx/mtlfxtemporalscalerbase/preexposure)
divides out a fixed input multiplier. Engine colour already carries the GPU's applied exposure;
the reciprocal texel cancels that factor in the vendor's working domain without a CPU readback.
The reconstructed output empirically retains the engine's pre-exposed domain. The original
`applied` texel hypothesis failed the frozen exposure-step bound; the reciprocal mapping passes
that same bound. This is an observed adapter convention, not access to MetalFX's private history
or a guarantee of pixel equality with native reconstruction.

Engine colour/depth slots remain valid across `NativeTaa ↔ VendorTemporal`: each mode writes a
real colour frame and the scene writes current depth. Switching alone therefore derives `None`
and continues engine history age. MetalFX's independent history resets on entry. A temporal-off
frame counts as leaving vendor mode. Output resize recreates the scaler and resets both histories;
render-scale change alone does neither. Status reports effective mode, fallback, vendor name,
last vendor reset and scaler generation. Native TAA remains the default and reference.

`MotionVectors` and `ReprojectionError` keep their existing engine diagnostics.
`ReprojectedHistory` adds `lmx.pass.temporal.reprojectedHistory` only when selected under vendor
mode: `VendorTemporalHistory.slang` applies the native motion/depth reprojection and exposure
correction subset without accumulation. The existing error diagnostic cannot supply that corrected
history image. The frozen native shaders remain unchanged. Rejection, blend-weight and per-pixel
age views require native kernel internals and are unavailable under vendor mode; the CPU history
age remains the engine's frames-since-reset count.

## Metal 4 encoding and lifetime

A barrier-only carrier did not satisfy MetalFX's opaque encoder handoff: it asserted that output
barrier stages were unset. The backend uses the scaler's public fence property. A short compute
carrier consumes pending dependencies, establishes all-stage queue visibility, updates the fence
at dispatch/blit stages and ends. MetalFX waits and updates the fence; the next consumer waits
before accessing its output. External texture uses map to all stages.

The measured required texture usages are `ShaderRead` for colour, depth, motion and reactive
inputs, and `ShaderRead | ShaderWrite | RenderTarget` for output. MetalFX requires private output
storage. A CPU-readable RHI output receives a copy from an owned private scratch texture inside
the same timed external call; the scratch is allocated when the scaler is created. Ordinary
private outputs are written directly. RHI-owned resources join the device residency set;
validation required no application registration of MetalFX's private allocations.

Every encoded frame slot retains the scaler state, including its fence and private scratch,
until that slot retires. Destroying or recreating the public object therefore cannot release
in-flight state. MetalFX has no public object label setter: the owned fence and command-buffer
debug groups carry the algorithm label, while vendor encoder labels are preserved. A nil factory
result carries a contextual RHI creation error; the upstream API supplies no NSError detail.

## Terminal access extension

This adds vendor rows to [ADR 0015](0015-temporal-slot-terminal-access.md); its native and Raw
rows are unchanged. Vendor colour deliberately preserves its opaque producer's conservative
all-stage write record even after display samples it, rather than narrowing the next import.

| Resource after a vendor frame | Recorded use |
|---|---|
| Current colour slot | `ExternalWrite` |
| Previous colour slot, when an engine history diagnostic reads it | `ShaderRead` |
| Previous colour slot otherwise | Retain its previous record |
| Current depth slot | `ExternalRead` |
| Other depth slot read by the corrected-history diagnostic | `ShaderRead` |
| Other depth slot otherwise | Retain its previous record |
| Renderer scene colour | `ExternalRead` |

A temporal-off frame imports depth slot zero with its recorded use before overwriting it; only
then does it record the ordinary `ShaderRead` contract. An unread slot cannot lose the all-stage
record its last vendor consumer established.

## Consequences

The vendor path is opt-in and backend-specific behind a neutral capability. Native frame
declarations, shaders, tolerances and checkpoint cases remain the reference. MetalFX owns opaque
internal allocation and image-quality behavior; its creation cost, private scratch and history
are additional to the graph's transient footprint. The
[milestone record](../milestones/m6.4.md) owns measurements and validation evidence;
[frame pipeline](../frame-pipeline.md) and [GPU debugging](../guides/gpu-debugging.md) own current
operation. [ADR 0016](0016-active-render-extent-and-resolution-control.md) continues to own render
extent and controller policy.
