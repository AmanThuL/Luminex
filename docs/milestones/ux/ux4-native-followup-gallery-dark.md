# UX4 native follow-up Gallery, dark

**Status**: In progress

2026-10-01 independent pixel review of all 21 component families and every labeled native state
in D100 and D150. Return to [follow-up scope](ux4-native-followup-validation.md).
Evidence prefix `E = ../Luminex-evidence/ux4/native-followup-2026-10-01/`. Review source: `E/native-gallery-dark-review.md`
and `.json`; the JSON retains per-state evidence, input/export hashes and capture dimensions.
Exports: `../Luminex-Identity/ux4-design/exports/`. All deviations below were recorded by that review.
Every family is OBSERVED_WITH_DEVIATIONS; neither visual parity nor interaction correctness passes.

## Capture limits and shared deviations

- The gallery stacks labeled states vertically on one dark panel, while component exports arrange fixed-width Dark/Light variants side by side; this presentation difference applies to every component.
- Native square controls differ from slightly rounded exported controls. The written UX4 record already retains the radius-0 cost choice; this report records the visible deviation without reopening or accepting that decision.
- Several fixtures replace compact composite components with large full-width rows. Per-component changes are enumerated below.
- JPEG evidence supports visual comparison but not exact token-color equality. No quantitative color match or contrast pass is asserted.
- The static mouse pointer/halo occludes small areas of some images. The pointer is capture context, not attributed to gallery UI.
- Clipping at the top and bottom of the scrolling content area is treated as incomplete capture, not automatically as a layout defect.
- New overlap captures use a larger window and Auto under actual OS Dark. They fill state coverage without claiming same-size pixel parity with the initial forced-Dark context. No new layout collision is visible in complete specimens.

## Button

Expected native states: Neutral Default, Neutral Hover, Neutral Active, Neutral Disabled, Primary Default, Primary Hover, Primary Active, Primary Disabled.
Export: `ux4-button.png`.
D100: `E/gallery-D100-focused-00.jpg`, `E/gallery-D100-focused-01.jpg`.
D150: `E/gallery-D150-overlap-a-00.jpg`, `E/gallery-D150-overlap-a-01.jpg`, `E/gallery-D150-overlap-a-02.jpg`, `E/gallery-D150-overlap-a-03.jpg`.

- Native states are stacked with captions, whereas export states are compact horizontal samples.
- Controls have square corners instead of the export's small rounding (the record separately retains the radius-0 cost decision).
- Save scene is visibly heavier than the export; native button/text proportions are wider-spaced.

## Icon button

Expected native states: Default, Hover, Active, Toggled, Disabled.
Export: `ux4-icon-button.png`.
D100: `E/gallery-D100-focused-01.jpg`, `E/gallery-D100-focused-02.jpg`.
D150: `E/gallery-D150-overlap-a-03.jpg`, `E/gallery-D150-overlap-a-04.jpg`.

- Native play glyph is larger with a heavier outline and uses square control corners.
- Native Active and Toggled fills are darker than the corresponding export samples; the active export is a much lighter blue-gray.

## Field/Text

Expected native states: Default, Focus, Hint, Disabled.
Export: `ux4-field-text.png`.
D100: `E/gallery-D100-focused-03.jpg`, `E/gallery-D100-focused-04.jpg`.
D150: `E/gallery-D150-overlap-a-06.jpg`, `E/gallery-D150-overlap-a-07.jpg`.

- Native fields span almost the content width and have square corners; exports are fixed compact widths with slight rounding.
- Default, Focus and Disabled omit the export's trailing clear ×.
- Focus shows the blue border but no insertion caret in these pixels; actual caret/focus interaction remains unverified.
- Native Disabled has a near-black fill, while the export's disabled fill is visibly lighter gray.

## Field/Number

Expected native states: Default, Active, Proposed, SystemApplied, Disabled.
Export: `ux4-field-number.png`.
D100: `E/gallery-D100-focused-04.jpg`, `E/gallery-D100-focused-05.jpg`, `E/0018.jpg`.
D150: `E/gallery-D150-overlap-a-07.jpg`, `E/gallery-D150-overlap-a-08.jpg`.

- Default/Active/Disabled center X 12.000; the export left-aligns X and value.
- Proposed omits X, uses more widely spaced figures, and places an outlined diamond at the far right instead of the export's filled diamond.
- SystemApplied adds 'set by dynamic resolution' and a gear rather than the export's compact X/value and ring; this gear matches the later written design grammar.
- All native fields are much wider and square-cornered.

## Field/Slider

