# Renderer test coverage on the integration branch

**Status**: In progress

The pending-family list in `xmake/port.lua` temporarily leaves concrete stages and their tests out of the build. Every case keeps its name, explicit tags and assertions and returns
with its family. The inventory below was measured before exclusion at `9a82612` with Catch2 file tags; ordinary GPU counts use `[gpu]~[.]`. Hidden diagnostics are tracked
separately.

## Left out on the integration branch

### SCENE returned

All original SCENE sources, shaders and independent tests are active. The eight dependent original cases below remain whole in TEMPORAL/POST, with unchanged names, tags, sections
and assertions. SCENE has no remaining manifest exclusion.

### VISIBILITY

| Test file | CPU | GPU | Hidden |
|---|---:|---:|---:|
| `Tests/Render/Passes/Occlusion/GpuHzbGraphTests.cpp` | 0 | 1 | 0 |
| `Tests/Render/Passes/Occlusion/GpuHzbOcclusionTests.cpp` | 0 | 1 | 0 |
| `Tests/Render/Passes/Occlusion/GpuHzbTests.cpp` | 0 | 1 | 0 |
| `Tests/Render/Passes/Occlusion/GpuOcclusionProbeTests.cpp` | 0 | 1 | 0 |
| `Tests/Render/Passes/Occlusion/GpuOcclusionReferenceTests.cpp` | 0 | 4 | 0 |
| `Tests/Render/Passes/Occlusion/GpuOcclusionTests.cpp` | 0 | 5 | 0 |
| `Tests/Render/Passes/Occlusion/HzbTests.cpp` | 2 | 0 | 0 |
| `Tests/Render/Passes/Visibility/GpuDisabledInstanceTests.cpp` | 0 | 2 | 0 |
| `Tests/Render/Passes/Visibility/GpuVisibilityContributionTests.cpp` | 0 | 2 | 0 |
| `Tests/Render/Passes/Visibility/GpuVisibilityGraphTests.cpp` | 0 | 2 | 0 |
| `Tests/Render/Passes/Visibility/GpuVisibilityRailTests.cpp` | 0 | 2 | 0 |
| `Tests/Render/Passes/Visibility/GpuVisibilityTests.cpp` | 0 | 3 | 0 |
| `Tests/Render/Passes/Visibility/GpuVisibilityWorkTests.cpp` | 0 | 6 | 0 |
| `Tests/Render/Renderer/RendererCaptureLayoutTests.cpp` | 3 | 0 | 0 |
| `Tests/App/Model/Performance/MeasurementSampleTests.cpp` | 1 | 0 | 0 |

### LIGHTS

| Test file | CPU | GPU | Hidden |
|---|---:|---:|---:|
| `Tests/Render/Passes/LocalLights/GpuLightClusterTests.cpp` | 0 | 8 | 0 |
| `Tests/Render/Passes/LocalLights/GpuLightDebugViewTests.cpp` | 0 | 5 | 0 |
| `Tests/Render/Passes/LocalLights/GpuLocalLightTests.cpp` | 0 | 1 | 0 |
| `Tests/Render/Passes/LocalLights/GpuRendererLightingTests.cpp` | 0 | 7 | 0 |
| `Tests/Render/Passes/LocalLights/RenderZeroLightInvariantTests.cpp` | 1 | 0 | 0 |

### POST

| Test file | CPU | GPU | Hidden |
|---|---:|---:|---:|
| `Tests/Render/Graph/GpuAppFrameDeclarationTests.cpp` | 0 | 1 | 0 |
| `Tests/Render/Passes/Bloom/GpuBloomTests.cpp` | 0 | 3 | 0 |
| `Tests/Render/Passes/Exposure/GpuExposureBloomTests.cpp` | 0 | 7 | 0 |
| `Tests/Render/Passes/Exposure/GpuRendererDisplayTests.cpp` | 0 | 5 | 0 |
| `Tests/Render/Passes/SelectionOutline/GpuSelectionOutlineTests.cpp` | 0 | 8 | 0 |

### TEMPORAL

| Test file | CPU | GPU | Hidden |
|---|---:|---:|---:|
| `Tests/App/Model/Graph/GpuGraphInspectorTests.cpp` | 0 | 2 | 0 |
| `Tests/Render/Passes/Temporal/GpuTemporalAccumulationTests.cpp` | 0 | 8 | 0 |
| `Tests/Render/Passes/Temporal/GpuTemporalHistoryTests.cpp` | 0 | 6 | 0 |
| `Tests/Render/Passes/Temporal/GpuTemporalMotionTests.cpp` | 0 | 10 | 0 |
| `Tests/Render/Passes/Temporal/GpuTemporalTests.cpp` | 0 | 7 | 0 |
| `Tests/Render/Passes/Temporal/GpuTemporalUpscaleTests.cpp` | 0 | 9 | 0 |
| `Tests/Render/Passes/Temporal/GpuVendorPackTests.cpp` | 0 | 1 | 0 |
| `Tests/Render/Passes/Temporal/GpuVendorTemporalTests.cpp` | 0 | 7 | 0 |
| `Tests/Render/Passes/Temporal/GpuZeroLightHistoryTests.cpp` | 0 | 1 | 1 |
| `Tests/Render/Passes/Temporal/RenderVendorTemporalTests.cpp` | 5 | 0 | 0 |

