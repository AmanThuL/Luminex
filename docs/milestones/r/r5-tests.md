# Renderer test coverage on the integration branch

**Status**: In progress

The pending-family list in `xmake/port.lua` temporarily leaves concrete stages and their tests out of the build. Every case keeps its name, explicit tags and assertions and returns with its family. The inventory below was measured before exclusion at `9a82612` with Catch2 file tags; ordinary GPU counts use `[gpu]~[.]`. Hidden diagnostics are tracked separately.

## Left out on the integration branch

### SCENE returned

All original SCENE sources, shaders and independent tests are active. Of the eight dependent original cases below, seven are active with TEMPORAL and the timing case is active with POST, all with unchanged names, tags, sections and assertions. SCENE has no remaining manifest exclusion.

### VISIBILITY returned

VISIBILITY has no manifest exclusion and no `LMX_PENDING_VISIBILITY` guard.

GPU visibility work generation builds on its address roots. `GpuVisibility.cpp`, `GpuVisibilityReadback.cpp`, `VisibilityCapture.cpp` and the classify, occlusion-classify, scan, emit and sparse-emit entries count with `rojoRHI::atomicAdd` at the dynamic counter word and ignore its old value, and compile with `slang.precise_files`, so the ordered fp32 plane sums stay separately rounded on every target. Both emitters store the six-word 24-byte indexed-indirect argument with `drawId` as the first row entry; the decoder compares all six words and the capacity guard tail starts at `capacity * 6` words. Each classify and emit callback records its states, counters, rows and arguments tickets after the dispatch that writes them, with the exact physical frontier. Retirement stages every completed entry by index before it publishes any: a causal resolve error leaves earlier retired records and every pending entry intact, an entry whose graph never recorded is dropped rather than retired as zeros, and a partly recorded entry is fatal misuse. `VisibilityRetirementTests.cpp` (three `[render][device]` cases, Null only: host-side transaction coverage) exercises it with real error tickets.

HZB and the independent reference follow. `HzbStage.cpp`, `HzbHistoryBinding.cpp`, `OcclusionReference.cpp` and the `HzbReduce`, `HzbPublish`, `HzbDebugView` and `OcclusionReference` entries build on their address roots. Precise selection is v1's prefix predicate: every `Visibility*`, `Hzb*` and `LightCluster*` entry and every `Occlusion*` entry except `OcclusionReference`, and a prefix that names no entry fails the configure. A pyramid becomes history only after its whole reduction chain and publication recorded in the declaring context frame; a frame whose only pyramid use is a suppressed observer leaves no final use behind. Reference and production retirement prepare both owners and join exact opaque declaration associations before either publishes.

The Renderer joins them. `prepareOcclusion` creates the pyramid stage, the production visibility owner ahead of the reference it checks, the reference and the debug pipeline before any pass is declared; `prepareVisibility` retires production and reference together through frame N-3 before draw-buffer growth and the visibility declaration, and returns the error, which stays sticky, from `declarePasses`. A failure after the GPU declaration cancels it with `discardUnrecorded`; visibility status, occlusion source facts and the pyramid stage's slots publish only when the whole frame declares, because the reference, whose readback allocation is the last fallible step, declares before the pyramid build. A failed declaration is not rolled back otherwise: the draw submission's prepared rows, recorded prefix copies and read uses stay advanced, and every application caller treats the failure as terminal. `drainVisibilityAfterIdle` returns the joined retirement's Result; App, screenshot and measurement paths stop on it. `Renderer::resize` prepares the pyramid pair with every other replacement and commits it in the same noexcept step, so a failed resize leaves the pyramids, their state entries and their history at the old extent.

Restored with their names, tags, sections and assertions: `GpuDisabledInstanceTests.cpp` 2, `GpuVisibilityGraphTests.cpp` 2, `GpuVisibilityRailTests.cpp` 2, `GpuHzbGraphTests.cpp` 1 and the temporal `GpuOcclusionTemporalTests.cpp` 5, `GpuVisibilityTemporalTests.cpp` 2 and `GpuVisibilityContributionTemporalTests.cpp` 2 (16 GPU), plus `RendererCaptureLayoutTests.cpp` 2 (CPU). The capture cases check schema version 2: each layout by name, size and field offsets with no slot, the per-draw block as `SceneDrawRoot` (its `firstEntry` at 8) and the visibility roots, and `DrawUniforms` absent. `MeasurementSampleTests.cpp` moves to LIGHTS: its measurement run requires the lighting declaration's frame, which only the pending lighting stage records, as measurement itself refuses to start without lighting diagnostics. Its case is converted to sample real declarations on the configured device (`[device]` added) across all eight Cpu/Gpu × Indirect/Batched × two/zero enabled cells and to execute each sampled frame. Readbacks between frames that must stay adjacent for occlusion are recorded on the frame's own command list, since a separate copy frame would break the source clock. Golden frames present the display target, whose shader-read final use a presenting graph cannot lower, so they compile without executing, as the temporal goldens do; the steady occlusion golden is a second renderer's frame after its executed first frame. The four graph goldens match unchanged once a fresh visibility slot imports without a prior use and a withdrawn history read records no final use, both as in v1.

