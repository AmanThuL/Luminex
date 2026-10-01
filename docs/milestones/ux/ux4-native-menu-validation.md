# UX4 native menu checkpoint

**Status**: In progress

The locked-Mac result below is historical. The [2026-10-01 unlocked follow-up](ux4-native-followup-validation.md)
records newly observed menu openings, physical shortcuts, focused-text editing and document
routes in their stated contexts. Nested menu leaves and the complete command/context matrix
remain unverified; the original rows below do not override those dated observations.

Task 16 native verification was **UNVERIFIED** at this checkpoint. Computer Use reported a locked Mac in the
Task 14 access attempt (`task-14/cua/locked-mac-attempt.json`). The controller requested manual
unlock; no unlock response or other state change was recorded at this checkpoint. No additional access retry,
editor gesture, OS preference change or screenshot occurred here. Screenshot paths below are
Unavailable, never inferred from CPU/AppKit probes.

Every row below requires the head in Dark and Light, maximized and at 1280 × 720. Command rows
cover both menu selection and the listed shortcut when present. Parent comparison in those
window sizes and both system appearances remains unverified under the same blocker; its older
ImGui menu row is the expected baseline, with no new Appearance/Density or native adapter.
The earlier parent observations remain in [parent validation](ux4-parent-validation.md).

