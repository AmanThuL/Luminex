# UX4 native follow-up Gallery, light

**Status**: Implemented — owner accepted for integration on 2026-10-01; failed and incomplete gates retained as measured

2026-10-01 independent pixel review of all 21 component families and every labeled native state
in L100 and L150. Return to [follow-up scope](ux4-native-followup-validation.md).
Evidence prefix `E = ../Luminex-evidence/ux4/native-followup-2026-10-01/`. Review source: `E/native-gallery-light-review.md`
and `.json`; the JSON retains per-state evidence, input/export hashes and capture dimensions.
Exports: `../Luminex-Identity/ux4-design/exports/`. All deviations below were recorded by that review.
Every family is OBSERVED_WITH_DEVIATIONS; neither visual parity nor interaction correctness passes.

## Capture limits and shared deviations

- The gallery stacks labeled states vertically; exports arrange compact fixed-size samples. Full-width controls, additional state captions and greater spacing are global differences.
- Square controls differ from rounded exports, but the binding milestone already retains radius/control 0 after its cost gate. This review records appearance without reopening or accepting that decision.
- The Light panel/background, dark text, blue operator accents, violet agent marks and status colors are visible. JPEG evidence does not establish exact token values or quantitative contrast.
- Scroll-edge cropping is a capture limitation, not automatically an internal layout defect. Pointer and its halo are capture context; obscured pixels are not reconstructed.
- No internal label collision, digit overlap or text clipping was identified in fully visible content. This is scoped to the captured 780×720-point Comfortable context.
- The Light L100 disabled-menu reason tooltip and Proposed-value tooltip are actually displayed. Screenshot inspection establishes visible wording, not complete interaction correctness.
- Clean targeted captures resolve every identified content/overline occlusion. Pointer halos still cover incidental blank areas in some frames; this report does not assert exact complete-frame pixel equality.

## Button

Expected native states: Neutral Default, Neutral Hover, Neutral Active, Neutral Disabled, Primary Default, Primary Hover, Primary Active, Primary Disabled.
Export: `ux4-button.png`.
L100: `E/gallery-L100-a-00.jpg`, `E/gallery-L100-a-01.jpg`.
L150: `E/gallery-L150-a-01.jpg`, `E/gallery-L150-a-02.jpg`.

- Controls are square rather than slightly rounded; radius 0 is already retained by the written cost decision.
- Save scene is heavier and its spacing differs from the export. Disabled neutral and primary fills are paler than the exported disabled samples.

## Icon button

Expected native states: Default, Hover, Active, Toggled, Disabled.
Export: `ux4-icon-button.png`.
L100: `E/gallery-L100-a-02.jpg`.
L150: `E/gallery-L150-a-03.jpg`.

- The native play outline is larger/heavier within the button; Active lacks the export's strong outlined edge, while Toggled retains a blue outline.
- Square native corners differ from the slightly rounded export.

## Checkbox

Expected native states: Off, On, Hover, Disabled.
Export: `ux4-checkbox.png`.
L100: `E/gallery-L100-a-02.jpg`, `E/gallery-L100-a-03.jpg`.
L150: `E/gallery-L150-a-04.jpg`.

- Native boxes are approximately control height; the export uses a smaller box aligned with text.
- On has a large blue check on a pale blue square; export shows a smaller check/blue boundary. Disabled fill is much paler in the native Light sample.

## Chip

Expected native states: Severity, Actor, Provenance, Count.
Export: `ux4-chip.png`.
L100: `E/gallery-L100-a-03.jpg`.
L150: `E/gallery-L150-a-05.jpg`, `E/gallery-L150-b-00.jpg`.

- WARN and 12 both use amber, while the export count is gray.
- The agent diamond follows the label instead of preceding it. The session dash is an underline rather than a leading mark.
- Pills have more padding and wider digit spacing; Count has a stronger outline.

## Field/Text

Expected native states: Default, Focus, Hint, Disabled.
Export: `ux4-field-text.png`.
L100: `E/gallery-L100-b-01.jpg`.
L150: `E/gallery-L150-b-01.jpg`, `E/gallery-L150-b-02.jpg`.

- Default, Focus and Disabled omit the export's trailing clear x; Focus has no visible insertion caret.
- Fields stretch to gallery width and have square corners; disabled background is much paler than the export.

## Field/Number

Expected native states: Default, Active, Proposed, SystemApplied, Disabled.
Export: `ux4-field-number.png`.
L100: `E/gallery-L100-b-01.jpg`, `E/gallery-L100-b-02.jpg`.
L150: `E/gallery-L150-b-02.jpg`, `E/gallery-L150-b-03.jpg`.

