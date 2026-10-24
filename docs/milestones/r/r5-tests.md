# Renderer test coverage on the integration branch

**Status**: In progress

The pending-family list in `xmake/port.lua` temporarily leaves concrete stages and their tests out
of the build. Every case keeps its name, explicit tags and assertions and returns with its family.
The inventory below was measured before exclusion at `9a82612` with Catch2 file tags; ordinary GPU
counts use `[gpu]~[.]`. Hidden diagnostics are tracked separately.

## Left out on the integration branch

### SCENE

| Test file | CPU | GPU | Hidden |
|---|---:|---:|---:|
| `Tests/Engine/Scene/GpuSceneTableAbiTests.cpp` | 0 | 3 | 0 |
| `Tests/Engine/Scene/GpuSceneTableTests.cpp` | 0 | 9 | 0 |
| `Tests/Render/Passes/Scene/GpuAlphaMaskTests.cpp` | 0 | 3 | 0 |
| `Tests/Render/Passes/Scene/GpuRendererTests.cpp` | 0 | 8 | 0 |
| `Tests/Render/Passes/Scene/GpuSceneShadingTests.cpp` | 0 | 3 | 0 |
| `Tests/Render/Passes/Shadow/GpuRendererShadowTests.cpp` | 0 | 4 | 0 |
| `Tests/Render/Passes/Shadow/RenderShadowFitTests.cpp` | 4 | 0 | 0 |
| `Tests/Render/Passes/Visibility/VisibilityTablesTests.cpp` | 4 | 0 | 0 |
| `Tests/Render/Passes/Visibility/VisibilityTests.cpp` | 3 | 0 | 0 |
| `Tests/Scenes/EngineSceneTests.cpp` | 2 | 12 | 0 |
| `Tests/Scenes/SceneMaterialLabTests.cpp` | 1 | 9 | 0 |

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

The measured phase-A baseline is **1256 CPU / 229 ordinary GPU** cases. Two retained detached-window
CLI cases make the full-source equivalent **1258 CPU / 229 ordinary GPU**. Excluded: **26 CPU / 177 GPU**.
Kept: **1232 CPU / 52 GPU**. Thus 1232+26=1258 CPU and 52+177=229 GPU.
The excluded files also contain 1 hidden diagnostic case; it is outside the ordinary totals.

All nine pure FrameData families/tests remain compiled, as do the three checkpoint cases and
the direct graph dump goldens. HzbTests needs hzbLayout from excluded HzbStage; shadow-fit tests
need excluded ShadowStage. VisibilityTablesTests needs buildDrawSubmission from excluded SCENE.
The mixed inspector file needs Native TAA history alternation and waits for TEMPORAL. The shared
frame-declaration file requires bloom transients and waits for POST. Mixed scene pixel files wait
for SCENE. Their complete registered file inventories are counted above; assertions are unchanged.

The two added retained cases in `Tests/App/Model/Options/AppOptionsTests.cpp` are
"--open-window opens a detached tool window" and
"--open-window rejects unknown names and headless runs". The measured excluded-file inventories
above remain unchanged.

The original seven-case CaptureSchemaTests file was split without changing a case block: four
generic schema API cases remain kept, and three full-renderer registration cases move to
RendererCaptureLayoutTests under VISIBILITY. That family returns after SCENE and LIGHTS, when
all required layouts are available. The three moved cases were selected by exact names in the
original full binary; their new file tag did not exist in that inventory.

MeasurementSampleTests requires real declaration candidates and GPU-classifier counters before
retirement, so its unchanged case waits for VISIBILITY after SCENE returns.

## GPU owner and error conversion

The error cases `gpuError maps InvalidDesc to InvalidArgument and keeps the message` and
`gpuError maps creation failures to Gpu and DeviceUnsupported to Unsupported` become
`gpuError keeps v2 error messages` and
`gpuError maps driver failures to Gpu and Unsupported to Unsupported`. RojoRHI no longer returns
InvalidDesc: invalid interface descriptors are fatal misuse. All four real categories preserve
messages; Unsupported maps to Unsupported and the other three map to Gpu. No fictitious descriptor
error is kept to satisfy the old assertion.