| Action | Expected result | Observed result | Status | Screenshot path |
| --- | --- | --- | --- | --- |
| Open Luminex menu | Native menu opens with the current model, enabled state and reasons | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| Open File menu | Native menu opens with the current model, enabled state and reasons | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| Open Edit menu | Native menu opens with the current model, enabled state and reasons | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| Open View menu | Native menu opens with the current model, enabled state and reasons | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| Open Window menu | Native menu opens with the current model, enabled state and reasons | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| Open Debug menu | Native menu opens with the current model, enabled state and reasons | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| Open Help menu | Native menu opens with the current model, enabled state and reasons | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| Luminex > About Luminex | The standard application action runs once | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| Luminex > Hide Luminex | The standard application action runs once | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| Luminex > Hide Others | The standard application action runs once | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| Luminex > Show All | The standard application action runs once | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| File > Open… | The existing document workflow runs once with its stopped/measurement/dirty gates | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| File > Save | The existing document workflow runs once with its stopped/measurement/dirty gates | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| File > Save As… | The existing document workflow runs once with its stopped/measurement/dirty gates | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| File > Revert | The existing document workflow runs once with its stopped/measurement/dirty gates | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| File > Open Scene > Sponza | Catalog action runs once; availability and recovery guidance remain intact | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| File > Open Scene > Material Lab | Catalog action runs once; availability and recovery guidance remain intact | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| File > Open Scene > Temporal Lab | Catalog action runs once; availability and recovery guidance remain intact | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| File > Open Scene > San Miguel | Catalog action runs once; availability and recovery guidance remain intact | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| File > Open Scene > Visibility Lab | Catalog action runs once; availability and recovery guidance remain intact | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| File > Open Scene > Light Lab | Catalog action runs once; availability and recovery guidance remain intact | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| File > Open Scene > retry after a load failure | Retry targets the retained failed catalog identity once | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| Luminex > Quit with clean and dirty documents | Quit uses the existing Save/Discard/Cancel workflow once; File has no duplicate Quit | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| View > Set Scene Camera from View | The named shared route runs once; existing availability and selection behavior remains | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| View > Reset Camera | The named shared route runs once; existing availability and selection behavior remains | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| View > Frame Selected | The named shared route runs once; existing availability and selection behavior remains | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| View > Selection Outline | The named shared route runs once; existing availability and selection behavior remains | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| View > Editor Camera | The named shared route runs once; existing availability and selection behavior remains | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| View > Debug View > Final | The selected diagnostic or Final is checked; disabled state has its original reason | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| View > Debug View > Motion | The selected diagnostic or Final is checked; disabled state has its original reason | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| View > Debug View > Reprojection | The selected diagnostic or Final is checked; disabled state has its original reason | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| View > Debug View > Reprojected | The selected diagnostic or Final is checked; disabled state has its original reason | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| View > Debug View > Rejection | The selected diagnostic or Final is checked; disabled state has its original reason | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| View > Debug View > Weight | The selected diagnostic or Final is checked; disabled state has its original reason | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| View > Debug View > Age | The selected diagnostic or Final is checked; disabled state has its original reason | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| View > Debug View > Light count | The selected diagnostic or Final is checked; disabled state has its original reason | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| View > Debug View > Light overflow | The selected diagnostic or Final is checked; disabled state has its original reason | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| View > Debug View > Light missed | The selected diagnostic or Final is checked; disabled state has its original reason | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| View > Debug View > HZB mip 0 | If this mip exists it selects once; otherwise it retains its actual unavailable reason | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| View > Debug View > HZB mip 1 | If this mip exists it selects once; otherwise it retains its actual unavailable reason | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| View > Debug View > HZB mip 2 | If this mip exists it selects once; otherwise it retains its actual unavailable reason | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| View > Debug View > HZB mip 3 | If this mip exists it selects once; otherwise it retains its actual unavailable reason | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| View > Debug View > HZB mip 4 | If this mip exists it selects once; otherwise it retains its actual unavailable reason | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| View > Debug View > HZB mip 5 | If this mip exists it selects once; otherwise it retains its actual unavailable reason | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| View > Debug View > HZB mip 6 | If this mip exists it selects once; otherwise it retains its actual unavailable reason | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| View > Debug View > HZB mip 7 | If this mip exists it selects once; otherwise it retains its actual unavailable reason | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| View > Debug View > HZB mip 8 | If this mip exists it selects once; otherwise it retains its actual unavailable reason | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| View > Debug View > HZB mip 9 | If this mip exists it selects once; otherwise it retains its actual unavailable reason | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| View > Debug View > HZB mip 10 | If this mip exists it selects once; otherwise it retains its actual unavailable reason | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| View > Debug View > HZB mip 11 | If this mip exists it selects once; otherwise it retains its actual unavailable reason | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| View > Debug View > HZB mip 12 | If this mip exists it selects once; otherwise it retains its actual unavailable reason | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| View > Debug View > HZB mip 13 | If this mip exists it selects once; otherwise it retains its actual unavailable reason | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| View > Debug View > HZB mip 14 | If this mip exists it selects once; otherwise it retains its actual unavailable reason | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| View > Debug View > HZB mip 15 | If this mip exists it selects once; otherwise it retains its actual unavailable reason | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| View > Debug View > HZB mip 16 | If this mip exists it selects once; otherwise it retains its actual unavailable reason | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| View > Debug View > HZB mip 17 | If this mip exists it selects once; otherwise it retains its actual unavailable reason | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| View > Debug View > HZB mip 18 | If this mip exists it selects once; otherwise it retains its actual unavailable reason | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| View > Debug View > HZB mip 19 | If this mip exists it selects once; otherwise it retains its actual unavailable reason | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| View > Debug View > HZB mip 20 | If this mip exists it selects once; otherwise it retains its actual unavailable reason | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| View > Debug View > HZB mip 21 | If this mip exists it selects once; otherwise it retains its actual unavailable reason | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| View > Debug View > HZB mip 22 | If this mip exists it selects once; otherwise it retains its actual unavailable reason | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| View > Debug View > HZB mip 23 | If this mip exists it selects once; otherwise it retains its actual unavailable reason | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| View > Debug View > HZB mip 24 | If this mip exists it selects once; otherwise it retains its actual unavailable reason | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| View > Debug View > HZB mip 25 | If this mip exists it selects once; otherwise it retains its actual unavailable reason | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| View > Debug View > HZB mip 26 | If this mip exists it selects once; otherwise it retains its actual unavailable reason | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| View > Debug View > HZB mip 27 | If this mip exists it selects once; otherwise it retains its actual unavailable reason | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| View > Debug View > HZB mip 28 | If this mip exists it selects once; otherwise it retains its actual unavailable reason | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| View > Debug View > HZB mip 29 | If this mip exists it selects once; otherwise it retains its actual unavailable reason | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| View > Debug View > HZB mip 30 | If this mip exists it selects once; otherwise it retains its actual unavailable reason | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| View > Appearance > Auto (system) | Editor and menu appearance follow the selected mode; Auto clears forced appearance | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| View > Appearance > Light | Editor and menu appearance follow the selected mode; Auto clears forced appearance | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| View > Appearance > Dark | Editor and menu appearance follow the selected mode; Auto clears forced appearance | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| View > Density > Comfortable | Metrics and the checked density change once and persist | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| View > Density > Compact | Metrics and the checked density change once and persist | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| View > UI Scale > 75% | The selected preset applies once with its checked state | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| View > UI Scale > 90% | The selected preset applies once with its checked state | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| View > UI Scale > 100% | The selected preset applies once with its checked state | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| View > UI Scale > 110% | The selected preset applies once with its checked state | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| View > UI Scale > 125% | The selected preset applies once with its checked state | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| View > UI Scale > 80% | The selected preset applies once with its checked state | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| View > UI Scale > 150% | The selected preset applies once with its checked state | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| View > UI Scale > Zoom In | The scale shared route runs once, including on detached tool windows | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| View > UI Scale > Zoom Out | The scale shared route runs once, including on detached tool windows | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| View > UI Scale > Reset to 100% | The scale shared route runs once, including on detached tool windows | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| Window > Hierarchy | The existing panel toggle or reset runs once with retained layout behavior | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| Window > Inspector | The existing panel toggle or reset runs once with retained layout behavior | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| Window > Rendering | The existing panel toggle or reset runs once with retained layout behavior | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| Window > Viewport | The existing panel toggle or reset runs once with retained layout behavior | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| Window > Console | The existing panel toggle or reset runs once with retained layout behavior | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| Window > Performance summary | The existing panel toggle or reset runs once with retained layout behavior | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| Window > Performance details | The existing panel toggle or reset runs once with retained layout behavior | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| Window > Render Graph | The existing panel toggle or reset runs once with retained layout behavior | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| Window > Style Gallery | The existing panel toggle or reset runs once with retained layout behavior | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| Window > Reset Default Layout | The existing panel toggle or reset runs once with retained layout behavior | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| Debug > Capture Next GPU Frame | Available capture queues once; unavailable/pending state retains visible disabled guidance | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| Help > Controls and scene-specific instructions | All existing instructions remain readable; passive items perform no action | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| Press Command-O outside editing | The existing named route runs once with its original shortcutAllowed restrictions | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| Press Command-S outside editing | The existing named route runs once with its original shortcutAllowed restrictions | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| Press Command-Shift-S outside editing | The existing named route runs once with its original shortcutAllowed restrictions | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| Press Command-Q outside editing | The existing named route runs once with its original shortcutAllowed restrictions | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| Press Home outside editing | The existing named route runs once with its original shortcutAllowed restrictions | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| Press F outside editing | The existing named route runs once with its original shortcutAllowed restrictions | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| Press Command-Minus outside editing | The existing named route runs once with its original shortcutAllowed restrictions | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| Press Command-Plus outside editing | The existing named route runs once with its original shortcutAllowed restrictions | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| Press Command-0 outside editing | The existing named route runs once with its original shortcutAllowed restrictions | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| Press C outside editing | The existing named route runs once with its original shortcutAllowed restrictions | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| Press Command-Equal | The retained physical alias invokes its existing route once | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| Press Command-Keypad Add | The retained physical alias invokes its existing route once | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| Press Command-Keypad Subtract | The retained physical alias invokes its existing route once | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| Press Command-Keypad 0 | The retained physical alias invokes its existing route once | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| Press shifted aliases other than Save | The retained physical alias invokes its existing route once | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| Edit > Cut with an ImGui text field focused | Exactly one Command chord edits that field through SDL | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| Edit > Copy with an ImGui text field focused | Exactly one Command chord edits that field through SDL | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| Edit > Paste with an ImGui text field focused | Exactly one Command chord edits that field through SDL | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| Edit > Select All with an ImGui text field focused | Exactly one Command chord edits that field through SDL | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| Type F, Home and C in an ImGui text field | Text editing retains the keys; no camera or capture action runs | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| Press Command-C and Command-V in an ImGui text field | Copy and paste edit that field; the native delegate does not consume the events | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| Press Command-S once, outside editing, on a dirty stopped document | One save reaches the existing document workflow; no duplicate polling save | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| Open Edit with no text field or only an active non-text widget | All four editing items are disabled with No text field has focus | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| Hover disabled menu rows | Each disabled reason is visible as its native tooltip | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| Use Cocoa chooser text fields and attached sheets | Native editing retains keys; editor commands do not steal those events | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| Hold RMB look, open a popup, and focus detached windows while pressing bare keys | Original keyboard suppression remains; no camera/capture action escapes its guards | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| Press C when capture is unavailable, then while pending | The existing recovery notice is restored; pending requests coalesce | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| Open detached Render Graph and Performance, then use menu commands and zoom | Both access the same application menus and retained zoom routes | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| Dock, undock and resize the editor and detached tools | Menus remain accessible; transport/activity/zoom row fits and retained docking works | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| Observe the macOS menu-bar strip under forced editor appearance | The strip follows system appearance; menu content follows the editor preference | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |
| Record System Appearance and Reduce Motion, toggle both, observe Auto, restore originals | Auto follows live, Reduce Motion snaps; original settings are restored even on failure | Not performed or observed: locked Mac | unverified | Unavailable: locked Mac |

The external invisible-window AppKit tests establish adapter behavior only. They do not prove
physical key delivery, native tooltip visibility, detached access, visual correspondence or
real-editor single dispatch. The system strip limitation is part of the binding plan.
No new System Settings originals were read because access was blocked; no preferences were changed.