- Default, Active and Disabled center X and the value rather than left-aligning them; native X is full-size.
- Proposed omits X and uses an outlined diamond instead of the filled export diamond.
- SystemApplied adds the full policy sentence and a gear instead of the export's compact ring. The written specification calls for a gear naming the policy, so this is an export deviation with a specification rationale.

## Field/Select

Expected native states: Closed, Open, Disabled.
Export: `ux4-field-select.png`.
L100: `E/gallery-L100-b-02.jpg`, `E/gallery-L100-b-03.jpg`.
L150: `E/gallery-L150-b-04.jpg`.

- A filled triangle in a separate shaded end region replaces the export chevron.
- Open choices are individual widely spaced bordered rows instead of one compact popup; MetalFX and its reason share one text string instead of separate columns.
- Native Disabled is paler than the exported fill.

## Field/Slider

Expected native states: Default, Active, Disabled.
Export: `ux4-field-slider.png`.
L100: `E/gallery-L100-b-03.jpg`, `E/gallery-L100-b-04.jpg`.
L150: `E/gallery-L150-b-05.jpg`, `E/gallery-L150-c-00.jpg`.

- Native shows a short rectangular thumb without the export's left-of-thumb filled segment.
- The 0.375 value is centered independently of the thumb; export places it just after the filled segment.

## Dock tab

Expected native states: Selected, Unselected, DimmedSelected.
Export: `ux4-dock-tab.png`.
L100: `E/gallery-L100-b-03.jpg`, `E/gallery-L100-b-04.jpg`.
L150: `E/gallery-L150-c-01.jpg`, `E/0022.jpg`.

- Selected and DimmedSelected omit the export close x.
- Each specimen becomes a full-width bordered row; selected blue and dimmed gray top bars remain visible.

## Menu item

Expected native states: Default, Hover, Checked, Disabled.
Export: `ux4-menu-item.png`.
L100: `E/gallery-L100-b-04.jpg`, `E/gallery-L100-b-05.jpg`.
L150: `E/gallery-L150-c-02.jpg`.

- Checked visibly reads '? Auto (system)' in place of the export's blue checkmark; pixels establish the wrong glyph, not its cause.
- F follows Frame Selected rather than aligning in a trailing shortcut column. Rows have full-width borders instead of the export's compact unboxed menu treatment.
- L100 b-05/c-00 visibly show the disabled reason tooltip: 'No subject with reliable bounds is selected.' This establishes the displayed tooltip only.

## Hierarchy row

Expected native states: Authored, Selected, Edited, Session, Off, Culled, Proposed, AgentFocus.
Export: `ux4-hierarchy-row.png`.
L100: `E/gallery-L100-b-05.jpg`, `E/gallery-L100-c-01.jpg`.
L150: `E/gallery-L150-c-03.jpg`, `E/gallery-L150-c-04.jpg`.

- Native Edited and Proposed marks sit at the far right rather than before the subject name; Proposed diamond is outlined rather than filled.
- Session underlines the entire name plus 'not saved'; export underlines only the name and colors the suffix amber.
- Native uses full-width bordered rows and a text greater-than disclosure character; export uses compact rows with a small chevron.

## Property row

Expected native states: Default, ReadOnly, Proposed, SystemApplied.
Export: `ux4-property-row.png`.
L100: `E/gallery-L100-c-01.jpg`, `E/gallery-L100-c-02.jpg`, `E/0024.jpg`.
L150: `E/gallery-L150-c-05.jpg`, `E/gallery-L150-d-00.jpg`, `E/gallery-L150-d-01.jpg`.

- Default value is centered in a wider field instead of left-aligned; label/value columns are farther apart.
- Proposed directly shows 12.000 and 9.500 rather than the export 12.0 and 9.5; the diamond is outlined. Clean L100 0024 and L150 c-05 establish these digits independently.
- ReadOnly stays on one line at this width rather than the export's two. SystemApplied places 'auto' next to the number and gear at far right instead of a trailing auto/ring pair.

## Subject header

Expected native states: Default, Changed.
Export: `ux4-subject-header.png`.
L100: `E/gallery-L100-c-02.jpg`, `E/gallery-L100-c-03.jpg`.
L150: `E/gallery-L150-d-01.jpg`.

- The checkbox is larger than the export. Reset follows Spot/changed dot immediately rather than aligning at the trailing edge of a fixed-width header.
- Native Changed expands the inline group by inserting the dot; Default reset is visibly disabled.

## Topic header

Expected native states: Collapsed, Expanded.
Export: `ux4-topic-header.png`.
L100: `E/gallery-L100-c-03.jpg`.
L150: `E/gallery-L150-d-02.jpg`.

- Native uses large filled disclosure triangles instead of chevrons.
- Reset occupies a separate bordered end cell; rows stretch across the gallery.

## Notice