The four existing EngineGpuOwnerTests cases retain their names and run as `[engine][device]`
against the configured backend because handles replace subclass stubs. Descriptor/label and move
identity assertions remain; readback buffer memory replaces obsolete descriptor booleans.
Deferred release is checked after actual frame-slot recycling rather than immediate destruction.
Additional owner cases cover reset/move exactly-once retirement and immutable color/depth creation
layout, underlying mip/layer metadata and storage-view presence. Existing Engine capture facade
assertions keep field names, offsets and size; schema version 2 replaces the removed numeric slot
assertion with explicit no-slot/no-page-channel assertions. The generic capture suite moves and
converts separately with its resource/root-window coverage.

## Scene upload conversion

The case `toTextureMips points each level at its payload offset with its row pitch` becomes
`toTextureData points each level at its payload offset with its row pitch`, matching the byte-span
upload adapter. Payload identity, level order and row pitches remain asserted; exact byte-span
sizes and zero bytesPerImage for these 2D subresources are also checked. This is a rename with no
case-count change.

## Preserving readback coverage

Nine additive readback cases cover padded texture rows, submitted-frame resolution, mapped Upload
regions, mapped Readback destinations, empty versus unexecuted operations, causal failures,
initialized depth, mixed-mip preservation and initialized-color sampling. Six require actual GPU
copy/fill or shader execution and carry `[render][device][gpu]`; three mapped-Upload/empty/error
cases carry `[render][device]`. Null validates commands but does not execute GPU byte payloads.
One additional `[render][device]` case checks actual replacement identity and fresh physical state
for both different and unchanged renderer extents; unchanged extent still replaces its targets.
The existing CPU cases retain their names and remain for conversion with the shared test support.
These additions are separate from the measured prep inventories above; integrated inventories
are measured after the retained test and oracle sources compile together.

### Graph and scene handle conversion

| Existing case | Replacement or classification | Preserved evidence |
|---|---|---|
| one transition point keeps texture and buffer barriers in record order | one boundary groups every required texture and buffer dependency | Logical observer row order remains explicit; independent physical texture and buffer masks share one boundary batch. |
| the pool releases a slot's placements when the slot comes round | the pool keeps a slot's placements while its layout is unchanged | Exact resource/view identities survive four paced visits; a same-byte format change invalidates both placements while retaining the heap. |
| execute encodes each pass kind in its own scope | Same name, adds `[device]` | Real context callbacks execute in order; lowered scopes, logical rows and physical masks are asserted separately. |
| execute derives a buffer barrier from a copy destination | Same name, adds `[device]` | Real callbacks retain resource identity; coalesced physical read/write masks cover both unchanged logical rows. |
| execute emits exactly the transitions the record lists | Same name, adds `[device]` | Emission returns unchanged logical transition rows; independent physical dependencies include first-use depth work. |
| execute runs none of a culled pass | Same name, adds `[device]` | Genuine command recording runs the surviving callback and never runs the culled callback. |

These renames and classifications add no cases. Pure snapshot and attachment descriptor checks remain device-free; external operation checks observe declared inputs/outputs and scope placement without creating a temporal scaler.

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

New case: `cached alias placements survive four tracked frames` (`[render][graph][device][tracker]`) executes real cleared transient placements across four paced frames and compares the returned first/fourth resource identities. It skips in Release because validation is Debug-only. Shader-access and reader-frontier coverage remain the independent physical lowering assertions; no synthetic shader access is recorded.

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

The external case `external passes order inputs and outputs without opening an RHI scope` retains its name, tags and count. Its former fake temporal call count, inputContentWidth32 and reset=true were parameters authored by its mock callback; they are replaced by independent external Run/no-scope placement, exact input/output snapshot identities, logical hazards and physical masks. Wrong-version access still aborts with the complete pass/resource/version diagnostic. Actual upscale translation remains with the temporal family.

The original `RenderReadbackTests.cpp` contains exactly four cases; all four remain. The earlier five-case inventory was inaccurate. Existing GPU tags in mixed scene suites remain unchanged; checked mapped Upload inspection consumes no additional frame and retains all row bytes, identities, capacities and retirement assertions.