Expected native states: Default, Active, Disabled.
Export: `ux4-field-slider.png`.
D100: `E/gallery-D100-focused-05.jpg`, `E/gallery-D100-focused-06.jpg`.
D150: `E/gallery-D150-overlap-a-10.jpg`, `E/gallery-D150-overlap-a-11.jpg`.

- The native track has only a blue grab: the export fills the segment before the grab.
- Native value is centered in the full control rather than placed immediately after the grab; wide control spacing is visible.
- Native disabled track stays near black; the export disabled track is lighter blue-gray.

## Field/Select

Expected native states: Closed, Open, Disabled.
Export: `ux4-field-select.png`.
D100: `E/gallery-D100-focused-05.jpg`, `E/gallery-D100-focused-06.jpg`.
D150: `E/gallery-D150-overlap-a-08.jpg`, `E/gallery-D150-overlap-a-09.jpg`, `E/gallery-D150-overlap-a-10.jpg`.

- Native dropdown affordance is a large filled triangle in a separate square button; export uses a small chevron.
- Open is shown as full-width individually bordered, vertically separated rows; the export uses one compact popup containing contiguous rows.
- MetalFX and 'unsupported here' share one muted line instead of separate label/reason columns.
- Native Disabled is near black rather than the lighter export disabled fill.

## Checkbox

Expected native states: Off, On, Hover, Disabled.
Export: `ux4-checkbox.png`.
D100: `E/gallery-D100-focused-02.jpg`, `E/gallery-D100-focused-03.jpg`.
D150: `E/gallery-D150-overlap-a-04.jpg`, `E/gallery-D150-overlap-a-05.jpg`.

- Native checkbox is approximately control height and much larger relative to Enabled than the export's 16 px box.
- Native checked tick is thick blue on dark fill; export uses a small light check with blue box.
- Native squares omit the export's slight corner rounding.

## Dock tab

Expected native states: Selected, Unselected, DimmedSelected.
Export: `ux4-dock-tab.png`.
D100: `E/gallery-D100-focused-06.jpg`, `E/gallery-D100-focused-07.jpg`.
D150: `E/gallery-D150-overlap-a-11.jpg`, `E/gallery-D150-overlap-a-12.jpg`, `E/0025.jpg`.

- Native tabs are full-width outlined strips instead of compact tabs.
- Selected and DimmedSelected omit the export's close ×.
- Native overline extends across the entire gallery width; dimmed overline is gray and selected overline is blue.

## Hierarchy row

Expected native states: Authored, Selected, Edited, Session, Off, Culled, Proposed, AgentFocus.
Export: `ux4-hierarchy-row.png`.
D100: `E/gallery-D100-focused-07.jpg`, `E/gallery-D100-focused-08.jpg`.
D150: `E/gallery-D150-overlap-a-13.jpg`, `E/gallery-D150-overlap-a-14.jpg`, `E/gallery-D150-overlap-a-15.jpg`.

- Native rows have rectangular border boxes and text '>' rather than the export's unboxed disclosure chevrons.
- Edited dot and Proposed diamond move from before the name to the far right; Proposed diamond is outlined rather than filled.
- Native Session underlines the whole name plus not saved, and not saved stays white; export underlines only the name and colors the suffix amber.
- Native Proposed suffix stays white instead of violet.
- Off and Culled suffixes have the same body sizing as the name rather than smaller secondary captions.
- AgentFocus uses a square full-width outline; export is a compact slightly rounded outline.

## Subject header

Expected native states: Default, Changed.
Export: `ux4-subject-header.png`.
D100: `E/gallery-D100-focused-09.jpg`, `E/gallery-D100-focused-10.jpg`.
D150: `E/gallery-D150-overlap-b-01.jpg`, `E/gallery-D150-overlap-b-02.jpg`.

- The enable box/check and Reset glyph are much larger relative to the title than in the export.
- Reset follows the title/kind directly instead of occupying the far trailing end of a fixed-width header; Changed dot remains before Reset.
- Native Default Reset looks subdued, and the native checkbox/control geometry is square.

## Topic header

Expected native states: Collapsed, Expanded.
Export: `ux4-topic-header.png`.
D100: `E/gallery-D100-focused-09.jpg`, `E/gallery-D100-focused-10.jpg`.
D150: `E/gallery-D150-overlap-b-01.jpg`, `E/gallery-D150-overlap-b-02.jpg`.

- Large filled disclosure triangles replace the export's small chevrons.
- Reset is a larger curved Codicon, and the header is a square full-width strip instead of the compact slightly rounded export.
- Both header backgrounds remain neutral; no blue selection fill is visible.

## Property row

