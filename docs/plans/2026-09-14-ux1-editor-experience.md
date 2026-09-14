# UX1 — Editor Usability and Diagnostics

**Status**: Proposed

This plan decomposes [UX1](../roadmap/editor-experience.md) using the
[milestone specification](../milestones/ux1.md),
[overall design](../specs/2026-09-14-ux1-editor-experience-design.md) and
[baseline audit](../research/2026-09-14-editor-uiux-audit.md). It does not start implementation or
M7.1. The accepted roadmap includes every P1–P3 area; implementation detail remains proposed.

## Start and branch sequence

The documentation baseline contains the roadmap, evidence synthesis, milestone specification,
overall design and this plan. Integrate this documentation outcome independently of source work.
Begin source work by resolving the design against the baseline, marking it Accepted and this
plan In progress, and updating the roadmap's active-plan sentence together. Preserve the rule
that only one implementation plan is active. No new Gate B decision is required.

Use short-lived implementation branches based on each preceding integrated stage:
`codex/editor-diagnostics`, `codex/editor-workflows`, then `codex/editor-visual-consistency`.
Each branch has a coherent outcome; split independently useful source changes into green commits
as needed. All stages execute this same plan; intermediate merges do not complete UX1.

Before editing, rebuild the parent App and preserve its binary/shaders and original workspace
outside the source tree. Repeat the audit's critical reproductions and record display/client/
viewport dimensions, scene/settings, device and validation configuration. Preserve before images
and the graph instability reproduction, with both original and reset default layout. Raw evidence
must survive later rebuilds; restore user workspace state after intrusive tests.

## Stage 1 — Trustworthy and readable diagnostics

1. Establish the shared field layout, spacing/type/state palette and responsive panel rules from
   the design. Apply them to Performance and Rendering Inspector first, so later workflow work
   uses the same visual system. Measure the proposed widths in the actual maximized and compact
   app, and update the design when measured choices differ.
2. Trace requested/effective rendering state and retired measurements through EditorShell,
   TemporalEditorState, DynamicResolution and PerformanceModel. Add explicit unavailable/fresh/
   inactive presentation and a coherent last-reset event without changing a per-frame renderer
   field's existing meaning. Implement dependent-control explanations and Inspector grouping.
3. Rework Performance summaries, sortable timing rows and labeled plot. Preserve snapshot
   coherence through Freeze/Clear/Resume, the actual sample window and timed-pass-sum semantics.
4. Reproduce the graph's temporal instability with captured alternating records. Investigate
   GraphNodeModel signatures and canvas invalidation; separate stable canvas identity from
   physical frame details. Implement frozen record/timing ownership, surviving-selection mapping,
   fit actions and readable details. Verify both unchanged and genuinely changed topology.
5. Give dump/capture one capability-aware action state with pending/result feedback, path and
   reveal/copy actions. Verify menu/shortcut parity and actionable capture-disabled startup.

**Behavior checks:** temporal off/on with retained requests; manual/dynamic resolution on/off and
scene switch; native/vendor/fallback presentation; no sample versus measured zero; reset event
reason/frame persistence; latest versus average and frozen-clear-resume transitions. Use existing
AppModel test families and focused new cases for actual regressions, not widget-by-widget tests.

For Graph, test alternating physical history instances without canvas-state loss, deliberate
topology removal, frozen snapshot lifetime after later records and scene switch, timing frame
matching and dump identity. On the native App, hold selection/navigation for at least ten seconds
under default Native TAA and inspect long resource names. Test capture-enabled success and safe
write failures in addition to disabled startup; do not substitute a log message for UI feedback.

## Stage 2 — Complete daily workflows

1. Keep selector/search/counts fixed above the Scene list; add clear search, groups, source labels,
   duplicate-name disambiguation and explicit filtered selection. Preserve current selection
   semantics and avoid premature GPU identity design.
2. Expose reliable selected-object bounds, Frame selected and a visible viewport cue. Implement
   per-group reset against authored scene/editor defaults through existing action/session paths.
   Preserve transform/motion behavior, distinguish scene changes from filtering, and label vector
   components/units, including scene-linear light radiance.
