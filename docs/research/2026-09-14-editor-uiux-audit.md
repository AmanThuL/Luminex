# Editor UI/UX Audit — 2026-09-14

**Status**: Frozen — non-normative

The current editor exposes useful rendering controls, but data clipping, misleading state and
unstable graph navigation impede human inspection. This note records observed behavior and
reproduction steps. The accepted response and delivery order belong to
[Editor Experience](../roadmap/editor-experience.md); recommendations here are historical evidence.

## Method and environment

The audit built and ran the default App on an Apple M3 Max after R1 and the gate B acceptance.
The main window was maximized within the display's usable bounds with its native macOS title bar:
1674 × 1052 pt client, 3348 × 2104 px swapchain. The restored workspace's viewport was
1133 × 804 pt; Reset Default Layout produced 969 × 726 pt. A restored window at 1280 pt width
was also examined. Clipping occurred in both restored and default maximized layouts.

The computer-use tool could not address the bare SDL executable, so a temporary application
wrapper launched the same built App, working directory and default arguments. Native screenshots
and interactions supplied the observations; source inspection supplied supporting explanations.
The ImGui controls were not exposed as individual accessibility elements, so interactions used
screen coordinates. Screenshot previews were scaled and do not establish native text size.

The external `luminex-uiux-audit` bundle contains the original Chinese report, 30 screenshots,
a panel-visibility contact sheet, five graph dumps, run logs and original/restored workspace
metadata. Raw artifacts stay outside the published source tree under documentation policy.
The original `imgui.ini` was restored byte-for-byte and the App exited after the audit.
Timing values observed during UI automation are not benchmark evidence.

## Interaction coverage

| Surface | Exercised behavior |
|---|---|
| Scene catalog | Loaded Sponza, Damaged Helmet, Milk Truck, MaterialLab, TemporalLab and optional San Miguel |
| Scene list | Search match/no result/clear, long-list scrolling, representative objects, three lights, keyboard row navigation |
| Inspector | Camera fields/reset, object transform components, light direction/radiance, rendering toggles/sliders/color popup, exposure/bloom/shadow groups |
| Temporal | Raw/Native TAA/MetalFX, temporal on/off, seven diagnostic modes, vendor-disabled views, render scale and dynamic-resolution controls, camera cut |
| Playback | Play/pause/step/reset time, including visible TemporalLab motion |
| Performance | Pause/resume sampling, clear history, waiting and populated states |
| Render Graph | Live display, temporal on/off comparison, selection, group expand/collapse, sink details, zoom, columns control, frame dumps |
| Workspace | All four menus, each docked panel's visibility, reset layout, restored/maximized window, detached graph, File Quit |
| GPU capture | Menu and C shortcut under default startup; both produced the capture-disabled warning in the log |

Representative controls and widget families were covered, not every object row or numeric value.
Continuous RMB + movement/key camera gestures could not be fully validated with the available
tool. Node dragging and docking were attempted without confirmed movement; neither is an
established product defect or a passed check. Successful capture with the enabling environment,
unsupported-vendor hardware and multi-monitor behavior were not tested. This audit does not
complete the separate historical M6 manual-quality or Xcode inspection follow-ups.

## Findings and reproduction