Expected native states: Default, ReadOnly, Proposed, SystemApplied.
Export: `ux4-property-row.png`.
D100: `E/gallery-D100-focused-08.jpg`, `E/gallery-D100-focused-09.jpg`.
D150: `E/gallery-D150-overlap-a-15.jpg`, `E/gallery-D150-overlap-b-00.jpg`, `E/gallery-D150-overlap-b-01.jpg`.

- Default value is centered instead of left-aligned; label/value columns are spaced much farther apart.
- Proposed shows 12.000 and 9.500 rather than 12.0 and 9.5, and uses an outlined diamond instead of a filled diamond.
- SystemApplied puts 'auto' immediately after the value and a gear at far right; export right-aligns auto with a system ring.
- ReadOnly remains one line here; export wraps its policy text into two lines. No native label/value overlap is visible.
- Earlier D150-a-09 was pointer-hover-filled. New overlap-a-15 and overlap-b-00 directly show the dark Default field and unobscured 12.0; the earlier fill observation is not used as default appearance.

## Chip

Expected native states: Severity, Actor, Provenance, Count.
Export: `ux4-chip.png`.
D100: `E/gallery-D100-focused-02.jpg`, `E/gallery-D100-focused-03.jpg`.
D150: `E/gallery-D150-overlap-a-05.jpg`, `E/gallery-D150-overlap-a-06.jpg`.

- WARN 12 is entirely amber; the export uses neutral gray for count 12.
- Actor diamond follows the label rather than leading it.
- Provenance has dashed underline below not saved; export has a leading dash mark.
- Pills are wider/padded with more open digit spacing; Count outline is visibly stronger at D100.

## Activity strip

Expected native states: idle, working, awaiting, proposed, applied, error, stale, Measuring, Loading, Capture pending.
Export: `ux4-activity-strip.png`.
D100: `E/gallery-D100-focused-13.jpg`, `E/gallery-D100-focused-14.jpg`, `E/gallery-D100-focused-15.jpg`.
D150: `E/gallery-D150-overlap-d-01.jpg`, `E/gallery-D150-overlap-d-02.jpg`, `E/gallery-D150-overlap-d-03.jpg`, `E/gallery-D150-overlap-e-00.jpg`, `E/gallery-D150-overlap-e-01.jpg`.

- Native specimens are unboxed stacked labels with bars underneath; export puts marks, verbs, progress and Stop in compact single-row strips.
- Measuring uses a blue operator dot and omits 120 / 600 frames; export uses a gray system ring and the frame count.
- Capture pending omits 'waiting for a drawable' and the export's Stop action.
- Working/proposed agent fixtures omit the export's named light-edit task, 3 / 5 count and Stop.
- Native Measuring Stop is a labeled text button, whereas the export uses a square stop icon.
- The seven explicit lifecycle fixtures and Loading are additions beyond the export's three shown activity samples; error/stale include extra fixture evidence messages.

## Proposal card

Expected native states: Pending, Applied.
Export: `ux4-proposal-card.png`.
D100: `E/gallery-D100-focused-15.jpg`, `E/gallery-D100-focused-16.jpg`, `E/gallery-D100-focused-17.jpg`, `E/gallery-D100-focused-18.jpg`, `E/gallery-D100-focused-19.jpg`, `E/gallery-D100-focused-20.jpg`.
D150: `E/gallery-D150-overlap-e-02.jpg`, `E/gallery-D150-overlap-f-00.jpg`, `E/gallery-D150-overlap-f-01.jpg`, `E/gallery-D150-overlap-f-02.jpg`.

- Native title is Adjust local-light intensity with a separate Agent / 2 changes state row; export uses Agent proposal, Balance the atrium fill light and a trailing status/time pill.
- Only one numeric diff is rendered although the native card says 2 changes; export shows two named Local Light 9 changes (Intensity and Range).
- Native diff is a full bordered field with 12.000/9.500 and an outlined diamond; export uses two compact right-aligned 12.0→9.5 and 8.0→10.0 lines.
- Evidence is a large underlined generic fixture link rather than the small Mono filename, frame count and missed-list evidence.
- Pending actions are left-aligned Show, Accept, Reject with violet borders; export right-aligns Show, Reject, Accept with neutral secondaries.
- Applied Revert is on its own following row; export places it at the trailing side of the explanatory line.
- Both cards add a gallery-fixture disclaimer and are much wider/taller. No internal clipping or overlap is visible in their complete captures.

## Notice

Expected native states: Success, Failure.
Export: `ux4-notice.png`.
D100: `E/gallery-D100-focused-10.jpg`, `E/gallery-D100-focused-11.jpg`.
D150: `E/gallery-D150-overlap-b-02.jpg`, `E/gallery-D150-overlap-b-03.jpg`, `E/0026.jpg`.

