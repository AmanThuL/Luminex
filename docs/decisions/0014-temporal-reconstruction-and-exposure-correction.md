# ADR 0014: Temporal reconstruction contract, ping-pong ownership and exposure correction

**Status**: Accepted (2026-09-08) · **Roadmap**: ../roadmap.md (M6.2) ·
**Spec**: ../specs/2026-09-07-m6.2-native-taa-exposure-design.md ·
**Milestone**: ../milestones/m6.2.md

## Context

M6.1 gave the frame trustworthy motion and one history that was always a raw-colour copy, with no
consumer accumulating over it. M6.2 needed a real reconstruction technique — native-resolution TAA
that reprojects, rejects, clips and blends — plus exposure that adapts at a bounded rate and a
history that is corrected for whatever exposure it was recorded at, since a changing exposure makes
every history texel describe a different brightness than the frame it blends with. Three questions
needed settling once, the way ADR 0013 settled motion and reset ownership: what shape the
reconstruction stage's inputs and outputs take, and how much of the algorithm they commit to before
a second implementation (M6.4's MetalFX adapter) exists; how two colour and two depth slots are
owned and their terminal uses tracked across a mode switch that is not a reset; and where the single
source of applied exposure lives so the resolve, the metering and the shading agree on what
"applied this frame" means.

## Decision

**Reconstruction contract.** `Source/Render/TemporalResolve.h` declares `TemporalInputs` (the
graph handles and per-frame values one frame hands the stage: `sceneColor`, `depth`,
`previousDepth`, `motion`, `reactive`, `history`, `colorSlot`, the exposure pair, both cameras,
extents, the reset reason and the reconstruction mode) and `TemporalResolveOutputs` (`resolved`,
and the `rejection`/`reprojected` diagnostics, declared only when a debug view sinks them). The
stage is a plain class the `Renderer` composes — it owns its pipelines, both history pairs and the
recorded terminal use of each slot, and the Renderer builds its inputs from graph handles and
routes its output without reading the stage's internals. No virtual reconstruction interface exists
yet; `TemporalInputs`/`TemporalResolveOutputs` is the seam M6.4 needs when a second implementation
exists to abstract over, and introducing the interface now against one implementation would be
speculative.

**Ping-pong ownership and the terminal-use invariant.** Two `RGBA16Float` colour slots
(`lmx.render.historyColor0/1`) and two `D32Float` depth slots (`lmx.render.sceneDepth0/1`) ping-pong
by declared temporal frame parity. Under `NativeTaa` the resolve writes the slot directly — no
copy — and under `Raw` the mode-gated commit copy writes it, so in both modes **every temporal
frame's colour slot holds that frame's output**. That invariant is what makes a reconstruction-mode
switch not a reset: both modes leave a real frame in the colour slot, so `Raw → NativeTaa` derives
`HistoryResetReason::None` and blends from the raw copy. The stage records each colour slot's actual
terminal use — `StorageWrite` after a resolve, `CopyDestination` after a raw commit — the way the
Renderer already recorded motion's in M6.1, so the next frame's import states the use the other mode
actually left it in rather than a mode-specific default. Both depth slots import `ShaderRead`,
because the resolve samples both depth slots every `NativeTaa` frame; that is their true terminal
use, and it is the same conservative rule M6.1 already applied to the single depth target it had.

**Exposure buffer as the single source of applied exposure.** `lmx.render.exposureBuffer` grows to
two floats, `{applied, previous}`. `ExposureSeed.slang` becomes a shift-and-set
(`previous = applied; applied = params.exposure`), declared on every manual-mode temporal frame and
on auto-mode reset frames, so the buffer — not a CPU-remembered value — is what the resolve, the
scene pass and the histogram all read as "applied this frame." `ExposureResolve.slang` bounds
adaptation to a stops-per-second rate rather than an exponential lerp, so settle time follows from
step size and rate exactly: `evNext = evApplied + clamp(evTarget − evApplied, −rate·dt, +rate·dt)`.
The resolve corrects the fetched history by `exposure[0] / exposure[1]` (applied over previous)
before clipping and blending it, so a manual EV edit or an auto adaptation step never blends two
frames recorded at different brightness without correction. The reactive weight
(`lmx.render.reactive`, `R8Unorm`) is a third scene-pass attachment (`extraColor[1]`) carrying a
per-pixel "do not accumulate me" signal the emissive term writes, forcing `alpha = 1` in the blend —
kept as a texture, not packed into an existing channel, because it is written by the same raster
pass that already produces motion and needs its own clear value.

**Disocclusion dilates on both sides.** The reconstructed-versus-fetched depth comparison uses this
frame's 3×3 closest depth (already computed for motion dilation) and the previous frame's 3×3
closest depth around `historyUv`, not a point sample on either side — a point-sampled test rejected
thin geometry on nearly a third of its texels every frame from jitter-driven sub-pixel coverage
flips alone, not real disocclusion. `kDisocclusionTolerance` stays at its spec-frozen 0.05.

## Consequences

- A future consumer of either colour slot reads the terminal use `recordFrame` actually left,
  never a mode-specific assumption; a third reconstruction mode extends the same recorded-use
  mechanism rather than inventing a parallel one.
- The exposure buffer's two-float layout is now load-bearing for every reader: `HistogramAccumulate.
  slang` and both auto scene/sky pipelines keep reading index 0 only, and any future exposure
  consumer must read the pair, not assume a single float.
- `TemporalInputs`/`TemporalResolveOutputs` is the contract M6.4's MetalFX adapter is expected to
  satisfy behind the same Renderer-composed seam; introducing a virtual interface is deferred until
  a second implementation exists to justify one.
- The dilated disocclusion test is binding on every future consumer of the reconstruction stage;
  a narrower point-sampled variant is not an acceptable regression without re-measuring the
  thin-geometry and ghosting tolerances together, since loosening the tolerance to compensate was
  shown to trade one for the other.

## Alternatives considered

- **Copy-based history instead of ping-pong slots.** Rejected: it would add a copy pass to every
  `NativeTaa` frame purely to preserve a single-history-texture shape, when the resolve can already
  write its output directly into the slot the next frame reads as history.
- **Reactive weight packed into scene-colour alpha instead of a third attachment.** Rejected:
  scene colour's alpha is unused today but packing a per-pixel accumulation control into it would
  couple two orthogonal concerns and complicate a future consumer of scene-colour alpha; a
  dedicated `R8Unorm` attachment costs one byte per pixel and one attachment slot, well inside
  `kMaxExtraColorTargets`.
- **An abstract reconstruction interface now, ahead of M6.4.** Rejected: M6.2 has exactly one
  implementation; a virtual seam designed against one concrete shape risks needing revision once
  MetalFX's actual input requirements are known, which is exactly the situation ADR 0013 avoided
  for history ownership by keeping Render's ownership concrete until a second consumer existed.
- **Exponential exposure smoothing instead of a bounded stops-per-second step.** Rejected: a lerp's
  settle time depends on both the smoothing factor and the size of the jump, which is not
  reproducible against a frozen frame-count tolerance the way a fixed rate and a fixed `dt` are.
- **Unifying the exposure reset with the history reset.** Rejected, as recorded in the spec:
  `TemporalSettings::sceneGeneration` bumps on any content change, so deriving the exposure reset
  from it would restart metering on every Inspector drag — a behaviour change nothing in this slice
  asked for. The blend's exposure correction already absorbs the discontinuity a real exposure reset
  produces, so unifying the two events buys nothing this slice needs; `App/ExposureReset.h` stays
  untouched.

## Cross-references

Spec: `docs/specs/2026-09-07-m6.2-native-taa-exposure-design.md` (amended 2026-09-08 for the
dilated disocclusion test, the rate-0 adaptation guard, the auto→manual correction-ratio bound,
and the `TemporalInputs`/`recordFrame` contract additions). ADR: `docs/decisions/0013-temporal-
motion-and-history-contract.md`. Plan: `docs/plans/2026-09-07-m6.2-native-taa-exposure.md`.
Milestone: `docs/milestones/m6.2.md`.
