# Editor Usability and Diagnostics Design

**Status**: Proposed

This design translates the accepted [UX1 boundary](../roadmap/editor-experience.md) into one
coherent editor experience. The [audit](../research/2026-09-14-editor-uiux-audit.md) supplies the
observed baseline; the [plan](../plans/2026-09-14-ux1-editor-experience.md) sequences implementation.
These are proposed behaviors and layout targets, not descriptions of the shipped application.

## User tasks and information hierarchy

The workspace supports three connected tasks: inspect the image and its subjects, change a
setting and understand the effective result, then explain a frame's execution and measured cost.
Put common actions beside the image or selected subject. Put verbose technical data behind
explicit Details sections while retaining full names, values and diagnostic meaning.

The default workspace has Scene on the left, Viewport in the center, Inspector on the right and
Performance across the bottom. Render Graph keeps its separate native window. Preserve user
docking and visibility choices; use schema migration only where a changed layout needs it.

| Region | Default content and layout target |
|---|---|
| Scene | Fixed selector/search/count header; scrollable grouped subject list below. Start at 240 pt wide, 220 pt compact |
| Viewport | Scene image with a compact toolbar, diagnostic legend when active, selection cue and bottom playback transport. Remaining width is flexible |
| Inspector | Fixed selected-subject name/type; scrollable grouped fields. Start at 340 pt wide, 320 pt compact |
| Performance | Full-width bottom dock; summary strip followed by cost table and interval plot. Start near 240 pt high, 210 pt compact |
| Render Graph | Fixed live/frozen frame toolbar; canvas plus details pane, with vertical stacking when side-by-side details become unreadable |

These dimensions are starting values measured in logical points, not backing pixels. Tune them
against maximized and 1280 × 720 pt screenshots before acceptance. At user-created narrow widths,
fields stack label above control, summaries wrap and auxiliary sections collapse. Essential
values and actions stay reachable through deliberate scrolling; forcing minimum widths must not
push other docked panels out of the window. The viewport's fit, backing scale and 1:1 status remain
inspectable and must update after toolbar/layout changes.

## Shared visual and control language

Use a shared App-owned style and field helpers across the five panels. Keep the existing ImGui
stack. Use a neutral dark background, one accent for selection/active actions, and distinct
warning/error treatments with accompanying text. Frozen, unavailable and empty states are words
as well as colors. The image should remain the strongest visual element.

Start with a 4/8/12/16 pt spacing scale, at least 13 pt body text, a modest section-heading
increase and comfortable 24 pt control rows. Validate the actual font and glyphs at Retina scale;
do not add a font dependency without its licensing and packaging. Align numeric values and use
tabular/monospaced numerals where available. Reserve strong emphasis for current mode, selected
subject and actionable problems. Read-only telemetry should not resemble editable inputs.

Labels appear left and values/controls right in wide fields, stacked in compact fields. XYZ and
RGB components have explicit labels; units remain adjacent to values. Long subject/pass/resource
names wrap in details or expose their full text through hover/copy. A tooltip supplements visible
essential meaning; it does not rescue a clipped primary value or missing failure reason.

Each editable group has a visible Reset action and a changed-from-default indication. Disable
irrelevant controls with a nearby reason. Toolbars and Inspector edit the same model, avoiding
duplicated settings or conflicting state. Keyboard focus must not send text-entry keys to camera
movement or capture actions. Show the existing fly-camera gesture in contextual viewport help.

## Rendering Inspector and state contract

Rendering opens with the current reconstruction and render/output extent summary, then these
groups: Exposure, Bloom, Shadows, Reconstruction, Resolution, and Display & Details. Primary
controls stay in the first level; history memory, vendor generation, precise display domains and
algorithm conventions live in nested Details. Camera, light and object inspectors use the same
field system. Scene playback moves to the viewport transport.

Derive presentation from explicit model data rather than scattered panel conditionals:

| State | Visible behavior |
|---|---|
| Temporal inputs off | Effective reconstruction Off, history/warmup N/A and effective scale 1.0; dependent scale, dynamic-resolution and diagnostic controls explain why they are disabled |
| Native or Raw active | Show requested/effective mode and extents; vendor capability is secondary information, never an active-mode label |
| Vendor active | Show the actual device algorithm and supported range; unavailable diagnostic choices have explanations |
| Vendor fallback | Keep requested vendor visible beside effective Native TAA and the fallback reason; do not silently rename the request |
| Dynamic resolution off | Show manual effective scale; controller observation is inactive or explicitly labeled as its last observation, with frame provenance |
| Waiting for a retired sample | Show Waiting/N/A, not a fabricated zero; a genuine measured zero remains a valid numeric sample |
| Reset event | Retain the last non-None reason and its matching frame; never pair a current None reason with a historical frame number |

Temporarily disabled settings retain their requested values for re-entry where the current
contract permits it. Read-only effective values describe actual execution. Preserve current
renderer/capture semantics: if a renderer field describes a per-frame reset, add a separate
editor last-event record rather than changing that field's meaning for every consumer.

Every diagnostic value identifies its source frame or sample interval. Retired-render data,
current requested settings and controller observations can legitimately come from different
times; expose that distinction. Changes in scene or mode invalidate incompatible samples and
display a waiting state until a relevant observation arrives.

## Performance

Replace the fixed prose column with compact summary cells: wall-clock frame interval and FPS,
timed pass sum, render/output dimensions, objects/draws, and transient memory in MiB. Details
retain byte-exact memory values and definitions. Label the timed pass sum as such: presentation,
driver and untimed GPU work are outside it. Do not relabel it total GPU frame time or full-frame
cost. Keep a controller budget separate from a wall-clock target.

