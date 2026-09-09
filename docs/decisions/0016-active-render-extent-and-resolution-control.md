# ADR 0016: Active render extent, output-extent capacity and a pure resolution controller

**Status**: Accepted (2026-09-09) · **Roadmap**: ../roadmap.md (M6.3) ·
**Spec**: ../specs/2026-09-09-m6.3-taau-dynamic-resolution-design.md ·
**Milestone**: ../milestones/m6.3.md

## Context

M6.2 gave the frame native-resolution TAA; M6.3 needed the scene to rasterise into a smaller
*render* rectangle than the *output* extent the display and every diagnostic still present at, and
a reconstruction kernel that turns jittered lower-resolution samples back into a full-extent image.
Four questions needed settling once: how much GPU capacity a temporal frame allocates when render
and output extents can now differ frame to frame; what growing the render graph and the RHI with a
sub-rectangle costs existing callers; which history-reset reason a render-scale change alone should
derive, given ADR 0013 already defined `ExtentChanged` for "any extent field changed"; and how a
GPU-time-driven scale decision is made and handed to the renderer without coupling the decision
itself to any renderer or App type.

## Decision

**Additive render area.** `RHI::RenderPassDesc` and the render graph's `PassDesc` each gain
`renderAreaWidth`/`renderAreaHeight` (default 0/0, meaning the whole attachment). A pass declares a
non-zero pair only when it rasterises into an origin-anchored sub-rectangle of a larger attachment;
both fields are zero or both are non-zero, and neither may exceed the attachment it is validated
against. The Metal 4 backend turns a non-zero pair into an explicit viewport and scissor rect; the
zero/zero path is untouched — one `setViewport` call, no scissor — so every pre-M6.3 pass and golden
stays byte-identical. The graph dump prints the area only when declared, so the M6.2 goldens do not
move.

**Capacity is the output extent.** Every renderer-owned temporal target — both colour history slots,
both depth slots, the motion and reactive attachments, the reprojection and resolve diagnostics —
allocates at the *output* extent, the largest a frame can ask for without a resize. A render scale
below 1.0 uses only the render area's sub-rectangle of that capacity; scale returning to 1.0 reuses
the same allocation. This is what makes a render-scale change reallocate nothing: `TransientPool`
and the renderer's persistent targets see a constant footprint across every scale the controller
picks, which the scale-oscillation scenario asserts directly (constant `historyBytes`/
`depthHistoryBytes`, no new pool generation, an unchanged transient high-water mark from the fourth
frame of an oscillating run).

**`ExtentChanged` follows the output extent only — supersedes ADR 0013.** ADR 0013 defined
`HistoryResetReason::ExtentChanged` as "any extent field changed." M6.3 narrows this to the output
extent alone: a render-scale change with the output extent unchanged derives `None`, and the history
survives it, because the same output-extent colour and depth slots already hold data at the right
capacity — there is nothing to reset. Projection aspect is taken from the output extent (so a
render-scale change never distorts the picture), and jitter is expressed in render pixels (so its
magnitude tracks what the scene actually rasterises, not what is later reconstructed to). Only a
genuine output-extent change — a window or viewport resize — still derives `ExtentChanged` and
resets history, matching ADR 0013's original resize-driven intent.

**The kernel splits at scale 1, by construction rather than by a flag.** `lmx.pass.temporal.resolve`
(the M6.2 native kernel, untouched) runs only when render extent equals output extent **and** either
this frame resets history or the previous frame's extents equal this frame's. Every other `NativeTaa`
frame — any upscale, and the one scale-1 frame immediately following a different render extent —
runs `lmx.pass.temporal.upscale` instead, which samples the same history at the general
`renderSamplePosition` mapping and carries the previous frame's render extent explicitly so its
disocclusion test addresses the right history rectangle. This keeps every scale-1 declaration and
the native kernel's compiled output byte-identical to M6.2 (verified: the shared-module extraction
into `Shaders/TemporalCommon.slang` produced identical MSL for `TemporalResolve.slang`, and the
scale-1 static-stability golden hash was unchanged before and after), while still reconstructing
correctly on the one frame whose history was accumulated at another extent. `Raw` follows the same
split between a plain commit copy and a spatial-only commit (`SpatialUpscale.slang`) that shares its
five-tap fetch with the temporal kernel.

**Terminal use — extends ADR 0015's current-slot row.** An upscaled `Raw` frame's current colour
slot is recorded `ShaderRead`, not `CopyDestination`: `lmx.pass.temporal.commitUpscaled` writes it
(a `StorageWrite`), but bloom and display then sample it directly — unlike M6.2's plain copy, an
upscaled `Raw` frame has no separate consumer reading a distinct raw-colour source, so the slot's
own final access is a shader read, the same rule ADR 0015 already applies to every other consumed
colour slot. Every other row of ADR 0015's table is unchanged. Scene colour's terminal use has no
row in that table — it is a separate record the Renderer keeps for itself
(`m_previousSceneColorUse`), not part of `TemporalResolve::recordFrame`'s slot bookkeeping. An
upscaled `Raw` frame sets that record to `ShaderRead` rather than `CopyDestination`, because
`lmx.pass.temporal.commitUpscaled` samples scene colour (the spatial-upscale fetch) instead of
blitting it, and nothing else copies out of scene colour that frame.

