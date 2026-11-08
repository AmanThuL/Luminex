# R5 Test Ledger

**Status**: Implemented

This ledger reconciles the frozen v1 suites with the suites that run on RojoRHI v2, as
[R5](r5.md) and its parity gate require: every v1 case that was renamed, split, merged or removed
is listed with its replacement, and the counts 1,197 unit and 229 GPU are carried to the final
counts line by line. Recorded on 2026-11-07 for the parity run at the integration branch tip.

## Method

The two arms were listed, not run, by Catch2 (`--list-tests -r xml`): the frozen v1 `Tests` binary
of the `r5-v1-reference` worktree, and the Release Metal 4 and Release Null `Tests` binaries of the
parity run, whose listings are identical. A unit case is any case without `[gpu]`; a GPU case
carries `[gpu]`; no listed case is hidden. A script diffs the listings by case name and checks every
removed name, added file, tag change, file move and skipped case against the tables below; it
fails on anything they do not account for. Its outputs (`v1-*.txt`, `v2-*.txt`, `diff.json`,
`check.txt`, "unexplained: 0") and the script are in the parity evidence,
`../rojo-rhi-evidence/luminex-port/P6.9/2026-11-06-42fd8920-os27.2/` under `ledger/` and
`scripts/ledger.py`. The run counts come from the same binaries (`tests/`, `tests/skips.md`).