Nine temporal-off counterparts (`[temporal-off]`) cover the occlusion and visibility predicates at full resolution without claiming jitter, motion, reactive or scaled coverage: `Occlusion/GpuOcclusionOffTests.cpp` 5, `Visibility/GpuVisibilityOffTests.cpp` 2 and `Visibility/GpuVisibilityContributionOffTests.cpp` 2. `RendererResizeTests.cpp` adds an occlusion-pyramid case. Two `[render][device][tracker]` cases, required by `Tools/check_tracker_inventory.py`, run GPU visibility across classifier and layout switches, culling off, an empty view and a resize, and occlusion across first build, history, the debug view, disable/re-enable and resize. The configured inventory is 1,332 unit cases (27 skip in Release Metal), 226 ordinary GPU cases and 20 Debug tracker cases.

### LIGHTS

| Test file | CPU | GPU | Hidden |
|---|---:|---:|---:|
| `Tests/Render/Passes/LocalLights/GpuRendererLightingTests.cpp` | 0 | 7 | 0 |
| `Tests/Render/Passes/LocalLights/RenderZeroLightInvariantTests.cpp` | 1 | 0 | 0 |
| `Tests/Render/Passes/LocalLights/LightCaptureLayoutTests.cpp` | 1 | 0 | 0 |
| `Tests/Render/Passes/Temporal/GpuLightDebugTemporalTests.cpp` | 0 | 1 | 0 |
| `Tests/Render/Passes/Temporal/GpuZeroLightHistoryTests.cpp` | 0 | 1 | 1 |
| `Tests/App/Model/Performance/MeasurementSampleTests.cpp` | 1 | 0 | 0 |

Clustered light lists return ahead of the rest of the family. `LightClusterStage.cpp`, `LightClusterReadback.cpp` and the `LightClusterCount`, `LightClusterScan` and `LightClusterFill` entries build on the shared 48-byte `LightClusterRoot`: each kernel's root carries the addresses it reads or writes and zeros the rest, and the 272-byte parameter block is uploaded once per declaration. The three entries compile with `slang.precise_files`, v1's `LightCluster*` selection. Three paced slots own Gpu-memory shader-write grid (27,648 bytes), index (262,144), count (13,824) and counter (32) buffers. Reset, count, scan and fill keep their labels, order and logical uses, the scan stays a single thread, and in Debug each dispatch declares its true compute-shader ranges. The scan callback records the counters and, when lists are captured, the grid; the fill callback records the indices; each ticket follows the dispatch that writes it, with the compute-write frontier. `LightClusterReadback.h` is the Render-private owner of those outcomes. Retirement checks counters, then grid, then indices: a recorded failure returns unchanged, a required outcome that was never recorded or holds too few bytes is an error rather than zeros, and the index outcome is checked even when nothing was assigned. Each declaration commits on its own, so a failure leaves that declaration and every later one pending and earlier retired records in place. A declaration whose graph never executes, because compilation failed or the frame was abandoned, is discarded with `LightClusterStage::discardUnrecorded`, by the exact `readbackIntent` token its outputs carry and only while no callback has entered it, as GpuVisibility discards its unrecorded intent; this amends the retirement matrix so such a declaration releases its paced slot instead of wedging retirement. Only a wholly unrecorded declaration is discardable: a partly recorded one stays pending and keeps returning its error, and a discarded declaration's callbacks must never run. `takeRetired` is destructive, so the Renderer holds taken records until its frame join commits. The eight original cases in `GpuLightClusterTests.cpp` run with their original names, tags, sections and mirror oracles; their retirement now requires the returned Result. `LightClusterRetirementTests.cpp` adds three `[render][device]` cases. The first runs one section per recorded failure: counters, grid, both (counters reported first), an index list with a defined and with an empty prefix, and a list nobody asked to capture. The second has sections for no declaration, a declaration after the completed frame, absent or incomplete counters, absent lists without capture, required lists with nothing assigned, and empty or short outcomes. The third discards an unexecuted declaration by its exact token and sees its slot recur, and refuses to discard a declaration that recorded an outcome or whose callbacks were entered. With the discard case the configured inventory was 1,330 unit cases (25 skip in Release Metal), 209 ordinary GPU cases and 18 Debug tracker cases.

The light diagnostics and the punctual oracle follow. `LightDebugStage.cpp` and the `LightDebugView` entry build on the 48-byte `LightDebugRoot`: the light-row, grid and index addresses, the sampled depth and display views, and the 188-byte `LightDebugParams` block, uploaded once per pass at sixteen-byte alignment because it begins with a matrix. The fragment math, palettes, sky test and froxel lookup are v1's, the entry loads through the root instead of bindings, and the vertex index comes from `rojoRHI::vertexIndex`. The pipeline is the shared fullscreen pipeline, and the stage registers both layouts and, per pass, both frame roots for capture. In Debug the draw declares fragment-shader reads of the depth view and the grid in every view, the display view for Overflow only, and the light rows and whole index list for Missed only, which are what each view's shader loads. `PunctualLightOracle` reads its 144-byte samples and writes its float4 results through the 16-byte `PunctualLightRoot`, calling the unchanged production `ComputePunctualLight`. Neither entry joins `slang.precise_files`: v1 compiled `LightDebugView` and `PunctualLightOracle` with the default math mode, and the oracle measures the agreement the scene pass's own mode has. Four of the five `GpuLightDebugViewTests.cpp` cases and the `GpuLocalLightTests.cpp` case run with their original names, tags, fixtures and assertions: the missed, clean, count and overflow views compare every pixel with the same predicates, the oracle keeps its 64 samples, 20 exact zeros, four cone-edge bounds and its 1e-5 relative and 1e-6 absolute tolerance. The zero-live case passes while `LMX_PENDING_LIGHTS` keeps the Renderer from declaring any light pass, so it does not yet certify the restored lighting path. The fifth case, "light diagnostics reuse actual temporal depth without contaminating history", needs the Renderer to declare the debug view, so it moved whole, with its seven frames, both reconstructions, 0.75 render scale and final image equality, to `Tests/Render/Passes/Temporal/GpuLightDebugTemporalTests.cpp`, which stays pending until the Renderer join; its readbacks, render and drain already require their Results. The Renderer and Scene integration remain pending, so `LMX_PENDING_LIGHTS` still guards the Renderer's lighting path. The configured inventory is 1,330 unit cases (25 skip in Release Metal), 214 ordinary GPU cases and 18 Debug tracker cases.