3. Move animation transport to Viewport with time, explicit step duration and playback state.
   Keep scene playback, graph freeze and metric freeze independent and understandable. Surface
   fly-camera help and verify text focus suppresses navigation/capture shortcuts as appropriate.
4. Derive the seven-view diagnostic legends from shader definitions and capability state; add
   mode badge and Return to Final. Add MaterialLab axes and concise purpose text for lab scenes.

**Behavior checks:** duplicate/unnamed subjects, selection hidden by search, selection after scene
switch, transformed object bounds, framing of tiny/large geometry and unavailable bounds; reset
scope/defaults for static and animated subjects; playback step/reset and independent freeze
states. Keep editor cues out of ordinary scene-only capture and verify color/motion contracts.
Walk Sponza, San Miguel when installed, and the labs with real selection, edit and recovery tasks.

## Stage 3 — Visual consistency and complete application review

1. Apply the established visual rules to Camera, object/light Inspector, Scene, Viewport,
   Performance and detached Graph. Review waiting/loading/empty/failed/disabled/frozen states,
   long labels, numeric precision and recovery actions. Finish P3 across every panel.
2. Reconcile default layout, compact reflow, graph detail stacking and workspace migration.
   Verify clean/default/restored workspace, hidden panels, reset, close/reopen and relaunch.
   Preserve native macOS title bars and the graph's undocked window contract.
3. Run the complete matrix below and retain matched before/after screenshots. Complete the
   baseline audit's unverified camera/docking gestures through an effective real-input method;
   record limitations honestly if one remains unverified. Resolve required coverage before
   claiming the UX1 completion gate.

| Journey | Required evidence |
|---|---|
| Default launch and resize | Maximized and 1280 × 720 pt, restored and reset layout, readable labels/values/actions, pt/px accounting |
| Find and recover a subject | Search, scroll, duplicate names, hidden selection, Frame selected, cue, edit and Reset in large scenes |
| Explain the active image | Raw/Native/MetalFX plus fallback; temporal off; manual/dynamic scale; requested/effective modes/extents and sample freshness |
| Explain measured cost | Sort most expensive pass, latest/average/window semantics, plot units/target, Freeze/Clear/Resume |
| Explain a frame | Native TAA stability, group/selection/pan/zoom, freeze with matched timing, exact displayed-frame dump, topology change and resume |
| Diagnose and animate | Every supported diagnostic legend and unavailable view, Return to Final, MaterialLab axes, TemporalLab play/pause/step/reset |
| Complete an action | Capture-disabled reason, capture-enabled success, dump/capture failure recovery, pending state, output path/reveal/copy, menu/C parity |
| Operate the workspace | Six catalog scenes where assets exist, all menus and panel visibility, detached graph, persistence, keyboard focus, RMB/WASD/QE and docking gestures |

Use presentation fixtures for unavailable-vendor states when hardware cannot exercise them,
and label that evidence separately from real-device fallback. UI automation limitations and
historical image exceptions cannot be recorded as successful acceptance checks.

## Validation and closure

At each source stage run build, formatting, policy, focused behavioral tests and the full CPU
unit suite. `xmake test Tests/unit` rebuilds Tests; direct binary execution requires a prior
Tests build. Use `MTL_DEBUG_LAYER=1 xmake test Tests/gpu` and matched fixed-camera image evidence
for rendering/RHI/shader-facing changes as required by the engineering rules. Choose further
checks by changed behavior, not by mechanically copying a structural-refactoring test matrix.
Do not infer a rendering speedup from UI captures or relax unresolved historical image results.

Review all P1–P3 areas against the roadmap gate. At closure, update the existing
[milestone specification](../milestones/ux1.md) with actual behavior, evidence, limitations and
durable deviations, replacing proposed behavior and pending results with the implementation
record. Update current architecture/guides and AGENTS to match
implemented controls/commands, retain the design as implemented context, and remove this
executor plan from the published baseline under documentation policy. Update the roadmap and
other current-document links to reference the milestone record rather than a removed plan.

Only the complete accepted UX1 outcome advances the roadmap to M7.1 planning. Its GPU scene
implementation remains a separate outcome under the already accepted gate B contract.