The cost table is the primary lower region. Show Pass, Average (ms), Latest (ms), Min–Max and
Samples with sortable numeric columns. Default to descending average after samples exist, with
a deterministic tie break and an explicit schedule-order option. Keep selection by pass identity
when sorting. Short labels remove redundant internal prefixes; hover/details retain exact IDs.
In compact layouts keep Pass/Average/Latest visible and put Min–Max/Samples in row details.

The plot shows wall-clock frame interval with labeled millisecond ticks, a labeled history/time
axis and a 16.7 ms reference marked as the 60 Hz target. Expose values above the plot range through
a visible overflow marker or axis expansion; never silently flatten them. State the averaging
window, sample count and refresh cadence using the model's actual values; retain the current
60-frame rolling window and four updates per second unless separately justified.

Use `Freeze metrics`, `Resume metrics` and `Clear history`. Freeze latches the complete displayed
snapshot and its timestamp/frame, including the chart and summaries. Clear while frozen yields
an explicit empty frozen state; Resume waits for new samples. It does not pause rendering or
animation. Inspector telemetry is independently live and labeled with its own provenance.

## Render Graph

The toolbar shows `Live · frame N` or `Frozen · frame N`, Freeze/Resume graph, Fit graph,
Fit selection, 100%, Reset layout, columns and Dump frame. Freeze latches the compiled record
and only the timing data matched to that record. If matching timing is unavailable, show N/A;
never attach a newer frame's timing to an older record. Dump uses the displayed record in both
modes. Freezing this panel does not stop the scene or Performance sampling.

Keep a stable logical identity for canvas nodes, links and selection separately from resource
instances that alternate per frame. Inspect the suspected signature/history interaction first;
do not suppress genuine topology changes to hide the symptom. Map surviving selections through
real topology updates; show a clear notice if an item disappears. Freeze must own its snapshot
data safely after renderer records retire or the scene changes. Resume reconciles with the
current live graph and explains any invalidated selection.

Resource details retain exact physical names, IDs, versions, ranges and access for the displayed
frame. Show fields as rows with wrapping/copy support; selecting a node must not grow neighboring
cards into overlaps. Prefer compact labels on cards and full text in the details pane. Re-layout
only when geometry truly changes or Reset layout is requested; preserve pan, zoom, collapsed
groups and user positions across unchanged topology. Full fit actions are explicit user choices.

## Scene, selection and edit recovery

Keep scene selection, clearable search and result count above the list's scroll area. Groups
collapse independently. Show source names when available with a disambiguating scene-local
identifier; unnamed content has a deterministic fallback. Treat duplicate names correctly and
do not imply that current row/index identity is a durable GPU identity.

A selection remains selected when filtered out, with `Selection hidden by filter` and a clear
filter action in the Inspector. Scene changes resolve or clear selection explicitly. `Frame
selected` is visible near the selected subject and fits the object's world-space bounds with a
margin. Add a viewport bounds/highlight cue that identifies the actual selection and remains
distinct from diagnostic color data. Implement it as editor presentation, excluded from ordinary
scene-only captures. When reliable bounds are unavailable, disable framing with a reason.

Reset transforms and lights to the current scene's authored defaults; reset rendering groups
to their documented editor defaults and camera to the scene's initial camera. Reset actions
state their scope and do not reload the scene or reset unrelated groups. For animated subjects,
restore the authored base and evaluate at current playback time through SceneSession. Preserve
motion/history discontinuity handling. Vector units follow current contracts; RGB radiance is
scene-linear and must not gain an invented photometric unit or an extra sRGB conversion.

## Viewport diagnostics and playback

Show a persistent mode badge for non-Final views and a `Return to Final` action. A compact legend
explains colors, range, units, invalid values and any clipping/scaling from the actual shader
definition for each supported mode. Do not infer color meaning from screenshots. Vendor-specific
availability stays consistent with Inspector. MaterialLab identifies roughness/metallic axes and
the purpose of its fixtures; other labs provide concise purpose/context relevant to the scene.

The transport exposes Play/Pause scene, Step (1/60 s), Reset time and current animation time.
Display paused/playing state explicitly. Step advances once and leaves playback paused; reset
sets time to zero through the existing session semantics. Scenes without animation show that
state without implying the renderer is paused. Camera rail behavior remains attributable to
playback. Camera cut stays a clearly named diagnostic action with visible reset-event feedback.

## Action feedback

Dump and capture share a bounded action-result presentation: unavailable, ready, pending,
succeeded or failed. Availability comes from the actual capability/environment; unsupported
actions explain how to recover. The default capture-disabled message includes the exact launch
requirement because it is necessary for the user to act. Menu and C shortcut share one action
path and report the same state; repeated requests while pending cannot pretend to be successes.

Keep the last result visible until dismissed or replaced, with a short explanation and output
path when applicable. Success provides Reveal in Finder and Copy path. Failed writes retain the
failure reason and permit retry. A transient notification may draw attention, but the result
cannot disappear before it can be read. Logging remains useful without being the UI's sole
feedback. This requires only the last relevant operation result, not a general event-log system.

## Implementation ownership and validation

Pure presentation data, snapshot coherence, selection resolution, reset intent and action state
belong in AppModel where applicable. ImGui layout, native-window behavior and drawing belong in
App/Panels and EditorShell. Scene owns source labels/authored defaults/bounds it can supply;
renderer observers provide truthful record/status data without taking an ImGui dependency.
Keep the established [module contract](../conventions/modules.md) and record lifetimes intact.

The roadmap owns the complete acceptance gate. The plan supplies concrete state-transition,
snapshot and interaction checks. Use tests for behavioral risks, screenshots for readability,
real gestures for navigation and matched image/GPU evidence when rendering-facing code changes.
Do not claim that this design or its documentation branch fixes any of the audited symptoms.