### POST returned

The Renderer creates and declares the real exposure seed and metering, bloom and display stages; POST has no manifest exclusion and no `LMX_PENDING_POST` guard. The five `GpuRendererDisplayTests.cpp` cases, the `GpuAppFrameDeclarationTests.cpp` case and the twelve-row timing case below are active with their original names, tags and assertions. Timing reads `GpuContext::passTimings()` after every `endFrame` and drains at most the configured frame slots until the measured frame publishes. Six `[device][render][tracker]` cases cover POST: stage-level exposure reset/manual Off feedback and bloom odd/tiny mip chains, Renderer-driven exposure auto/manual/manual-temporal/reset transitions and bloom toggles with odd, tiny and equal-extent resizes, and selection visible/rejected/disabled consumers and replacement/resize. `Tools/check_tracker_inventory.py` requires them. The temporal flag in the Renderer exposure case selects the manual seed and the exposure import's prior use, and now also declares native reconstruction. The two whole temporal cases moved out of POST are active with TEMPORAL.

### TEMPORAL returned

TEMPORAL has no manifest exclusion and no `LMX_PENDING_TEMPORAL` define.

The temporal stage, `RendererTemporal.cpp` and the Renderer integration build on the address roots; no `LMX_PENDING_TEMPORAL` guard remains and the stand-in depth target is gone. The Renderer owns RG16Float motion and R8Unorm reactive targets matching the scene stage's motion pipeline variants, and imports the stage's two depth/colour slot pairs by declared temporal frame parity. A resize allocates the scene, motion and reactive targets and both slot pairs before it adopts any of them, so a failed allocation leaves the renderer at its old extent with every old identity and state entry, both slot pairs and the vendor scaler; `RendererResizeTests.cpp` (`[render][device]`) fails a resize at each allocation in turn by holding all but 0, 1, 2, ... view slots, and checks that the next vendor frame at the old extent creates no scaler. The occlusion pyramid joins that commit with VISIBILITY. The original vendor packing case (`GpuVendorPackTests.cpp`, `[gpu][temporal][vendor][pack]`) is active.

The vendor path selects MetalFX from `GpuContext::upscalerSupport()` ("MetalFX Temporal") and clamps the render scale into its input range. `vendorRenderExtents` raises each axis to `ceil(minInputScale × output)` where rounding to the nearest pixel falls short, which only a device minimum above 0.5 can cause; at the Metal and Null minimums the extents equal `renderExtentsForScale`'s, which a pure case checks with a 0.6 minimum and both current ones. Declaration asserts the content is not below the minimum, which RojoRHI would raise silently where v1 refused it. The upscaler is created at the output extent and recreated after an output resize; a creation failure falls back to Native TAA until then. The seven `GpuVendorTemporalTests.cpp` cases return with their original names, tags and tolerances on the shared temporal support; each renderer frame records one upscale. The six `RenderVendorTemporalTests.cpp` cases run on a real device on the configured backend (Null advertises MetalFX at 0.25 to 1.0); the four that create a renderer add `[device]`. Without the removed fake device, scaler creations are counted and refused through `temporal_detail::vendorCreationProbe()`, a test-only seam of relaxed atomics in `VendorTemporalScaler`. The v1 command assertions read the frame's single external vendor pass from the dump, the content width from the temporal status and the reset flag from `vendorReset`. The byte-identical fallback case uses that creation failure, since a device listing MetalFX cannot report it unavailable; the policy case still covers `Unsupported`. Their frames execute, so they export the display target: `RenderGraph::lowerPhysical` refuses a texture with both a final use and a presentation sink, and the renderer gives the display target a shader-read final use, which v1's compile-only frame never lowered. `frame-temporal-vendor.txt` keeps v1's full content with that sink changed; the comparison masks only transient offsets, sizes and alignments and the pool byte totals, which differ by backend. Vendor readbacks expect the colour slot as `ExternalWrite`, its final use, where v1 passed an unchecked `ShaderRead`.

Two `[render][device][tracker]` cases cover TEMPORAL in `TemporalTrackerTests.cpp`: native frames alternate both slot pairs across Native TAA, Raw, half-scale upscale, temporal-off and post-resize frames, and vendor frames leave the colour slot as `ExternalWrite` and the depth slot as `ExternalRead` in compute shader states that the following native, Raw and temporal-off frames consume. `Tools/check_tracker_inventory.py` requires both.

