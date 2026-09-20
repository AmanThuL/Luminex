# Interface Gate B — Foundation Handoff Review

**Status**: Implemented (2026-09-13)

## Disposition

**PASS — entry to M7.1 approved.** The owner requested this review after M6.5 and R1 completion.
The review verifies the current interfaces against ADR 0010 and the temporal ADRs, and adopts
the minimal first-consumer semantics in [ADR 0021](../../decisions/0021-gpu-scene-handoff-contract.md).
No blocking interface defect was found in the reviewed scope. This is the separate review that
[Rendering Foundations](../../roadmap/rendering-foundations.md#m6--temporal-and-display-foundation)
requires; M6/R1 completion alone did not approve it.

M7.1 is eligible for its own implementation plan. Stable GPU identities/tables, GPU visibility
and a new submission path are not implemented by this documentation-only change.

## Public presentation

The owner includes the README refresh in this handoff. The public page leads with shipped rendering,
a real Sponza editor screenshot, four feature summaries, quick-start commands and documentation
links. It retains platform/API/language/license badges and adds the existing CI workflow's dynamic
build badge. Long implementation tables and Mermaid diagrams move out of the homepage's scope;
their technical content remains in the linked architecture and frame guide.

The editor screenshot uses a gallery camera at `(9.5, 2.3, -0.387)`, yaw −90°, pitch 8° and vertical
FOV 55°, with native TAA, automatic exposure and bloom. The Inspector shows Rendering settings.
The Performance panel is paused for capture;
its visible numbers are incidental UI state, not benchmark evidence. A CSS title bar replaces the
native title bar, with a decorative background and shadow; captured scene/UI content is unchanged
apart from uniform display scaling. The original capture and composition HTML remain in
`/tmp/luminex-readme-gallery`; [asset notices](../../../THIRD_PARTY_NOTICES.md) cover the new image.
This presentation work changes no renderer code, golden reference or interface acceptance rule.

## Reviewed contracts and evidence

| Area | Finding | Implementation and regression evidence |
|---|---|---|
| ADR 0010 / root data | Conforms: one allocate/copy/address-bind operation, frame-only addresses, persistent buffer reuse, no parallel uniform API | `CommandList::bindFrameData`, `GpuAddress`, `Metal4CommandList::bindFrameData`; `RHIFrameDataTests`, `GpuFrameDataTests` |
| Arena growth and retirement | Independent growable arenas share the allocator/argument-table retirement proof; slot reuse asserts completed ownership | `Metal4FrameArena`, `Metal4Device::beginFrame/endFrame`; page growth, wide alignment and twelve overlapping frames in `GpuFrameDataTests` |
| Binding semantics | Production stages use frame data and object-shaped buffers/textures; buffers share one slot namespace while textures and samplers have separate namespaces | `SceneStage`, `ShadowStage`, `ExposureStage`, `CommandList`; public binding validation and GPU frame-data coverage |
| Temporal continuity | Rendered-frame chronology is distinct from allocation slots; UV motion, invalid sentinel, reset reasons and exposure domains agree with the accepted ADRs | `Scene::commitFrame`, `SceneSession`, App and Screenshot loops, `Temporal`, `TemporalHistory`, `Renderer`; `RenderTemporalTests` and temporal GPU families |
| Persistent history synchronization | Imports retain actual/conservative terminal uses, including unread vendor slots; render-scale changes preserve output-capacity history | `TemporalResolve::importColor/importDepth/recordFrame`; `GpuTemporalHistoryTests`, `GpuTemporalUpscaleTests`, `RenderVendorTemporalTests` |
| Graph and resource synchronization | Explicit resource uses, buffer ranges and alias transitions remain owned by the graph; indirect argument reads are distinct from shader reads | `RenderGraphTransitions`, `RenderGraphCompile`, `TransientPool`, `CommandList`; graph transition/external/transient cases and unchanged checkpoint A |
| Capabilities | Neutral temporal support and creation failure select an observable native fallback; no new submission capability is implied | `DeviceCapabilities`, `VendorTemporalScaler`; unsupported/failed-creation and mode-switch cases in `RenderVendorTemporalTests` |
| Scene identity | Current `SceneView` is a borrowed draw span, not a persistent identity store; minimal identity/update semantics are now explicit | `Scene`, `SceneView`, ADR 0021; implementation and new identity/remap fixtures remain M7.1 work |

The source audit also confirms no `setUniforms`, `pushRoot` or NoApi implementation remains in
`RHI`, `Source`, `Shaders` or `Tests`. Historical references in benchmark tooling are provenance,
not a supported second API. Static meshes, textures and other persistent data still bind through
owned resources; the review does not reinterpret transient GPU addresses as stable scene IDs.

## Fresh validation

Raw logs are outside the published source tree in `/tmp/luminex-interface-gate-b-evidence`.
The current release App and Tests targets build successfully. `xmake test Tests/unit` passes.
The rebuilt Tests binary, run from its shader directory with `MTL_DEBUG_LAYER=1`, passes the
unchanged `[checkpoint-a]` filter: **19 cases / 1,856 assertions**. The run includes the twelve-frame
arena recycle test and buffer/indirect/synchronization oracles; no Metal validation error occurs.

Commands:

```sh
xmake build App
xmake build Tests
xmake test Tests/unit
(cd build/macosx/arm64/release/test && MTL_DEBUG_LAYER=1 ./Tests '[checkpoint-a]')
xmake policy
```

The first build invocation supplied two positional targets, which xmake rejected before building;
its diagnostic is retained in `build.log`. The two separate valid builds above replace that command.
This is an invocation correction, not a waived build failure. The GPU run uses runtime MSL.

The [R1.5 verification](../r1.5.md#verification) remains the recorded full GPU/temporal, capture and
rendered-output evidence for the unchanged executable source. This gate reruns the CPU suite and
checkpoint A; it does not claim a fresh full GPU suite, image comparison, vendor inspection or
performance measurement. Documentation policy and local link review validate the handoff edits.

## Preserved limits

- [M5.6](../m5/m5.6.md) remains reliability failure / DEFER, with no accepted production speedup or
  adopted ICB path. Frozen experiment source/evidence remains untouched.
- [M6.4](m6.4.md) retains manual Sponza switching and Xcode opaque-encoder inspection follow-ups.
  Existing automated fallback/capability evidence does not complete those checks.
- [M6.5](m6.5.md) retains EDR DEFER and its accepted historical screenshot-hash exception.
  The drift remains unresolved.
- [R1.3](../r1.3.md#unresolved-image-parity) and [R1.5](../r1.5.md#unresolved-image-parity) retain their
  explicitly accepted slice-specific parity exceptions. Failed comparisons stay failed and
  unexplained. Gate B approves interface entry; it does not grant an M7 image-parity exception.

These limits do not expose an unresolved interface prerequisite for the first CPU-driven table
consumer. They carry forward under the roadmap's existing handoff rules. M7.1 must still prove
its table updates, mappings, resource retirement, fallbacks and material/temporal preservation;
later submission adoption needs its own evidence.