Expected native states: Success, Failure.
Export: `ux4-notice.png`.
L100: `E/gallery-L100-c-03.jpg`, `E/gallery-L100-c-04.jpg`.
L150: `E/gallery-L150-d-02.jpg`, `E/gallery-L150-d-03.jpg`, `E/0023.jpg`.

- Actions sit outside and below the notice container and use blue primary fills; exports contain small neutral buttons within the notice.
- Neither native notice has a close x. Success path/time is body Sans instead of small Mono.
- Failure omits the specific arch/arch_01 values and unchanged-file sentence shown in the export.

## Legend chip

Expected native states: Motion, HZB.
Export: `ux4-legend-chip.png`.
L100: `E/gallery-L100-c-04.jpg`.
L150: `E/gallery-L150-d-03.jpg`, `E/gallery-L150-d-04.jpg`.

- The rounded container encloses only the title; swatches and ranges are outside it.
- Titles omit dropdown chevrons and place x next to text; HZB minus/count/plus are text without separate button boxes.
- Ranges cluster at the left rather than aligning across the swatches. Native samples span the gallery width.

## Graph card

Expected native states: Raster, Compute, Culled.
Export: `ux4-graph-card.png`.
L100: `E/gallery-L100-c-05.jpg`.
L150: `E/gallery-L150-d-04.jpg`, `E/gallery-L150-d-05.jpg`, `E/gallery-L150-e-00.jpg`.

- Title and cost are one Mono string instead of a Sans title plus separately right-aligned Mono cost.
- Pin labels use Mono instead of the export's smaller Sans. Cards are much wider/taller.
- Culled has dashes below its bottom edge rather than around the perimeter.

## Console row

Expected native states: Trace, Debug, Info, Warning, Error, Critical.
Export: `ux4-console-row.png`.
L100: `E/gallery-L100-d-01.jpg`, `E/gallery-L100-d-02.jpg`.
L150: `E/gallery-L150-e-01.jpg`, `E/gallery-L150-e-02.jpg`, `E/gallery-L150-e-03.jpg`.

- Timestamp, severity pill and message are on three separate lines rather than one compact row.
- Pills use title case and more padding; Warning/Error show generic diagnostic messages instead of the export's specific fallback/hash messages.
- Info names Geist Regular 16 pt instead of the obsolete Inter sentence; this follows the accepted typeface change. Trace, Debug and Critical add states beyond the export's three.

## Activity strip

Expected native states: idle, working, awaiting, proposed, applied, error, stale, Measuring, Loading, Capture pending.
Export: `ux4-activity-strip.png`.
L100: `E/gallery-L100-d-02.jpg`, `E/gallery-L100-d-03.jpg`, `E/gallery-L100-d-04.jpg`.
L150: `E/gallery-L150-e-04.jpg`, `E/gallery-L150-e-05.jpg`, `E/gallery-L150-f-01.jpg`.

- Native specimens are unboxed labels with progress/underlines beneath, rather than compact boxed strips.
- Measuring uses an operator dot and text Stop button and omits 120 / 600 frames; export uses a system ring and square Stop icon.
- Capture pending omits waiting-for-drawable text and Stop. Agent fixtures omit the named edit task, 3 / 5 count and Stop; seven lifecycle fixtures and Loading extend the export.

## Attention ring

Expected native states: Row, Field.
Export: `ux4-attention-ring.png`.
L100: `E/gallery-L100-d-04.jpg`, `E/gallery-L100-d-05.jpg`.
L150: `E/gallery-L150-f-01.jpg`, `E/gallery-L150-f-02.jpg`.

- Row shows Local Light 9 · attention with no chevron. Field is a single line 'Intensity 12.000' rather than a relative-intensity label plus separate numeric input.
- Both omit the agent diamond and 'Agent is reading this · not selected' caption. Outlines are square and full-width.

## Proposal card

Expected native states: Pending, Applied.
Export: `ux4-proposal-card.png`.
L100: `E/gallery-L100-d-05.jpg`, `E/gallery-L100-d-04.jpg`.
L150: `E/gallery-L150-f-02.jpg`, `E/gallery-L150-f-03.jpg`.

- Both cards say '2 changes' but each displays only one numeric diff; export has two named changes, Intensity and Range.
- Native title is Adjust local-light intensity with a separate actor/count row, replacing the export's Agent proposal title, task subtitle and status/time pill.
- Diff uses a full-width bordered field with 12.000/9.500 and outlined diamond; export uses two compact numeric lines.
- Evidence is a large generic underlined fixture link instead of a small Mono filename plus frame/list details.
- Pending actions are left-aligned Show, Accept, Reject with violet borders; export orders Show, Reject, Accept at right. Applied Revert is on a separate line.
- Both add a gallery-fixture disclaimer and use much larger cards.