Fifty-one native cases return with their original names, tags, sections and assertions: `GpuTemporalTests.cpp` 7, `GpuTemporalMotionTests.cpp` 10, `GpuTemporalHistoryTests.cpp` 6, `GpuTemporalAccumulationTests.cpp` 8, `GpuTemporalUpscaleTests.cpp` 9, `GpuSceneTableTemporalTests.cpp` 6, `GpuAlphaMaskTemporalTests.cpp` 1, `GpuExposureTemporalTests.cpp` 1, `GpuSelectionOutlineTemporalTests.cpp` 1 and `GpuGraphInspectorTests.cpp` 2. Each renderer readback first requires the use the renderer's table records (`requireRecordedUse`), with the v1 argument values except the display target, which every frame leaves as a shader read (`setFinalUse`, in v1 too, whose barrier-only readback did not check it); the raw resolve-diagnostics case pushes the resolve's address root; the inspector timing case drains frame slots until the measured frame publishes, and its executed Native TAA frames export the display target. The five native `frame-temporal*.txt` graph goldens match unchanged: bloom and display declare their mip ranges over all layers, and the resolve, vendor-history and vendor packing passes read the exposure pair as a storage buffer, as v1 declared it. The occlusion and visibility temporal files returned with VISIBILITY and the zero-light history file waits under LIGHTS. At the TEMPORAL return the configured inventory was 1,294 unit cases (18 skip in Release, among them every tracker case), 186 ordinary GPU cases and 18 Debug tracker cases.

### Exposure numerical checkpoint and Bloom source coverage

The six original standalone cases in `Tests/Render/Passes/Exposure/GpuExposureBloomTests.cpp` retain their names and `[gpu]` tags. At checkpoint `7d7979c374ba3ca7c84376ad65d049a116c28334`, the 2026-10-27 M4 Pro run passes all six on Release Metal with `MTL_DEBUG_LAYER=1`: 345 assertions and clean diagnostics. Their source and four-configuration compile evidence use both genuine generated consumers for all Exposure roots and pointed fields on Metal, SPIR-V and HLSL. This numerical checkpoint does not accept production ExposureStage/Renderer feedback or the later combined Bloom build.

The original seventh case, "a manual temporal frame records the EV edit and the value before it", moves whole to `GpuExposureTemporalTests.cpp`. Its EV 0 to 2 to -1 sequence, original sections and assertions run with TEMPORAL. The shared Exposure/Bloom helper uses v2 resources in all three consumer TUs.

Bloom restores all three original cases in `Tests/Render/Passes/Bloom/GpuBloomTests.cpp` and adds "a one-mip bloom output remains sampled threshold data". All four run in the ordinary Metal GPU suite with their `[gpu]` tags. The new case uses the real Bloom and Display owners at tiny extents with nonzero threshold output and zero intensity. Source roots carry exact mip storage views; final output alone is sampled. Layout reflection or returned-root registration does not close Z13.actual submitted consumption under #145/P6.9. Selection restores seven original cases in `Tests/Render/Passes/SelectionOutline/GpuSelectionOutlineTests.cpp` with their original names, tags, sections and numerical assertions. The complete eighth case, "selection presentation leaves ordinary display scene color and temporal history untouched", moves to `Tests/Render/Passes/Temporal/GpuSelectionOutlineTemporalTests.cpp`; its four Native TAA frames, jitter, history equality, age and positive outline checks remain intact. The shared v2 fixture has both actual source consumers. The nine pool constructor transports use the owning `GpuContext` rather than its device handle. The Temporal case runs in the ordinary Metal GPU suite with TEMPORAL. The editor target starts in its actual creation state and ends in the sampled UI state. Same-extent replacement allocates successfully before forgetting the previous state; failure retains the old owner. The seven original Selection cases run in the ordinary Metal GPU suite, and two Selection tracker cases cover selected/rejected/disabled consumers and replacement/resize. Source roots, reflected layout and returned-root registration do not prove submitted pipeline/root/draw consumption: P/B/M remain distinct from C, and Z13.actual under #145/P6.9 remains open. Z03/Z11 POST-dependent obligations, image gates and the fixed smoke/performance gates remain separate.

### Visibility and occlusion layout source coverage

Nine complete Raw cases moved with their original translation-unit helpers to the three Temporal files and returned with VISIBILITY; the scene-lighting/cluster capture case moved to `LightCaptureLayoutTests.cpp` under LIGHTS. These transfers change no baseline case totals. Eight production address roots feed both App and Tests layout probes; three test-oracle roots feed only Tests. The visibility capacity case covers the 23/24/47/48/72-byte boundaries; two physical argument-buffer carriers changed from 2000 to 2400 bytes with both logical 100-argument predicates kept. No layout probe establishes submitted consumption under #145.

## Totals

The measured phase-A baseline is **1256 CPU / 229 ordinary GPU** cases. Two retained detached-window CLI cases make the full-source equivalent **1258 CPU / 229 ordinary GPU**. Excluded: **26 CPU / 177 GPU**. Kept: **1232 CPU / 52 GPU**. Thus 1232+26=1258 CPU and 52+177=229 GPU. The excluded files also contain 1 hidden diagnostic case; it is outside the ordinary totals.