- Native actions sit below and outside the notice box, use primary blue fills, and are larger; export contains neutral buttons within the notice.
- No close × is drawn in either native notice.
- Success path/time uses large body Sans rather than the smaller Mono secondary line in the export.
- Failure drops the specific arch/arch_01 values and unchanged-file sentence from the export.
- Native boxes are darker and wider with more obvious rounding; green/red left status rails remain visible.

## Legend chip

Expected native states: Motion, HZB.
Export: `ux4-legend-chip.png`.
D100: `E/gallery-D100-focused-10.jpg`, `E/gallery-D100-focused-11.jpg`.
D150: `E/gallery-D150-overlap-b-03.jpg`, `E/gallery-D150-overlap-b-04.jpg`, `E/gallery-D150-overlap-c-00.jpg`.

- Native rounded container encloses only the title line; swatches and range text sit outside it. Export encloses title, swatches and ranges together.
- Native title omits dropdown chevron, keeps × beside the title, and renders HZB minus/count/plus as text without separate small button boxes.
- Range labels cluster at the left instead of aligning to left/center/right below the swatches.
- Native swatches span the gallery width, with a large empty separation between title text and far edge.

## Console row

Expected native states: Trace, Debug, Info, Warning, Error, Critical.
Export: `ux4-console-row.png`.
D100: `E/gallery-D100-focused-12.jpg`, `E/gallery-D100-focused-13.jpg`.
D150: `E/gallery-D150-overlap-c-02.jpg`, `E/gallery-D150-overlap-c-03.jpg`, `E/gallery-D150-overlap-d-00.jpg`, `E/gallery-D150-overlap-d-01.jpg`.

- Each native timestamp, severity pill and message occupies a separate line; export aligns them in one row.
- Native pills use title case Warning/Error/Info instead of uppercase WARN/ERROR/INFO, and are wider.
- Warning/Error use generic Scene loading diagnostic specimen messages instead of the export's specific MetalFX fallback and stale scene hash messages.
- Info names Geist Regular, 16 pt rather than the export's obsolete Inter Regular sentence; this is consistent with the accepted typeface change.
- Native date is 2026-09-30 instead of 2026-09-29; extra Trace, Debug and Critical states are shown beyond the export's three severities.

## Graph card

Expected native states: Raster, Compute, Culled.
Export: `ux4-graph-card.png`.
D100: `E/gallery-D100-focused-11.jpg`, `E/gallery-D100-focused-12.jpg`.
D150: `E/gallery-D150-overlap-b-04.jpg`, `E/gallery-D150-overlap-c-00.jpg`, `E/gallery-D150-overlap-c-01.jpg`, `E/gallery-D150-overlap-c-02.jpg`.

- Native title and cost are joined as one Mono string; export uses a Sans title with a separately right-aligned Mono cost.
- Pin labels are visibly Mono in native instead of the export's small Sans labels.
- Native cards stretch across the gallery and are taller with more pin spacing.
- Culled dashes appear only below the card, rather than around its full perimeter as in the export.

## Menu item

Expected native states: Default, Hover, Checked, Disabled.
Export: `ux4-menu-item.png`.
D100: `E/gallery-D100-focused-06.jpg`, `E/gallery-D100-focused-07.jpg`.
D150: `E/gallery-D150-overlap-a-11.jpg`, `E/gallery-D150-overlap-a-12.jpg`, `E/gallery-D150-overlap-a-13.jpg`.

- Checked visibly displays '? Auto (system)' instead of the export's blue checkmark. The screenshot establishes the wrong displayed glyph, not its cause.
- F follows Frame Selected within the label instead of occupying the trailing shortcut column.
- Native rows have full-width border boxes and much larger vertical spacing; export uses unboxed compact menu rows.
- Disabled reason tooltip is not visible in either reviewed context; native macOS menu behavior is not established by these gallery specimens.

## Attention ring

Expected native states: Row, Field.
Export: `ux4-attention-ring.png`.
D100: `E/gallery-D100-focused-15.jpg`, `E/gallery-D100-focused-16.jpg`.
D150: `E/gallery-D150-overlap-e-01.jpg`, `E/gallery-D150-overlap-e-02.jpg`.

- Native Row is Local Light 9 · attention without a disclosure chevron; export uses a chevron and a separate agent-reading caption.
- Native Field is one line 'Intensity 12.000' inside a single outline, rather than a labeled relative-intensity row with a separate filled numeric input showing 12.0.
- Both native variants omit the leading diamond/caption 'Agent is reading this · not selected'.
- Native outlines are square and full-width; export outlines are compact and slightly rounded.

