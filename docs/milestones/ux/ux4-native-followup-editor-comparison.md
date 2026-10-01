# UX4 native follow-up editor and Figma comparison

**Status**: Implemented — owner accepted for integration on 2026-10-01; failed and incomplete gates retained as measured

2026-10-01 independent inspection of 25 retained native images and four default-workspace Figma
exports. Return to [follow-up results](ux4-native-followup-validation.md).
Evidence prefix `E = ../Luminex-evidence/ux4/native-followup-2026-10-01/`.
Review sources: `E/native-editor-figma-review.md` and `.json`; the JSON retains input dimensions
and SHA-256 values. Exports are under `../Luminex-Identity/ux4-design/exports/`.

The comparison uses actual window captures at 2× backing. Logical dimensions, selected subjects,
scene/camera, tabs and focus differ from the 1920×1080 references. The macOS-chrome exports own
the current macOS composition; the ordinary exports retain the earlier in-window menu row.
The review lists visible differences and contract concerns without establishing whole-editor
Figma parity, exact token colors, all font sizes, functional dispatch or renderer-image parity.

All 16 classified findings and their individual image paths follow. The image review covers
selected evidence through 0102; the final gesture ledger also includes process-only Quit
0103/0104. The controller subsequently corrects 0058's dimension transcription and distinguishes
0047's live action reading from its screenshot reading; these do not change reviewed image bytes.

## E01: Capture geometry and native chrome

Classification: context-mismatch. Reference: Both macos-chrome exports.
Screenshots: `E/0003.jpg`, `E/0010.jpg`, `E/gesture-0058.jpg`, `E/gesture-0059.jpg`.

References are 1920×1080 compositions including an OS menu strip, centered title and traffic lights. Native captures contain the application window only, with left-aligned title and an OS capture/share badge; windowed evidence is 2560×1504 physical pixels including title, with 2× backing. Maximized evidence is 3348×2168.

OS menu-strip absence, badge, focus dimming and title placement cannot be counted as missing editor implementation. Geometry is not normalized; no pixel-parity or exact sizing verdict is valid. The ordinary exports retain the earlier in-window menu row and are supplemental references, not the current macOS route.

## E02: Different workspace state and scene subjects

Classification: context-mismatch. Reference: Both editor exports.
Screenshots: `E/0003.jpg`, `E/0010.jpg`, `E/gesture-0057.jpg`, `E/gesture-0064.jpg`, `E/gesture-0087.jpg`.

Figma selects dirty Local Light 9 (Spot), 41/41 hierarchy, Console selected, and a tight lion crop. Native starts with Tour Camera selected but Editor Camera inspected, 24/24 hierarchy, Performance selected, and a taller lion/floor framing. Later Light selects Crytek Sponza or Local Light 1 (Point); MaterialLab has 51/51 rows.

Different subjects explain absent spot direction/cone fields, different values, dirty marks, rendered image and lower-pane contents. No matching Light 9 Spot state was captured. Neither whole-editor parity nor missing-Spot-control defects can be concluded.

## E03: Viewport inset and Light surround

Classification: layout-deviation. Reference: Both macos-chrome exports.
Screenshots: `E/0003.jpg`, `E/0010.jpg`, `E/gesture-0057.jpg`, `E/gesture-0058.jpg`, `E/gesture-0087.jpg`.

Figma image abuts its tab/content edges with almost no padding. Actual image is inset on all sides inside its dock panel. In Light the immediate visible surround is white; in Dark it is dark.

Concrete composition deviation. The Light white surround merits controller review against the milestone statement that surface/viewport is a dark surround in both themes. These captures do not identify whether that token applies only to unused image-area pixels or also panel padding, so this review does not assign a code root cause.

## E04: Dock controls and tab treatment

Classification: layout-deviation. Reference: Both macos-chrome exports.
Screenshots: `E/0003.jpg`, `E/0010.jpg`, `E/gesture-0059.jpg`, `E/gesture-0064.jpg`, `E/gesture-0090.jpg`.

Native docks include left tab-list arrows and right dock-level X controls in addition to individual tab X controls. Figma shows simple tabs without those extra dock controls. Native tab widths and blank header space differ. Most unfocused selected tabs have gray top rules; a focused Hierarchy or Viewport has blue in 0064/0090.

Actual ImGui docking affordances are retained. Do not report all blue selected-tab overlines as missing: focus changes the captured state. This remains a visible design/layout difference, not a demonstrated loss of functionality.

## E05: Inspector vectors and labels

Classification: layout-deviation. Reference: Both editor exports.
Screenshots: `E/0003.jpg`, `E/gesture-0057.jpg`, `E/gesture-0058.jpg`, `E/gesture-0064.jpg`, `E/gesture-0090.jpg`.