All nine pure FrameData families/tests remain compiled, as do the three checkpoint cases and the direct graph dump goldens. HzbTests is active with HzbStage; the four shadow-fit cases are active with ShadowStage. VisibilityTablesTests and VisibilityTests are active with the draw submission builder; their original seven names and tags remain. The mixed inspector file is active with TEMPORAL. The shared frame-declaration file requires bloom transients and is active with POST. Mixed scene pixel files are active with SCENE. Their complete registered file inventories are counted above; assertions are unchanged.

The two added retained cases in `Tests/App/Model/Options/AppOptionsTests.cpp` are "--open-window opens a detached tool window" and "--open-window rejects unknown names and headless runs". The measured excluded-file inventories above remain unchanged.

The original seven-case CaptureSchemaTests file was split without changing a case block: four generic schema API cases remain kept, two full-renderer registration cases returned in RendererCaptureLayoutTests with VISIBILITY, and the lighting one waits in LightCaptureLayoutTests under LIGHTS.

## GPU owner and error conversion

The error cases `gpuError maps InvalidDesc to InvalidArgument and keeps the message` and `gpuError maps creation failures to Gpu and DeviceUnsupported to Unsupported` become `gpuError keeps v2 error messages` and `gpuError maps driver failures to Gpu and Unsupported to Unsupported`. RojoRHI no longer returns InvalidDesc: invalid interface descriptors are fatal misuse. All four real categories preserve messages; Unsupported maps to Unsupported and the other three map to Gpu. No fictitious descriptor error is kept to satisfy the old assertion.

The four existing EngineGpuOwnerTests cases retain their names and run as `[engine][device]` against the configured backend because handles replace subclass stubs. Descriptor/label and move identity assertions remain; readback buffer memory replaces obsolete descriptor booleans. Deferred release is checked after actual frame-slot recycling rather than immediate destruction. Additional owner cases cover reset/move exactly-once retirement and immutable color/depth creation layout, underlying mip/layer metadata and storage-view presence. Existing Engine capture facade assertions keep field names, offsets and size; schema version 2 replaces the removed numeric slot assertion with explicit no-slot/no-page-channel assertions. The generic capture suite moves and converts separately with its resource/root-window coverage.

## Scene upload conversion

The case `toTextureMips points each level at its payload offset with its row pitch` becomes `toTextureData points each level at its payload offset with its row pitch`, matching the byte-span upload adapter. Payload identity, level order and row pitches remain asserted; exact byte-span sizes and zero bytesPerImage for these 2D subresources are also checked.
This is a rename with no case-count change.

## Preserving readback coverage

Nine additive readback cases cover padded texture rows, submitted-frame resolution, mapped Upload regions, mapped Readback destinations, empty versus unexecuted operations, causal
failures, initialized depth, mixed-mip preservation and initialized-color sampling. Six require actual GPU copy/fill or shader execution and carry `[render][device][gpu]`; three
mapped-Upload/empty/error cases carry `[render][device]`. Null validates commands but does not execute GPU byte payloads. One additional `[render][device]` case checks actual
replacement identity and fresh physical state for both different and unchanged renderer extents; unchanged extent still replaces its targets. The existing CPU cases retain their
names and remain for conversion with the shared test support. These additions are separate from the measured prep inventories above; integrated inventories are measured after the
retained test and oracle sources compile together.

### Graph and scene handle conversion

| Existing case | Replacement or classification | Preserved evidence |
|---|---|---|
| one transition point keeps texture and buffer barriers in record order | one boundary groups every required texture and buffer dependency | Logical observer row order remains explicit; independent physical texture and buffer masks share one boundary batch. |
| the pool releases a slot's placements when the slot comes round | the pool keeps a slot's placements while its layout is unchanged | Exact resource/view identities survive four paced visits; a same-byte format change invalidates both placements while retaining the heap. |
| execute encodes each pass kind in its own scope | Same name, adds `[device]` | Real context callbacks execute in order; lowered scopes, logical rows and physical masks are asserted separately. |
| execute derives a buffer barrier from a copy destination | Same name, adds `[device]` | Real callbacks retain resource identity; coalesced physical read/write masks cover both unchanged logical rows. |
| execute emits exactly the transitions the record lists | Same name, adds `[device]` | Emission returns unchanged logical transition rows; independent physical dependencies include first-use depth work. |
| execute runs none of a culled pass | Same name, adds `[device]` | Genuine command recording runs the surviving callback and never runs the culled callback. |

These renames and classifications add no cases. Pure snapshot and attachment descriptor checks remain device-free; external operation checks observe declared inputs/outputs and
scope placement without creating a temporal scaler.