## Totals

The measured phase-A baseline is **1256 CPU / 229 ordinary GPU** cases. Two retained detached-window CLI cases make the full-source equivalent **1258 CPU / 229 ordinary GPU**.
Excluded: **26 CPU / 177 GPU**. Kept: **1232 CPU / 52 GPU**. Thus 1232+26=1258 CPU and 52+177=229 GPU. The excluded files also contain 1 hidden diagnostic case; it is outside the
ordinary totals.

All nine pure FrameData families/tests remain compiled, as do the three checkpoint cases and the direct graph dump goldens. HzbTests needs hzbLayout from excluded HzbStage; the
four shadow-fit cases are active with ShadowStage. VisibilityTablesTests and VisibilityTests are active with the draw submission builder; their original seven names and tags
remain. The mixed inspector file needs Native TAA history alternation and waits for TEMPORAL. The shared frame-declaration file requires bloom transients and waits for POST. Mixed
scene pixel files wait for SCENE. Their complete registered file inventories are counted above; assertions are unchanged.

The two added retained cases in `Tests/App/Model/Options/AppOptionsTests.cpp` are "--open-window opens a detached tool window" and "--open-window rejects unknown names and headless
runs". The measured excluded-file inventories above remain unchanged.

The original seven-case CaptureSchemaTests file was split without changing a case block: four generic schema API cases remain kept, and three full-renderer registration cases move
to RendererCaptureLayoutTests under VISIBILITY. That family returns after SCENE and LIGHTS, when all required layouts are available. The three moved cases were selected by exact
names in the original full binary; their new file tag did not exist in that inventory.

MeasurementSampleTests requires real declaration candidates and GPU-classifier counters before retirement, so its unchanged case waits for VISIBILITY after SCENE returns.

## GPU owner and error conversion

The error cases `gpuError maps InvalidDesc to InvalidArgument and keeps the message` and `gpuError maps creation failures to Gpu and DeviceUnsupported to Unsupported` become
`gpuError keeps v2 error messages` and `gpuError maps driver failures to Gpu and Unsupported to Unsupported`. RojoRHI no longer returns InvalidDesc: invalid interface descriptors
are fatal misuse. All four real categories preserve messages; Unsupported maps to Unsupported and the other three map to Gpu. No fictitious descriptor error is kept to satisfy the
old assertion.

The four existing EngineGpuOwnerTests cases retain their names and run as `[engine][device]` against the configured backend because handles replace subclass stubs. Descriptor/label
and move identity assertions remain; readback buffer memory replaces obsolete descriptor booleans. Deferred release is checked after actual frame-slot recycling rather than
immediate destruction. Additional owner cases cover reset/move exactly-once retirement and immutable color/depth creation layout, underlying mip/layer metadata and storage-view
presence. Existing Engine capture facade assertions keep field names, offsets and size; schema version 2 replaces the removed numeric slot assertion with explicit
no-slot/no-page-channel assertions. The generic capture suite moves and converts separately with its resource/root-window coverage.

## Scene upload conversion

The case `toTextureMips points each level at its payload offset with its row pitch` becomes `toTextureData points each level at its payload offset with its row pitch`, matching the
byte-span upload adapter. Payload identity, level order and row pitches remain asserted; exact byte-span sizes and zero bytesPerImage for these 2D subresources are also checked.
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
overlapping old-generation bytes, growth, and refusal against actual device limits when a threshold is representable. The original three Scene GPU files remain pending by exact
paths; the new independent copy case is active.

Source-derived discovery delta at this boundary is +7 unit and +1 ordinary GPU: expected configured inventory 1,265 unit / 59 ordinary GPU, including the same three tracker cases.
Binary discovery and runtime results are recorded separately as validation evidence; this inventory makes no claim that the remaining Scene, Shadow, visibility classifier or
temporal coverage has returned.

## Shadow fitting and independent filter coverage

All four original `RenderShadowFitTests.cpp` cases return unchanged apart from including their actual ShadowStage API header. The complete existing case "shadow filters resolve a
quad's shadow on a plane" (`[gpu]`) moves from `GpuRendererShadowTests.cpp` to the active mirrored `GpuShadowSmokeTests.cpp`. Its receiver and occluder geometry, homogeneous
interpolation, PCF/PCSS math, filter values and all original fourteen REQUIRE sites remain; checked v2 creation/readback and optional byte-export assertions are additional. The
three complete renderer-dependent cases remain pending in their original file; no case is duplicated, renamed or retired by this extraction.

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
| Scene tables share geometry and material without sharing motion | Engine/Scene/GpuSceneTableTemporalTests.cpp, TEMPORAL |
| Scene removal preserves row identity and the image of remaining objects | same |
| A reused scene row seeds its own pose and renders zero first-frame motion | same |
| Scene replacement rejects foreign identities while submitted scenes overlap | same |
| Scene table growth keeps stable rows and retires old buffers after three frames | same |
| Scene table slots preserve each submitted transform across three-frame overlap | same |
| alpha mask shares color depth and motion coverage | Render/Passes/Temporal/GpuAlphaMaskTemporalTests.cpp, TEMPORAL |
| pass timings name every pass the graph ran | Render/Passes/Exposure/GpuSceneTimingTests.cpp, POST |

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