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