Figma positions the label above three horizontal XYZ fields. Native Position uses one label column and three vertically stacked X/Y/Z rows, including at maximized size; transform subjects do the same for Rotation/Scale. Labels and digits are readable in these captures.

A substantial density/layout difference, but separate vector rows do not establish overlapping or stacked label ink. The parent 0097 already uses vertical camera XYZ. Matching panel width and the same selected subject would be needed to classify a regression or enforce a horizontal-layout requirement.

## E06: Color components lack visible RGB letters

Classification: visible-label-gap. Reference: Light editor exports.
Screenshots: `E/gesture-0064.jpg`, `E/gesture-0079.jpg`, `E/gesture-0081.jpg`.

Figma color fields explicitly display R, G and B prefixes. Actual Local Light 1 color row shows three numeric boxes (1.000, 0.720, 0.450) with red/green/blue edge strips, and a swatch; no R/G/B letters are visible. XYZ labels are visible on the position rows above.

Specific visible deviation and accessibility/readability concern against the repository statement that vectors name XYZ/RGB. A hover tooltip was not captured, so tooltip labeling is unverified. Do not claim that color values are clipped: the visible digits are complete.

## E07: Subject header checkbox and provenance placement

Classification: layout-deviation. Reference: Light editor exports.
Screenshots: `E/gesture-0064.jpg`, `E/gesture-0079.jpg`, `E/gesture-0081.jpg`.

Figma begins the subject header with a checkbox, then subject/kind; its edited dot sits near Reset. Native begins with the subject, places kind and enabled checkbox near Reset, and puts edited dots after the subject and by the enabled control. Hierarchy edited dots trail the row/root instead of preceding the subject as in Figma.

The operator-blue grammar is recognizable; placement differs. 0079 shows Sponza* and edited dots; 0081 shows clean title and cleared dots after saving. This is scoped visual evidence, not verification of every provenance source or tooltip.

## E08: Hierarchy structure, clipping and off state

Classification: content-and-layout-deviation. Reference: Both editor exports.
Screenshots: `E/0003.jpg`, `E/gesture-0059.jpg`, `E/gesture-0079.jpg`, `E/gesture-0087.jpg`, `E/gesture-0089.jpg`.

Native Sponza has a second nested Sponza node, one Crytek Sponza source-object row, Environment below local lights and no count in the Local Lights group label. Figma has Environment above Local Lights (16), then expanded primitive names. Native long source/generated names clip horizontally and expose a horizontal scrollbar; Figma labels fit its authored width. Figma off examples are gray; selected native Local Light 1 [off] retains dark text on a blue row.

Most differences derive from document topology, selection, data and retained workspace widths. Horizontal scrolling provides access; not proof of lost subjects. Selected row text is explicitly meant to use text/primary, so the selected off-row appearance does not prove a disabled-text defect.

## E09: Console structure and severity chips

Classification: layout-deviation. Reference: Both editor exports.
Screenshots: `E/gesture-0079.jpg`, `E/gesture-0087.jpg`, `E/gesture-0093.jpg`.

Figma has a short search field, three rounded INFO/WARN/ERROR chips, full date/time strings and pill severity markers in unruled rows. Native has a long search, six square T/D/I/W/E/C filter buttons, time-only timestamps and plain severity cells in a ruled table with alternating row fill. A retained Copy visible success line occupies another row.

Concrete component/composition differences. Event counts, missing-mip warning and logged text are runtime content. Figma itself still says Inter in a sample log despite Geist being the accepted typeface; that sample line is not a requirement to restore Inter. Parent 0097 already contains the six filters and table structure.

## E10: Square controls versus softened reference corners

Classification: documented-design-change. Reference: Both editor exports.
Screenshots: `E/0010.jpg`, `E/gesture-0064.jpg`, `E/gesture-0079.jpg`, `E/gesture-0056.jpg`.

Actual ordinary numeric fields, transport controls and Console chips are square; Figma ordinary fields and chips have visibly softened corners. Native notices retain separate rounded card treatment.

The milestone explicitly records radius/control 0 after reductions from 3 through 2, while the Compact cost gate still failed. This is a documented implementation/design divergence, not an unexplained rendering failure or evidence that the cost gate passed.

## E11: Numeric spacing and formatting

Classification: typography-deviation. Reference: Both editor exports.
Screenshots: `E/0003.jpg`, `E/gesture-0064.jpg`, `E/gesture-0037.jpg`.

Actual Sans numbers visibly use wider, regular advances (e.g. Local Light 10, 0.000 s and 45.000), while Figma shows tighter figures and fewer decimals in intensity/range examples. Native data tables use a visibly monospaced face.