| Existing case | Replacement or classification | Preserved evidence |
|---|---|---|
| setFinalUse overrides the derived state and emits no barrier | setFinalUse overrides the logical state and emits a physical transition | Logical override and unchanged observer rows remain; exact final color-attachment-to-shader-read dependency is now required. |
| execute emits no barrier for a texture only exported | export adds no barrier after the texture's producing pass | The producer has its required Undefined first-use dependency; export adds no later dependency. |
| CaptureSchema registers and unregisters resources | Same name, moved Render/Renderer → Engine/Gpu, adds `[device]` | Genuine owner registration, move identity and destruction/unregistration preserve resource label/format/lifetime assertions. |
| CaptureSchema records frame-data ranges inside the frame window | CaptureSchema records named root addresses inside the frame window, moved Render/Renderer → Engine/Gpu | Schema2 named Addr root/window/dedup records replace retired slot/page/upload fields; outside-window drops and all address words remain explicit. |
| CaptureSchema escapes JSON strings | Same name, moved Render/Renderer → Engine/Gpu | Escaped text remains byte-identical; typed same-bits resource kinds have independent unregister behavior. |
| CaptureSchema writes context and layouts | Same name, moved Render/Renderer → Engine/Gpu | Context, root layout name and field byte offsets remain; retired binding slot is omitted. |
| makeReadbackTicket stamps the frame and region | Same name, adds `[device]` | Genuine owners retain recorded handle/frame/region metadata without fabricating a completed copy. |
| wholeBuffer spans the descriptor size from offset zero | Same name, adds `[device]` | Genuine buffer descriptor retains offset0/size1024. |
| a ticket resolves the object recorded even after its owner is replaced | Same name, adds `[device]` | Mapped Upload source remains alive through checked resolution; bytes stay0x11 after replacement with0x22. |
| execute writes back the last scheduled use | Same name, adds `[device]` | Genuine emission commits logical and physical state only after pure lowering left the table unchanged. |
| a write wins inside the last pass | Same name, adds `[device]` | Logical write wins while exact mip read/write physical frontiers remain distinct. |
| execute encodes the schedule as labelled render passes | Same name, adds `[device]` | Genuine callbacks run in schedule order; lowered attachment identities/clears/stores and physical dependency masks remain explicit. |

New case: `cached alias placements survive four tracked frames` (`[render][graph][device][tracker]`) executes real cleared transient placements across four paced frames and
compares the returned first/fourth resource identities. It skips in Release because validation is Debug-only. Shader-access and reader-frontier coverage remain the independent
physical lowering assertions; no synthetic shader access is recorded.

Existing scene cases adding only `[device]` for genuine resource creation (all names, other tags and assertions retained):

- `Saved mesh rows select the bound object and retain exported pose edits` (`AppSceneTreeTests.cpp`).
- `explicit saved camera changes generation without ordinary view persistence` (`AppSceneDocumentSaveTests.cpp`).
- `a saved camera yaw beyond a half turn wraps, saves and stays clean` (`AppSceneDocumentSaveTests.cpp`).
- `steep orientations without an exact quaternion save, reload and stay clean` (`AppSceneDocumentSaveTests.cpp`).
- `Save and Save As adopt one snapshot while preserving live identity and own state` (`AppSceneDocumentSaveTests.cpp`).
- `save failures preserve metadata baselines and block save-first destruction` (`AppSceneDocumentSaveTests.cpp`).
- `read-only companion and Save As aliases preserve both existing files` (`AppSceneDocumentSaveTests.cpp`).
- `replacement builds before invalidation and discard cannot return through the cache` (`AppSceneDocumentSaveTests.cpp`).
- `session reload preserves the operator view only when its subject survives` (`AppSceneDocumentSaveTests.cpp`).
- `Save As retires a cached destination without invalidating the live source` (`AppSceneDocumentSaveTests.cpp`).
- `Save preserves immutable imported poses and generated session defaults` (`AppSceneDocumentSaveTests.cpp`).
- `Save As protects the actual padded alternate source buffer` (`AppSceneDocumentSaveTests.cpp`).
- `queued Quit waits for actual Save As adoption or preserves a failed live document` (`AppSceneDocumentSaveTests.cpp`).
- `confirmed Save persists exposure before Open chooser cancellation` (`AppSceneDocumentSaveTests.cpp`).
- `save watcher adopts only the verified editor write of the watched pair` (`AppSceneDocumentSaveTests.cpp`).
- `successful Save and Save As retain a later external pair for review` (`AppSceneDocumentSaveTests.cpp`).
- `save refuses a pair replaced between canonical read and hash` (`AppSceneDocumentSaveTests.cpp`).
- `content Save As rejects geometry and texture aliases before invoking the writer` (`AppSceneDocumentSaveTests.cpp`).
- `content save receipt covers JSON and animation only and adopts a clean document` (`AppSceneDocumentSaveTests.cpp`).
- `schema one native Save and Save As adopt schema two with matching receipts` (`AppSceneDocumentSaveTests.cpp`).
- `Instantiation captures generated own flags before an off ancestor masks them` (`AppSceneEnabledTests.cpp`).
- `SceneSession view borrows item storage while commit and rewind preserve frame ownership` (`AppSceneSessionTests.cpp`).
- `unedited export preserves loaded fields and canonical bytes` (`SceneDocumentExportTests.cpp`).
- `persistent edits then exact restoration become clean despite generation changes` (`SceneDocumentExportTests.cpp`).
- `saved object group light and look edits survive reader and reinstantiation` (`SceneDocumentExportTests.cpp`).
- `generated and animated previews never enter the saved document` (`SceneDocumentExportTests.cpp`).
- `CLI lights remain clean until persisted values change` (`SceneDocumentExportTests.cpp`).
- `export rejects divergent primitive poses rather than losing one edit` (`SceneDocumentExportTests.cpp`).
- `saved camera uses only the explicit rest camera and preserves untouched lens fields` (`SceneDocumentExportTests.cpp`).
- `shared light definitions split only for a changed bound node` (`SceneDocumentExportTests.cpp`).
- `nonfinite or non-unit orientation export fails explicitly` (`SceneDocumentExportTests.cpp`).
- `directions without an exact quaternion save the nearest one and are reported` (`SceneDocumentExportTests.cpp`).
- `a saved camera yaw beyond a half turn wraps and stays exportable` (`SceneDocumentExportTests.cpp`).
- `nonfinite look values fail export instead of aborting the dirty comparison` (`SceneDocumentExportTests.cpp`).
- `noncanonical input is clean and saves the canonical model` (`SceneDocumentExportTests.cpp`).
- `successful save adoption uses the new override before the immutable pose baseline` (`SceneDocumentExportTests.cpp`).
- `mobility follows nearest source override and export preserves authored values` (`SceneDocumentExportTests.cpp`).