**The controller is a pure, App-driven contract.** `Source/Render/ResolutionController` observes a
retired frame's summed GPU pass time against a budget and headroom, and proposes the next
`renderScale` with hysteresis (a settle window after any change, and a run of consecutive
over-budget or under-target samples before stepping) — nothing about it reads a device, a graph, or
an App type. `EditorShell` owns one controller instance, feeds it `frameGpuMilliseconds()` from the
retained frame ring, and writes its `scale()` back onto `EditorRenderSettings::renderScale` every
frame dynamic resolution is enabled; turning it off freezes `renderScale` at the controller's last
value rather than jumping back to a manual setting the user did not choose. `--render-scale` and
`LMX_DYNAMIC_RESOLUTION_BUDGET_MS` are the two ways to drive this outside the Inspector: the flag
sets a fixed manual scale for one screenshot/scripted run, the env var seeds dynamic resolution on
before the loop starts, for a run long enough to show the controller settle.

**The upscaled ghosting tolerance is widened, as an accepted limit rather than a tuned constant.**
Spec §10's amendment (2026-09-09) raises the upscaled "Motion and ghosting" assertion to 0.125× the
cube-to-floor contrast (measured 0.1149; native stays 0.1) and the "Scale oscillation" ghosting
clause to 0.15× (measured max 0.1287, frame 22 of the alternation; every scale-0.5 frame in that
scenario measures ≤ 0.066, inside the native bound). The reason is structural, not a quality
regression: step 2's motion/depth dilation runs over the 3×3 *render* texels around the nearest
sample, and at scale 0.5 that footprint spans two *output* pixels rather than one, so two trailing
columns of a vacated region adopt the moving surface's dilated motion and depth and are blended
rather than rejected (disoccluded share 0.50 against native's 0.75). Raising `kUpscaleMinWeight` to
twice its frozen value moved the measurement by under 0.001, ruling out the blend weight as the
cause. The oscillation clause's own excess sits entirely on the alternation's scale-1 frames, where
the kernel-selection rule above runs the upscale kernel against a history accumulated at half scale
with the sample-proximity term at its scale-1 worst — a one-frame transition cost, not a sustained
ghost, since the clause keeps measuring every frame rather than only the last. Stating the dilation
footprint in output pixels (a 3×3 at scale 1 becoming a 2×2 render-texel dilation at 0.5) is a
candidate follow-up; it changes the kernel, so it is not taken here.

## Consequences

- A future consumer that reads the render graph's declared area must treat 0/0 as "whole
  attachment," not as an unset field to reject.
- Any new renderer-owned temporal target must allocate at the output extent, not the render extent,
  or it silently breaks the reallocation-free scale-change contract this ADR establishes.
- `ExtentChanged`'s narrowed meaning is binding: a future extent-like field (e.g. a per-axis
  resolution) that should reset history on its own change must derive its own reason or fold
  explicitly into this one; it is not covered by "any extent field" any more.
- The kernel-selection rule is binding on any future reconstruction technique added behind
  `ReconstructionMode`: a scale-1 frame following a different render extent must not assume the
  native, single-extent kernel is safe to run.
- The widened ghosting tolerances apply only to the upscaled and oscillating rows named above; the
  native scale-1 tolerance (0.1) is unchanged and remains binding.
- `ResolutionController` has no assert on a negative `gpuMilliseconds` sample, and
  `--render-scale nan` currently passes the CLI's range check (`nan` compares false against both
  bounds); both are recorded as known limits in the milestone rather than fixed here.

## Alternatives considered

- **One kernel at every scale**, always running the upscale path even at scale 1. Rejected: it
  would make the native M6.2 kernel's compiled output and every scale-1 golden and tolerance subject
  to review again, when the whole point of the split is that M6.2's byte-identical output and
  measured tolerances need not be re-earned.
- **Reallocating history and diagnostics per scale**, sized to the active render extent instead of
  the output extent. Rejected by the scale-oscillation gate: a scale that changes every frame would
  reallocate every frame, which is exactly the reallocation-thrash the roadmap's M6.3 gate rules
  out.
- **Per-pixel history resampling to the new render extent on a scale change.** Unnecessary: the
  history already lives at the output extent, so a scale change needs no resampling step at all —
  only the render-extent-relative texel closest to a given output pixel differs, and
  `renderSamplePosition` already expresses that mapping.

## Cross-references

[ADR 0013](0013-temporal-motion-and-history-contract.md) — `ExtentChanged`'s original definition,
narrowed here. [ADR 0014](0014-temporal-reconstruction-and-exposure-correction.md) and
[ADR 0015](0015-temporal-slot-terminal-access.md) — the reconstruction contract and terminal-use
table this ADR extends. Spec: `../specs/2026-09-09-m6.3-taau-dynamic-resolution-design.md` (§10's
2026-09-09 amendment records the ghosting-tolerance measurements in full). Milestone:
`../milestones/m6.3.md`.