A case added on Luminex `main` before the switch, by the seams of R5.2 and R5.3, is marked `main`;
a case added on the integration branch is marked `branch`. The ledger compares against frozen v1,
so a seam case that the branch renamed appears only under its final name (see
[Seam cases renamed on the branch](#seam-cases-renamed-on-the-branch)).

## Totals

| Suite | v1 | Removed | Added | v2 | Release Metal 4 | Release Null | Debug Metal 4 | Debug Null |
|---|---:|---:|---:|---:|---|---|---|---|
| Unit (`~[gpu]`) | 1,197 | 4 | 151 | 1,344 | 1,313 passed, 31 skipped | 1,320 passed, 24 skipped | 1,337 passed, 7 skipped | 1,344 passed |
| GPU (`[gpu]~[.]`) | 229 | 1 | 20 | 248 | 248 passed | not run (reads zeros by design) | 248 passed | not run |
| Tracker (`[tracker]`, within unit) | 0 | 0 | 24 | 24 | skipped (Debug only) | skipped (Debug only) | 24 passed | 24 passed |

So 1,197 − 4 + 151 = 1,344 and 229 − 1 + 20 = 248. Of the 171 added cases, 53 were added on `main`
before the switch and 118 on the branch. No case moved between the unit and GPU suites. GPU cases
ran under `MTL_DEBUG_LAYER=1`. v1 skipped no case; every v2 skip runs in another configuration
([Skipped cases](#skipped-cases)). Hosted CI runs Release unit on Null and Debug `[tracker]` on
Null.

## Removed and renamed v1 cases

| v1 case | Suite | v2 replacement | Reason |
|---|---|---|---|
| `CaptureSchema records frame-data ranges inside the frame window` (`Render/Renderer/CaptureSchemaTests.cpp`) | unit | `CaptureSchema records named root addresses inside the frame window` (`Engine/Gpu/CaptureSchemaTests.cpp`) | Capture schema version 2 records each frame root by name and address; v1's slot, arena-page and upload fields do not exist on v2. The window, deduplication and out-of-window drop checks remain. |
| `R32Float is exact sampled storage data without attachment capability` (`Render/Passes/Occlusion/HzbTests.cpp`) | unit | `R32Float sampled storage and reported attachment usage follow v2 capabilities` (same file, adds `[device]`) | v1 refused an unsupported render-target usage with a returned error; on v2 it is fatal misuse, so the case reads `formatCapabilities` and creates the attachment only where it is reported. |
| `execute emits no barrier for a texture only exported` (`Render/Graph/RenderGraphTransitionTests.cpp`) | unit | `export adds no barrier after the texture's producing pass` (same file) | v2 lowers the first use of a texture with an Undefined to ColorAttachment layout barrier, so "no barrier" is no longer true; the case now asserts that one and that export adds nothing after the producing pass. |
| `zero-live modes preserve graph and shader inputs across temporal cells` (`Render/Passes/LocalLights/RenderZeroLightInvariantTests.cpp`) | unit | `zero-live requested modes preserve the Off frame on a configured device` (same file, `[device]`) | The v1 case recorded through a fake command list, which v2 cannot subclass; the replacement replays the same opaque, masked double-sided and sky fixture through five Off, Native and Vendor cells on a real device. |
| `device-private placed buffers reject host upload permission` (`Engine/Scene/GpuSceneTableAbiTests.cpp`) | GPU | none in Luminex | Retired. The case asserted a returned `InvalidDesc` for a CPU-writable buffer placed in a device-private heap; on v2 that descriptor is fatal misuse. RojoRHI's death cases `VU-rojo-buffer-0003`, `VU-rojo-buffer-0014` and `VU-rojo-heap-0005` at the pinned commit cover it. |

The `CaptureSchemaTests.cpp` file of v1 is split: the replacement above and three generic schema
cases moved to `Engine/Gpu/`, two renderer registration cases to `RendererCaptureLayoutTests.cpp`
and the lighting case to `LightCaptureLayoutTests.cpp` ([file moves](#same-name-file-moves)).

## Same-name tag changes

| Change | Cases | Reason |
|---|---:|---|
| `+[device]` | 49 | The case creates real textures and buffers through `GpuContext` on the configured backend, Null on hosted CI and Metal 4 locally, where v1 subclassed a fake device. Names, sections and assertions are unchanged. |
| `-[checkpoint-a]` | 3 | [ADR 0033](../../decisions/0033-rojorhi-conformance-replaces-checkpoint-a.md) retires the tag. "a transient cannot read what the transient it replaced left behind" and "resizing and toggling transients leaks no heap generation" stay GPU cases; "a transient loaded as an attachment fails to compile" stays a graph case. |

The 49 are 15 scene export, 19 document save and repair, 5 graph execution and 4 vendor temporal
cases, and 6 app, scene-session, measurement, instantiation and capture-schema cases.

## Same-name file moves

| v1 file | v2 file | Cases | Reason |
|---|---|---:|---|
| `Tests/Render/Renderer/CaptureSchemaTests.cpp` | `Tests/Engine/Gpu/CaptureSchemaTests.cpp` | 3 | `CaptureSchema` moved from Render to Engine with the GPU layer |
| `Tests/Render/Renderer/CaptureSchemaTests.cpp` | `Tests/Render/Renderer/RendererCaptureLayoutTests.cpp` | 2 | full-renderer registration cases returned with visibility |
| `Tests/Render/Renderer/CaptureSchemaTests.cpp` | `Tests/Render/Passes/LocalLights/LightCaptureLayoutTests.cpp` | 1 | the lighting registration case returned with local lights |
| `Tests/Render/Passes/Occlusion/GpuOcclusionTests.cpp` | `Tests/Render/Passes/Temporal/GpuOcclusionTemporalTests.cpp` | 5 | cases that need temporal history live with the temporal family |
| `Tests/Render/Passes/Visibility/GpuVisibilityTests.cpp` | `Tests/Render/Passes/Temporal/GpuVisibilityTemporalTests.cpp` | 2 | as above |
| `Tests/Render/Passes/Visibility/GpuVisibilityContributionTests.cpp` | `Tests/Render/Passes/Temporal/GpuVisibilityContributionTemporalTests.cpp` | 2 | as above |
| `Tests/Engine/Scene/GpuSceneTableTests.cpp` | `Tests/Engine/Scene/GpuSceneTableTemporalTests.cpp` | 6 | as above |
| `Tests/Render/Passes/SelectionOutline/GpuSelectionOutlineTests.cpp` | `Tests/Render/Passes/Temporal/GpuSelectionOutlineTemporalTests.cpp` | 1 | as above |
| `Tests/Render/Passes/Scene/GpuAlphaMaskTests.cpp` | `Tests/Render/Passes/Temporal/GpuAlphaMaskTemporalTests.cpp` | 1 | as above |
| `Tests/Render/Passes/Exposure/GpuExposureBloomTests.cpp` | `Tests/Render/Passes/Temporal/GpuExposureTemporalTests.cpp` | 1 | as above |
| `Tests/Render/Passes/LocalLights/GpuLightDebugViewTests.cpp` | `Tests/Render/Passes/Temporal/GpuLightDebugTemporalTests.cpp` | 1 | as above |
| `Tests/Render/Passes/Scene/GpuRendererTests.cpp` | `Tests/Render/Passes/Exposure/GpuSceneTimingTests.cpp` | 1 | the twelve-row pass-timing case needs bloom and exposure |
| `Tests/Render/Passes/Shadow/GpuRendererShadowTests.cpp` | `Tests/Render/Passes/Shadow/GpuShadowSmokeTests.cpp` | 1 | the shadow smoke left the renderer-wide file with the shadow stage |

The moves come from the per-family port, which built one stage family at a time: a case that needs
a later family moved to a file of that family, with its name, tags, sections and assertions. The
temporal Off variants of the moved occlusion and visibility cases are the added
`GpuOcclusionOffTests.cpp`, `GpuVisibilityOffTests.cpp` and `GpuVisibilityContributionOffTests.cpp`.

## Added cases

Paths are under `Tests/`; Unit and GPU are counts.

| File | Unit | GPU | Origin | Purpose |
|---|---:|---:|---|---|
| `App/Model/Options/AppOptionsTests.cpp` | 3 | | branch | `--open-window` opens a detached window and rejects unknown names; `--rhi-validation` is refused in Release |
| `App/Model/Options/RunHooksTests.cpp` | 2 | | branch | frame-hook parsing and the smoke resize hook |
| `Core/Diagnostics/CoreResultTests.cpp` | 2 | | main | Luminex's own `Error` and `Result` |
| `Engine/Asset/EngineImageDataTests.cpp` | 1 | | main | `asset::ImageData` keeps mip ranges when copied |
| `Engine/Gpu/CaptureSchemaTests.cpp` | 1 | | branch | the named-root replacement above |
| `Engine/Gpu/EngineCaptureSchemaTests.cpp` | 1 | | main | uniform struct layouts reach the capture sidecar |
| `Engine/Gpu/EngineFormatInfoTests.cpp` | 2 | | branch | texel sizes and graph-dump format names Luminex now owns |
| `Engine/Gpu/EngineGpuContextTests.cpp` | 7 | | branch+main | pass timings under the frame scope, shader paths, upscaler support, a frame closed without present |
| `Engine/Gpu/EngineGpuErrorTests.cpp` | 2 | | branch | v2 error messages and codes map onto Luminex errors |
| `Engine/Gpu/EngineGpuOwnerTests.cpp` | 6 | | branch+main | GPU owners keep descriptors and labels, destroy once and move safely |
| `Engine/Upload/EngineDdsUploadTests.cpp` | 1 | | branch | DDS levels map to v2 texture data with row pitches |
| `Render/Gpu/RenderReadbackDeviceTests.cpp` | 4 | | branch | readback errors, empty readbacks, mapped Upload inspection and resize identities |
| `Render/Gpu/RenderReadbackGpuTests.cpp` | | 6 | branch | readback after the fence, de-padding, initialized color and depth, mixed mip states |
| `Render/Gpu/RenderReadbackTests.cpp` | 4 | | main | readback tickets, regions and packed sizes |
| `Render/Graph/BufferReadWriteTrackerTests.cpp` | 1 | | branch | in-place buffer reads ordered after writes under the tracker |
| `Render/Graph/GraphDumpTests.cpp` | 1 | | main | Luminex use names match the dump vocabulary |
| `Render/Graph/RenderGraphBufferPriorTests.cpp` | 4 | | branch | buffer state carried into the frame lowers to the right first barrier |
| `Render/Graph/RenderGraphLoweringTableTests.cpp` | 10 | | branch | the use-to-stage, access and layout table and its edge cases |
| `Render/Graph/RenderGraphLoweringTests.cpp` | 6 | | branch+main | raster, compute, copy and external passes lower to passes and scopes |
| `Render/Graph/RenderGraphTransientTests.cpp` | 1 | | branch | cached alias placements under the tracker for four frames |
| `Render/Graph/RenderGraphTransitionTests.cpp` | 1 | | branch | the renamed export case above |
| `Render/Graph/RenderTransientPoolTests.cpp` | 2 | | branch+main | placement reuse and deferred heap destruction |
| `Render/Graph/TextureStateTableTests.cpp` | 8 | | branch+main | persistent texture layouts across frames |
| `Render/Passes/Bloom/BloomFrameDataTests.cpp` | 3 | | main | pure bloom root builders |
| `Render/Passes/Bloom/BloomTrackerTests.cpp` | 2 | | branch | bloom toggles and resizes under the tracker |
| `Render/Passes/Bloom/GpuBloomTests.cpp` | | 1 | branch | a one-mip bloom output stays sampled data |
| `Render/Passes/Display/DisplayFrameDataTests.cpp` | 1 | | main | pure display root builder |
| `Render/Passes/Exposure/ExposureFrameDataTests.cpp` | 3 | | main | pure exposure root builders |
| `Render/Passes/Exposure/ExposureTrackerTests.cpp` | 2 | | branch | exposure feedback and transitions under the tracker |
| `Render/Passes/LocalLights/LightClusterFrameDataTests.cpp` | 2 | | main | pure light-cluster root builders |
| `Render/Passes/LocalLights/LightClusterRetirementTests.cpp` | 3 | | branch | light-cluster readback retirement keeps errors and never publishes zeros |
| `Render/Passes/LocalLights/LightTrackerTests.cpp` | 4 | | branch | local-light modes and diagnostics under the tracker |
| `Render/Passes/LocalLights/RenderZeroLightInvariantTests.cpp` | 1 | | branch | the device replay above |
| `Render/Passes/Occlusion/GpuOcclusionOffTests.cpp` | | 5 | branch | temporal-off occlusion oracles |
| `Render/Passes/Occlusion/HzbFrameDataTests.cpp` | 2 | | main | pure HZB root builders |
| `Render/Passes/Occlusion/HzbHistoryBindingTests.cpp` | 6 | | branch | HZB history binding and invalidation |
| `Render/Passes/Occlusion/HzbResizeTests.cpp` | 7 | | branch | HZB resize prepares, commits and abandons atomically |
| `Render/Passes/Occlusion/HzbTests.cpp` | 1 | | branch | the renamed R32Float case above |
| `Render/Passes/Occlusion/OcclusionTrackerTests.cpp` | 1 | | branch | occlusion pyramids and the reference under the tracker |
| `Render/Passes/Occlusion/OcclusionTransactionTests.cpp` | 11 | 1 | branch | retirement transactions of production and reference visibility; one native GPU matrix |
| `Render/Passes/Scene/GpuAlphaMaskTests.cpp` | | 1 | branch | alpha-mask coverage with temporal Off |
| `Render/Passes/Scene/GpuDrawSubmissionTests.cpp` | | 1 | branch | paced draw rows and arguments across overlap and growth |
| `Render/Passes/Scene/GpuRendererTests.cpp` | | 1 | branch | scene pass timings after slot retirement |
| `Render/Passes/Scene/SceneFrameDataTests.cpp` | 3 | | main | pure scene, sky and local-light root builders |
| `Render/Passes/Scene/SceneTrackerTests.cpp` | 4 | | branch | scene submission, culling, wireframe and material views under the tracker |
| `Render/Passes/SelectionOutline/SelectionOutlineFrameDataTests.cpp` | 2 | | main | pure selection-outline root builders |
| `Render/Passes/SelectionOutline/SelectionTrackerTests.cpp` | 2 | | branch | selection paths and resize under the tracker |
| `Render/Passes/Shadow/ShadowTrackerTests.cpp` | 2 | | branch | shadow filter and caster toggles under the tracker |
| `Render/Passes/Temporal/RenderVendorTemporalTests.cpp` | 1 | | branch | vendor render extents respect the upscaler's minimum input scale |
| `Render/Passes/Temporal/TemporalFrameDataTests.cpp` | 6 | | main | pure temporal, upscale and vendor-packing root builders |
| `Render/Passes/Temporal/TemporalTrackerTests.cpp` | 2 | | branch | native and vendor history slots under the tracker |
| `Render/Passes/Visibility/GpuVisibilityContributionOffTests.cpp` | | 2 | branch | temporal-off GPU classification contributions |
| `Render/Passes/Visibility/GpuVisibilityOffTests.cpp` | | 2 | branch | temporal-off visibility modes and overlapping lists |
| `Render/Passes/Visibility/VisibilityFrameDataTests.cpp` | 2 | | main | pure visibility root builders |
| `Render/Passes/Visibility/VisibilityRetirementTests.cpp` | 3 | | branch | visibility readback retirement keeps completed batches on failure |
| `Render/Passes/Visibility/VisibilityTrackerTests.cpp` | 1 | | branch | GPU visibility classify and emit under the tracker |
| `Render/Renderer/RendererResizeTests.cpp` | 2 | | branch | a failed resize leaves the renderer and occlusion at the old extent |
| `Render/Renderer/RendererTrackerTests.cpp` | 2 | | branch | the minimal frame and a culled display read under the tracker |

The 24 `[tracker]` cases are the 23 cases of the eleven `*TrackerTests.cpp` files and the
transient alias case; `Tools/check_tracker_inventory.py` requires the renderer and family cases in the
hosted Debug listing.

## Seam cases renamed on the branch

Three cases added on `main` were renamed on the branch, so the diff against v1 shows only their
final names: `gpuError maps InvalidDesc to InvalidArgument and keeps the message` became `gpuError
keeps v2 error messages`, and `gpuError maps creation failures to Gpu and DeviceUnsupported to
Unsupported` became `gpuError maps driver failures to Gpu and Unsupported to Unsupported`, because
RojoRHI v2 returns no `InvalidDesc`; `toTextureMips points each level at its payload offset with its
row pitch` became `toTextureData points each level at its payload offset with its row pitch`, for
the byte-span upload adapter, with the same assertions plus byte-span sizes.

## Skipped cases

| Null-fixture case | File |
|---|---|
| `a later reference error preserves production reference and complete recovery history` | `Render/Passes/Occlusion/OcclusionTransactionTests.cpp` |
| `prepared owner tokens bind actual owner context mutation epoch move and consumption` | `Render/Passes/Occlusion/OcclusionTransactionTests.cpp` |
| `a real reduction prefix without publication cannot become HZB history` | `Render/Passes/Occlusion/HzbHistoryBindingTests.cpp` |
| `an abandoned HZB declaration cannot supply next-frame history` | `Render/Passes/Occlusion/HzbHistoryBindingTests.cpp` |
| `visibility retirement distinguishes unrecorded intent from recorded empty work` | `Render/Passes/Visibility/VisibilityRetirementTests.cpp` |
| `visibility retirement preserves the entire completed batch on causal failure` | `Render/Passes/Visibility/VisibilityRetirementTests.cpp` |
| `visibility retirement validates undecoded tickets and preserves source generations` | `Render/Passes/Visibility/VisibilityRetirementTests.cpp` |

Every skipped case is new on v2 and runs in at least one other configuration:

- **The 24 `[tracker]` cases** skip in Release on both backends, because RojoRHI's validation
  tracker exists only in Debug builds. They run in Debug on Metal 4 and Null, 24 of 24 each, and
  in the hosted Debug Null job.
- **The 7 Null-fixture cases above** skip on Metal 4 in Release and Debug. They inject staging
  errors into retirement bookkeeping, or fork a child to observe a misuse diagnostic, which must not
  run Metal work in an inherited process; their subject is backend-independent bookkeeping. They
  run on Null in Release and Debug, and in hosted Release CI.

Release Metal 4 therefore skips 31 cases, Release Null 24 and Debug Metal 4 seven; Debug Null and
both GPU runs skip none. The minimal-frame tracker case skips in two sections, so the console shows
32 skipped blocks for 31 cases.

## Goldens

Since `r5-v1-reference` one golden line changed and nothing else under `Tests/Golden/` or any hash
constant under `Tests/`. In `frame-temporal-vendor.txt` the display sink `present r2 v2` became
`export r2 v2`: the vendor dump case executes its frame on v2, and an executed frame exports the
display target, as `Renderer::render` does, where v1's compile-only frame presented it. The five
native `frame-temporal*.txt` goldens and the two `display-sdr-*.png` goldens are unchanged, and the
vendor images pass the parity gate against frozen v1 within `vendor-quantization-v1`, which is the
frozen-v1 comparison the reset rule asks for.

## Retired bookkeeping

Earlier revisions of this file logged each family's return and a per-root "actual consumption"
ledger. The parity gate does not use that ledger: the image gate over the 15 cases and the
regression sweep, the GPU oracles and the tracker cases are the evidence that every root is
consumed, and the capture decoder names every registered root on a real capture. The history
stays in the family tags, such as `r5-lights-validated-2026-11-04`.
