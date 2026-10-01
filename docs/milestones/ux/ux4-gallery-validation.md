# UX4 gallery validation

**Status**: Implemented — owner accepted for integration on 2026-10-01; failed and incomplete gates retained as measured

The locked-Mac native checkpoint below is historical. The [2026-10-01 unlocked follow-up](ux4-native-followup-validation.md#gallery-comparisons-and-body-size)
records D100/D150/L100/L150 as OBSERVED_WITH_DEVIATIONS, all 21 families and labeled states,
with [Dark](ux4-native-followup-gallery-dark.md) and [Light](ux4-native-followup-gallery-light.md)
per-component deviation/screenshot records. The original U table remains that checkpoint's
result. The current captures establish no Figma parity, interaction or 1× pass.

The catalog contains exactly the 21 Figma page 04 component sets. Task 17 adds the missing
Capture pending activity and Applied proposal specimens. At this checkpoint final native captures and all 21 native
component comparisons were **UNVERIFIED**: the Mac was locked and manual unlock was pending.
CPU checks establish names, renderer identities and submitted state text, not displayed pixels.
The [UX4 record](ux4.md#ux44--style-gallery-documentation-and-acceptance) owns the outcome;
the [main validation](ux4-validation.md) retains earlier failures and limits.

## Source and test checkpoint

At entry, the existing exact-name test passed **85 assertions in 1 case** before source edits.
The strengthened existing case passes **106 assertions in 1 case**, checking each golden name's
renderer identity without depending on the sibling Identity checkout or adding a duplicate test.
`GalleryCatalog.cpp` needs no change. Its 21 entries and exhaustive `drawComponent` switch already
exist. The focused compiler checks the panel; no renderer, RHI, CLI or dependency change follows.

An external CPU ImGui probe checks Pending/Applied labels, Show/Accept/Reject/Revert, Capture
pending and existing activity labels in both palettes at font scales 1.0/1.5. The calibrated
pre-edit run fails 16 of 64 checks (missing labels/routes); the completed run passes 64/64.
Pending existed without its label; Applied/Revert and Capture pending were absent.
Two earlier harness runs are invalid evidence: one read a cleared log and crossed a palette
scope over `End`; another expected custom draw-list text in ImGui's widget text log.
They remain archived separately and do not count as product failures.

Evidence root `E = ../Luminex-evidence/ux4/task-17/` retains commands, results, source hashes,
the unchanged 21 PNG exports under `figma/`, and `report.md`. The source metadata lists 21 actual
results but says `setCount: 22`; its additional Appearance/Density documentation specimen is
not a component set. No 22nd catalog entry is authorized. All exports were inspected read-only.
Body 16 and the encoded `#4CABFD` selection outline remain settled.

## Exact component mapping and source observations

Every renderer below belongs to `StyleGalleryPanel.cpp`; names map through `GalleryCatalog`.
Exports are `E/figma/ux4-<name in lowercase, spaces and slashes replaced by hyphens>.png`.
Export axes include Dark/Light for every row. These are export-to-source observations, not
observed deviations in final native screenshots. General layout differs: Figma places fixed-size
variants side by side; the gallery stacks responsive specimens. No visual pass follows from this table.

| Exact Figma name | Renderer counterpart | Source states | Retained export/source difference or context |
|---|---|---|---|
| Button | Button loop, `controlState` | Neutral/Primary × Default/Hover/Active/Disabled | Same labels; native ImGui metrics and primary Medium differ from fixed export geometry. |
| Icon button | `iconButton`, `controlState` | Default/Hover/Active/Toggled/Disabled | Codicons with labeled fallback; export uses a drawn play reference. |
| Field/Text | `InputTextWithHint` | Default/Focus/Hint/Disabled | Export has integrated clear × and a focus cursor; source has no clear control and sets only the Focus border. |
| Field/Number | `DragFloat`, `proposedValue`, `fixture` | Default/Active/Proposed/SystemApplied/Disabled | Proposed source omits X; SystemApplied uses a gear and policy text versus the export's ring. |
| Field/Slider | `SliderFloat` | Default/Active/Disabled | Export fills the track to the grab; source uses the ImGui slider without that filled-track specimen. |
| Field/Select | `BeginCombo`, Open fixtures | Closed/Open/Disabled | Open source uses stacked fixtures beside a live combo; export has one compact popup and a separate disabled reason. |
| Checkbox | `Checkbox` | Off/On/Hover/Disabled | Source uses ImGui's frame-size box; export specifies a 16 px box. |
| Dock tab | `fixture`, overline | Selected/Unselected/DimmedSelected | Export includes close × on selected variants; source has only Inspector and the overline. |
| Hierarchy row | `fixture`, `mark`, `dashedLine` | Authored/Selected/Edited/Session/Off/Culled/Proposed/AgentFocus | Source uses > text, trailing marks and ordinary borders; export uses disclosure ink and inline provenance. |
| Subject header | checkbox, Medium title, `iconButton` | Default/Changed | Same state axes; responsive source can wrap instead of fixed trailing Reset placement. |
| Topic header | `collapsingHeader`, `iconButton` | Collapsed/Expanded | Same state axes; source puts Reset on a following row below its width threshold. |
| Property row | property grid, `proposedValue`, `fixture` | Default/ReadOnly/Proposed/SystemApplied | Proposed source uses 12.000/9.500 versus 12.0/9.5; SystemApplied uses a gear versus ring. |
| Chip | pill `fixture` | Severity/Actor/Provenance/Count | Source combines WARN/count ink and puts the actor mark at the end; export separates count ink and leads with the mark. |
| Activity strip | `activitySpecimens`, `activityStrip` | Seven agent lifecycle labels; Measuring/Loading/Capture pending | Capture now exists. Source Measure actor is Operator; export is System. Only Measure stops in source; exported Capture/Agent show Stop. |
| Proposal card | `proposalSpecimen` | Pending/Applied | Both states now exist. Source retains one numeric diff and generic fixture evidence/title; export has two changes, timestamp chip and named evidence. |
| Notice | `fixture`, status bar, primary actions | Success/Failure | Source actions sit outside the fixture and no close × exists; export groups actions and close inside the notice. |
| Legend chip | title `fixture`, swatches, Mono labels | Motion/HZB | Source uses text for title/close/mip affordances and separate swatches; export encloses them in one chip. |
| Console row | timestamp, severity pill, wrapped text | Trace/Debug/Info/Warning/Error/Critical | Source expands three exported severities to six and stacks parts; export Info still names Inter, source correctly names Geist. |
| Graph card | `graphCard` | Raster/Compute/Culled | Source joins title/cost as Mono text and dashes only the Culled bottom edge; export separates cost and dashes the perimeter. |
| Menu item | `fixture`, reason tooltip | Default/Hover/Checked/Disabled | Source uses a text check and embedded F; export has separate check/shortcut slots. This specimen does not verify NativeMenu. |
| Attention ring | row/field `fixture` | Row/Field reserved focus | Source outlines the numeric fixture directly; export outlines a label/value group and shows an agent caption beneath. |

## Final capture contexts

The controller owns native gestures, OS settings and `screencapture -o -l <window id>`.
No App launch, lock bypass, guessed window ID or capture occurred in this implementation.
Each context requires actual current-revision gallery pixels, recorded shell appearance, gallery
palette, UI scale, density, backing scale, window dimensions, scroll position and screenshot hash.
Font-scale CPU probes do not establish these UI-scale/native contexts. Full coverage needs scroll
captures of every state, not only the top of the window. The type ramp remains part of each context.

| Context | Required appearance / palette / UI scale | Density, backing scale, size and revision | Action / expected | Observed / screenshot path |
|---|---|---|---|---|
| D100 | Dark / Dark / 100%, UNVERIFIED | UNVERIFIED | UNVERIFIED: capture every state and type ramp | UNVERIFIED; none, target `../Luminex-evidence/ux4/gallery/` |
| D150 | Dark / Dark / 150%, UNVERIFIED | UNVERIFIED | UNVERIFIED: capture every state and type ramp | UNVERIFIED; none, target `../Luminex-evidence/ux4/gallery/` |
| L100 | Light / Light / 100%, UNVERIFIED | UNVERIFIED | UNVERIFIED: capture every state and type ramp | UNVERIFIED; none, target `../Luminex-evidence/ux4/gallery/` |
| L150 | Light / Light / 150%, UNVERIFIED | UNVERIFIED | UNVERIFIED: capture every state and type ramp | UNVERIFIED; none, target `../Luminex-evidence/ux4/gallery/` |

## Per-component native checkpoint

`U` means **UNVERIFIED**, with no current pixels and no screenshot path, in every context cell.
Actions and expected checks are pending instructions, not performed gestures or visual results.
For each row the controller must compare the mapped export's full state set, record visible
deviations and attach paths/hashes separately for D100, D150, L100 and L150.

| Exact name | Action | Expected | D100 | D150 | L100 | L150 | Observed | Screenshot path |
|---|---|---|---|---|---|---|---|---|
| Button | U: capture/compare | U: both kinds, four states | U | U | U | U | UNVERIFIED | UNVERIFIED: none |
| Icon button | U: capture/compare | U: five states, icon/fallback | U | U | U | U | UNVERIFIED | UNVERIFIED: none |
| Field/Text | U: capture/compare | U: four states, focus/hint | U | U | U | U | UNVERIFIED | UNVERIFIED: none |
| Field/Number | U: capture/compare | U: five states, proposed/system marks | U | U | U | U | UNVERIFIED | UNVERIFIED: none |
| Field/Slider | U: capture/compare | U: three states, value/grab | U | U | U | U | UNVERIFIED | UNVERIFIED: none |
| Field/Select | U: capture/compare | U: three states, disabled reason | U | U | U | U | UNVERIFIED | UNVERIFIED: none |
| Checkbox | U: capture/compare | U: four states, box/label | U | U | U | U | UNVERIFIED | UNVERIFIED: none |
| Dock tab | U: capture/compare | U: three states, overline | U | U | U | U | UNVERIFIED | UNVERIFIED: none |
| Hierarchy row | U: capture/compare | U: eight states, provenance | U | U | U | U | UNVERIFIED | UNVERIFIED: none |
| Subject header | U: capture/compare | U: two states, scoped Reset | U | U | U | U | UNVERIFIED | UNVERIFIED: none |
| Topic header | U: capture/compare | U: two states, disclosure/Reset | U | U | U | U | UNVERIFIED | UNVERIFIED: none |
| Property row | U: capture/compare | U: four states, grid/reflow | U | U | U | U | UNVERIFIED | UNVERIFIED: none |
| Chip | U: capture/compare | U: four kinds, marks/count | U | U | U | U | UNVERIFIED | UNVERIFIED: none |
| Activity strip | U: capture/compare | U: capture/measure/lifecycle, 2 pt bar | U | U | U | U | UNVERIFIED | UNVERIFIED: none |
| Proposal card | U: capture/compare | U: Pending/Applied, actions/evidence | U | U | U | U | UNVERIFIED | UNVERIFIED: none |
| Notice | U: capture/compare | U: success/failure, status/actions | U | U | U | U | UNVERIFIED | UNVERIFIED: none |
| Legend chip | U: capture/compare | U: Motion/HZB, range/mip | U | U | U | U | UNVERIFIED | UNVERIFIED: none |
| Console row | U: capture/compare | U: six severities, Mono timestamps | U | U | U | U | UNVERIFIED | UNVERIFIED: none |
| Graph card | U: capture/compare | U: three kinds, costs/pins | U | U | U | U | UNVERIFIED | UNVERIFIED: none |
| Menu item | U: capture/compare | U: four states, check/reason | U | U | U | U | UNVERIFIED | UNVERIFIED: none |
| Attention ring | U: capture/compare | U: row/field, 2 pt agent border | U | U | U | U | UNVERIFIED | UNVERIFIED: none |

No Task 12 historical image is used as final evidence or as a visual comparison in this checkpoint.
Screenshots 0133/0134 predate the native menu and later fixtures. A future historical comparison
must record that revision/context and both input hashes, then list only deviations actually visible.
Native menu behavior, display ink, clipping, digit overlap, both themes at 100/150 and all earlier
slice gates remain controller work. This checkpoint neither accepts UX4 nor closes its plan.