The external case `external passes order inputs and outputs without opening an RHI scope` retains its name, tags and count. Its former fake temporal call count, inputContentWidth32
and reset=true were parameters authored by its mock callback; they are replaced by independent external Run/no-scope placement, exact input/output snapshot identities, logical
hazards and physical masks. Wrong-version access still aborts with the complete pass/resource/version diagnostic. Actual upscale translation remains with the temporal family.

The original `RenderReadbackTests.cpp` contains exactly four cases; all four remain. The earlier five-case inventory was inaccurate. Existing GPU tags in mixed scene suites remain
unchanged; checked mapped Upload inspection consumes no additional frame and retains all row bytes, identities, capacities and retirement assertions.

### Retained GPU oracles and scene fixtures

`saved mesh mobility stays aligned with instantiated object order` retains its name and assertions and adds only `[device]`: genuine context-owned resources replace its fake
device. This changes no case count. All existing GPU names/tags and all three graph checkpoint cases remain.

Graph GPU cases retain every byte, gradient, alias, generation and image assertion. Resource resolution checks now assert non-null value identities instead of pointer-result
success. Repeated byte fills preserve their uint32 patterns. The retained-timing case uses its owning context and joins every retired publication after endFrame, including a
complete three-slot tail drain. Sampler mip comparisons keep both actual/reference draws and reuse one physical state table. Display's two PNG goldens and ±1 tolerance are
unchanged; five real root layouts are checked against reflection, while the constant-output MRT oracle remains root-free.

### Measured retained inventory before application-gate additions

The retained-suite inventory is **1,256 unit / 58 ordinary GPU** cases, measured with `~[gpu]` and `[gpu]~[.]`. Against the retained baseline of 1,232/52, this adds 24 unit and six
GPU cases. The eight recorded renames and 53 appended `[device]` classifications change no case count. This measurement precedes the application-gate cases.

Release discovery finds 1,256 unit cases. The full Release unit runs pass 1,255 and skip the cached-alias tracker case because validation is Debug-only. Debug's 61 device cases
pass on Null and Metal; the focused tracker is one of those cases, enables Device validation and checks four real frames. The six new readback GPU cases pass focused Debug Metal
execution with explicit Device validation and `MTL_DEBUG_LAYER=1`. All 58 ordinary GPU cases and all three checkpoint cases pass Metal with that native layer; the two display PNG
goldens retain ±1 tolerance and remain unchanged. These focused selectors are subsets of the measured inventory.

 ## Renderer tracker coverage

Two added cases use a validation-enabled Debug context: `the minimal frame runs four frames under the validation tracker` (empty and MaterialLab sections) and `the display read can
be culled and restored under the tracker`. The latter omits the third frame's display read and draw, checks that Display is culled without changing its persistent state, and
restores the real read on frame four. The shared helper keeps its offscreen output persistent and exports it rather than presenting it.

The configured inventory is **1,258 unit / 58 ordinary GPU** cases, including three `[tracker]` cases. The two Renderer cases add two inventory entries; sections add none. All
three tracker cases skip before device creation in Release, so the Release unit summary reports 1,255 passed and three skipped. Debug runs the three cases with validation enabled.
The existing 53 device classifications, eight renames and pending-family inventory are unchanged.

 ## Paced draw submission coverage

`VisibilityTests.cpp` (three cases) and `VisibilityTablesTests.cpp` (four cases) return with the CPU draw builder. The sparse-row case now checks the 24-byte indirect ABI, drawId
at offset zero, zero firstInstance/baseVertex, and every run's instance count. No case is renamed or retired.

`Tests/Render/Passes/Scene/GpuDrawSubmissionTests.cpp` adds one ordinary GPU case, "draw submission copies paced rows and arguments across overlap and growth"
(`[gpu][scene-submission]`). It records actual destination readbacks on the prepared frames in Direct, Indirect and Batched modes, including empty prefixes, all three paced slots,
overlapping old-generation bytes, growth, and refusal against actual device limits when a threshold is representable. The original three Scene GPU files remained pending at that
boundary and have since returned with SCENE; the new independent copy case is active.

Source-derived discovery delta at this boundary is +7 unit and +1 ordinary GPU: expected configured inventory 1,265 unit / 59 ordinary GPU, including the same three tracker cases.
Binary discovery and runtime results are recorded separately as validation evidence; this inventory makes no claim that the remaining Scene, Shadow, visibility classifier or
temporal coverage has returned.

## Shadow fitting and independent filter coverage

All four original `RenderShadowFitTests.cpp` cases return unchanged apart from including their actual ShadowStage API header. The complete existing case "shadow filters resolve a
quad's shadow on a plane" (`[gpu]`) moves from `GpuRendererShadowTests.cpp` to the active mirrored `GpuShadowSmokeTests.cpp`. Its receiver and occluder geometry, homogeneous
interpolation, PCF/PCSS math, filter values and all original fourteen REQUIRE sites remain; checked v2 creation/readback and optional byte-export assertions are additional. The
three complete renderer-dependent cases stayed in their original file, which has since returned with SCENE; no case is duplicated, renamed or retired by this extraction.