The milestone specifies the 0.6 em forced digit advance; differing decimal formats and data affect width. No digit overlap is visible in these representative editor captures. This does not validate 1× rendering, every font size, or all panels at default width.

## E12: Transport order and capture control identity

Classification: layout-deviation. Reference: Both macos-chrome exports.
Screenshots: `E/0003.jpg`, `E/0010.jpg`, `E/gesture-0064.jpg`, `E/gesture-0047.jpg`.

Figma orders Play, Stop, Step, camera-shaped control, then time. Native Sponza places time before the camera-shaped control. Native measuring state shows the progress readout and a second activity label, operator dot, small blue progress line and Stop. MaterialLab in 0087–0093 lacks the Sponza camera-shaped control.

Transport composition differs. The camera-shaped control must not be called a successful GPU Capture button from its glyph: its identity/availability depends on the scene/transport. Absence in MaterialLab is scene-context evidence, not automatically a missing command. 0047 reads 2322/6000 in the retained pixels although its ledger observation says 1694/6000, consistent with a live interval; cite the screenshot value when discussing that image.

## E13: Generated-subject tooltip repeats provenance sentence

Classification: confirmed-presentation-defect. Reference: Milestone provenance/source-tooltip contract; no matching default-editor specimen.
Screenshots: `E/gesture-0088.jpg`.

The tooltip for material-lab sphere r0c0 includes “Generated by material-lab · not saved” twice in consecutive lines; the second instance continues with the scene document path.

Reproducible visible duplication in the retained capture. Keep one provenance sentence with the source path. This does not erase the successful visibility of the session-only underline, explanatory Inspector text or source path.

## E14: Detached Graph and Performance theme evidence

Classification: context-and-coverage-limit. Reference: No detached-window screen in the supplied default-editor references.
Screenshots: `E/0011.jpg`, `E/0012.jpg`, `E/0013.jpg`, `E/0017.jpg`, `E/gesture-0037.jpg`, `E/gesture-0056.jpg`, `E/gesture-0061.jpg`.

Graph changes from dark canvas/cards/chrome to light canvas/cards/chrome. Fit reduces cards enough that labels become small; Details remains readable. Dark Performance Live shows table, plot and definitions; Light Measure shows cancellation reason, path and Export.

Supports these visible surfaces adopting the theme. It does not establish a detached-window Figma layout match. Initial graph clipping is a pan/zoom state; 0013 fits it. Card text shrink under Fit is a usability observation, not a demonstrated 100%-type defect. 0014 was overwritten and cannot establish the original freeze gesture; 0017 is only the later frozen Dark observation. Timings are screen readings, not benchmarks.

## E15: Actual operator/system states without agent runtime

Classification: context-and-coverage-limit. Reference: Milestone actor/provenance contract.
Screenshots: `E/gesture-0047.jpg`, `E/gesture-0072.jpg`, `E/gesture-0079.jpg`, `E/gesture-0081.jpg`, `E/gesture-0088.jpg`, `E/gesture-0089.jpg`.

Visible examples include operator measurement activity, a gear beside Effective scale, edited blue dots, a system-ring save notice, and amber session underlines. Generated object disable retains a clean document title and not-saved wording.

These are bounded UI observations. Source tooltips for every mark, pending-capture first frame, brief load/controller transitions and all status retention are not proved. Agent backend is unimplemented: pending/apply/accept/reject behavior and real agent lifecycle are explicitly unverified; gallery drawings cannot prove them.

## E16: Selection outline and rendered scene are outside theme comparison

Classification: context-mismatch. Reference: Default-editor screenshots do not show the same selected rendered object.
Screenshots: `E/gesture-0088.jpg`, `E/gesture-0089.jpg`, `E/gesture-0090.jpg`, `E/gesture-0091.jpg`, `E/gesture-0092.jpg`, `E/gesture-0093.jpg`.

MaterialLab has one blue outlined sphere at 0088 and loses that sphere when disabled at 0089. The camera view and generated edit persist in later Light captures; toolbar time shows 0.017 after Step and 0.000 after Stop.

Visible Light outline is not a verification of one exact encoded outline constant in both themes; JPEG and unequal scenes preclude that claim. These captures cannot repair renderer image gates or prove that a Light/Dark scene image is byte-identical.

## Remaining limits

- No matched Local Light 9 Spot state or normalized 1920×1080 editor screenshot.
- No native 1× display, Compact editor comparison, or full completion-gesture matrix in this independent review.
- No successful agent proposal runtime: backend unimplemented; gallery-only reserved vocabulary is not behavioral evidence.
- No all-menu/native-shortcut verification, forced-Light-under-Dark proof, or first-frame Reduce Motion proof from these images.
- No whole-editor/Figma parity, whole-application acceptance, cost gate success or renderer parity is claimed.