| Priority | Observation | Reproduction / representative artifact |
|---|---|---|
| P1 | Performance summaries and timing columns clip, including at the default maximized layout; compact mode can leave only pass names visible | Reset Default Layout, inspect Performance, restore to 1280 pt width; `01-startup.jpg`, `24-reset-default-layout.jpg`, `25-window-restored-size.jpg` |
| P1 | Rendering Inspector mixes settings, playback and verbose telemetry; exposure labels clip on the right | Select Rendering, expand auto exposure; `02-rendering-native.jpg`, `03-autoexposure.jpg` |
| P1 | Inspector GPU time can remain zero or stale; temporal-off mode still presents Native TAA/warmup, editable scale disagrees with effective extent, last reset loses its reason | Toggle dynamic resolution and temporal inputs, change scale, perform camera cut and wait; `04-temporal-off-status.jpg`, `05-disabled-scale-mismatch.jpg`, `10-dynamic-resolution.jpg` |
| P1 | Default Native TAA graph repeatedly loses selection and changes scale; temporal-off selection persists, but expanded labels can overlap and details clip | Select a graph pass in Sponza or San Miguel with TAA, then repeat with temporal inputs off; `26-default-sponza-graph.jpg`, `21-graph-temporal-off-selection.jpg`, `22-graph-sink.jpg` |
| P2 | Numbered object names and transforms do not identify the selected geometry; selector/search scroll away; filtering leaves an unexplained hidden selection | Scroll San Miguel's 281 objects, select an object, filter it out; `13-filter-empty.jpg`, `16-scene-scroll.jpg`, `17-selected-unknown-object.jpg` |
| P2 | Diagnostic colors and lab grids lack explanatory legends or axes | Switch motion/reprojection/rejection/age views; open MaterialLab; `07-motion-view.jpg`, `08-reprojection-error.jpg`, `09-rejection-mask.jpg`, `14-material-lab.jpg` |
| P2 | Playback is buried inside rendering settings without time/step size; pause meanings differ; object/light edits lack clear recovery and component labels | Operate TemporalLab and compare Inspector with Performance Pause; `11-object-inspector.jpg`, `12-light-inspector.jpg`, `15-temporal-playback.jpg`, `18-performance-cleared.jpg` |
| P2 | Graph dump succeeds only visibly in logs; default GPU capture appears actionable but provides no UI result | Dump a graph frame, invoke capture by menu and C; `23-capture-no-feedback.jpg`, graph dumps and `recheck.log` |
| P3 | Similar text/control density obscures hierarchy; sparse subject inspectors and dense Rendering use space unevenly | Compare camera/light/object inspectors, Rendering and default/compact workspace screenshots |

The `07-motion-view.jpg` screenshot uses MetalFX motion diagnostics. The attempted-selection
image `20-graph-pass-details.jpg` has no retained selection; the confirmed temporal-off selection
evidence is `21-graph-temporal-off-selection.jpg`.

## Source corroboration and uncertainty

- [PerformancePanel](../../Source/App/Panels/PerformancePanel.cpp) reserves a fixed 260 pt stats
  column plus a plot width floor before allocating the timing table. Its timed pass sum is
  explicitly narrower than total GPU frame time; a redesign must preserve that meaning.
- [DynamicResolution](../../Source/App/Model/DynamicResolution.cpp) updates its last measurement
  while the controller is active. [InspectorPanel](../../Source/App/Panels/InspectorPanel.cpp)
  presents this as frame GPU time, explaining the stale/zero observation. Keep controller
  observations distinct from independent retired-frame telemetry.
- [Renderer](../../Source/Render/Renderer.cpp) writes the current reset reason each frame while
  retaining the event frame only when a reset occurs. The UI needs a coherent last-event pair;
  replacing the render contract blindly would risk changing existing consumers.
- [GraphNodeModel](../../Source/App/Model/GraphNodeModel.cpp) includes pin labels and resource
  identity in its shape signature; [RenderGraphCanvas](../../Source/App/Panels/RenderGraphCanvas.cpp)
  clears selection and arranges the picture when that signature changes. Alternating temporal
  resources are a plausible cause of the observed instability. The symptom and on/off contrast
  were confirmed; this causal explanation remains a hypothesis requiring focused reproduction.
- [RenderGraphDump](../../Source/App/Panels/RenderGraphDump.cpp) reports results through logging.
  The capture recheck log recorded the missing enabling environment for both action routes;
  no successful GPU capture was claimed.

## Synthesis

Make the existing information reliable and navigable before adding more GPU-driven counters.
Retain the large viewport, direct camera editing, shared settings, genuine metric freeze and
independent native graph window. Establish a common hierarchy and control language across panels.
Present measurement provenance, mode dependencies and action results beside the relevant data.
Use real tasks and matched before/after screenshots for acceptance. The roadmap owns which
recommendations are adopted, their exact completion boundary and deferred editor systems.