The independent oracle uses reflected ShadowSmokeRoot32 and pointed ShadowSmokeUniforms144, including explicit twelve-byte zero tail padding. Production Shadow roots48/32 and the
pointed 64-byte light transform are reflected separately. The real scalar48-byte engine vertex transport is checked at all twelve field offsets through both App and Test assertion
consumers; the independent oracle retains its original packed12-byte position vertex.

The source-derived delta is +4 unit and +1 active ordinary GPU, with no full-source case-count change. Expected configured inventory is 1,269 unit / 60 ordinary GPU, including the
unchanged three tracker cases. Discovery and native filter results are separate evidence; this extraction does not establish integrated renderer shadows, masks, depth bias or Scene
rendering coverage.

## Scene and shadow address-root coverage

Original SCENE ownership is65 cases:14unit/51ordinaryGPU. Seven pure visibility cases and four shadow-fit cases have returned; the two EngineScene CPU cases and one MaterialLab CPU
case return with their original names/tags. ShadowSmoke's original case moved to GpuShadowSmokeTests.cpp only; it is not an addition. The three active SceneTable cases use
explicitly named submitOffTables: texture factors/stale handles, local-light row pacing, and texture-binding paced retirement. The local-light case inspects actual mapped Engine
data with lighting Off, not light shading.

| Original case, unchanged name/tags | Dependency home |
|---|---|
| Scene tables share geometry and material without sharing motion | Engine/Scene/GpuSceneTableTemporalTests.cpp, TEMPORAL (returned) |
| Scene removal preserves row identity and the image of remaining objects | same |
| A reused scene row seeds its own pose and renders zero first-frame motion | same |
| Scene replacement rejects foreign identities while submitted scenes overlap | same |
| Scene table growth keeps stable rows and retires old buffers after three frames | same |
| Scene table slots preserve each submitted transform across three-frame overlap | same |
| alpha mask shares color depth and motion coverage | Render/Passes/Temporal/GpuAlphaMaskTemporalTests.cpp, TEMPORAL (returned) |
| pass timings name every pass the graph ran | Render/Passes/Exposure/GpuSceneTimingTests.cpp, POST (returned) |

The six Table cases retain their Raw temporal helper; Alpha retains all four sections and every history/motion/rigid/invalid/opaque check; timing retains all12 labels
including9bloom passes. Their balanced original bodies/helper closures are byte-identical. No new skip replaces coverage. Two additive `[gpu]` oracles are `alpha mask shares color
and depth coverage with temporal Off` (with its existing `[alpha-mask]` tag) and `scene pass timings name the active Off frame after slot retirement`. The latter observes the
actual retired frame and shadow/scene/display3labels, finite positive durations, and real three-slot publication, without claiming bloom behavior.

Only `device-private placed buffers reject host upload permission` retires: v2 invalid descriptor misuse replaces its obsolete returned InvalidDesc/cpuWrite assertion. Replacement
coverage is in pinned RojoRHI death cases `VU-rojo-buffer-0003 ShaderWrite is set on an Upload buffer`, `VU-rojo-buffer-0014 the CPU pointer of a Gpu buffer is taken` and
`VU-rojo-heap-0005 a buffer is placed in a heap of another memory class`. The other two ABI/upload cases preserve64-byte surrounding-data checks and all116words/row for two
complete table rows.

`a pass resolving an undeclared texture is refused while the frame runs` retains its name/tag and actual graph-execution path. Its obsolete optional GraphError/refusal.has_value
assertion becomes parent-observed SIGABRT plus a reached-body marker from a freshly spawned Tests process. All three original diagnostic predicates and the real context/two-owner
creation checks remain. No Metal operation runs in a forked inherited process. The Helmet baked/fallback mip case keeps its original name/tag/size/hash/pixel probes, with both
loads using a unique independent physical asset copy and only copied Baked renamed; fetched shared inputs are never mutated.

Six `[device][tracker]` additions are Scene submission Direct/Indirect/Batched (six frames), wireframe, CPU culling, Sponza material/IBL, Shadow PCF/PCSS and directional caster
toggles (four each). Existing three trackers remain. Production draw bodies declare real shader reads; no clear-only graph declaration is treated as shader observation. Nested
frame-arena and opaque adapter reads remain outside public handle-based observation; GPU-private hand-built material flags remain an explicit normal-read limit.

Source-derived final target:1278unit/103ordinaryGPU/9tracker, Release1269pass/9trackerSKIP. Arithmetic is1258+14+6unit,58+51−8deferred−1retired+2Off+1paced-copyGPU. Full-source GPU
delta is+2. Binary discovery and runtime results are separate evidence; these targets alone claim no run.

`ordinary graph buffer writes order in-place reads across paced frames` adds one tracker/unit: four paced in-place frames retain logical Write with physical Read|Write.
The existing copy-to-compute GPU case keeps all assertions and adds final words (+0GPU). Null proves tracking, not arithmetic; texture lowering and all19goldens stay.
Source target:1279unit/103ordinaryGPU/10tracker. Release skips the new tracker before device creation; actual discovery and Metal arithmetic remain separate evidence.
