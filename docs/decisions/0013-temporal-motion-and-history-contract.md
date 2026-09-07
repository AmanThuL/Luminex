# ADR 0013: Temporal motion convention, history ownership and additive attachment growth

**Status**: Accepted (2026-09-07) · **Roadmap**: ../roadmap.md (M6.1) ·
**Spec**: ../specs/2026-09-07-m6.1-temporal-state-motion-design.md ·
**Milestone**: ../milestones/m6.1.md

## Context

Before M6.1, nothing in the shipped frame remembered the previous one: the camera held no previous
pose, a `DrawItem` carried one model matrix and no identity, and the only cross-frame GPU value was
the exposure feedback buffer. M6 needs trustworthy per-pixel motion and a persistent history before
any reconstruction technique can consume them. Three questions had to be settled once, in a way
later M6 slices and a future backend can rely on: what a motion vector means and how invalid motion
is marked; who owns the previous-frame state and when it advances; and how the render graph grows to
carry a second colour attachment without disturbing existing callers.

## Decision

**Motion convention.** `lmx.render.motion` (`RG16Float`) holds `uvCurrent - uvPrevious`, both from
*unjittered* clip positions, with `uv = ndc.xy * (0.5, -0.5) + 0.5` over the render extent
(+y down, texture space). A consumer fetches history at `uv - motion`. The sentinel
`kMotionInvalid = (+inf, +inf)` marks a draw whose deformation is unsupported; consumers test
`isinf`, never compare against zero, so a genuinely stationary object is distinguishable from one
whose motion could not be computed. `Source/Render/Temporal.h` and `Shaders/Motion.slang` are the
single source for this arithmetic; nothing else restates it.

**History ownership.** Engine owns identity and previous transforms — `Scene` keeps each object's
committed previous model matrix, advanced by `Scene::commitFrame()`. Render owns camera history and
the one GPU-resident history texture (`lmx.render.historyColor`, `RGBA16Float` at the render extent)
— `Renderer` keeps the previous `CameraFrameState` and creates, imports and overwrites the history
texture. The history and motion targets are created with the scene targets — in `resize()`, and so
in `create()`, which resizes once — and recreated by `resize()` under the caller's idle guarantee,
rather than on the first frame that enables temporal. Both functions already return a `Result`, so
an allocation failure is reported rather than asserted mid-declaration. Nothing persists on the GPU
that the Renderer does not own and import. *Previous* means the
previous *declared* frame: `Renderer::declarePasses` advances its temporal state as its last act, so
a frame skipped before declaration never becomes "previous," and `Scene::commitFrame()` is called
only after a frame is actually declared.

**Reset reasons and their derivation order.** `HistoryResetReason` is `None`, `FirstFrame`,
`TemporalEnabled`, `SceneChanged`, `ExtentChanged`, `ProjectionChanged`, `CameraCut`, in that
priority. `deriveHistoryReset` is pure: no previous signature → `FirstFrame`; previous temporal-off
and current on → `TemporalEnabled`; then scene generation, extents, projection (`fovY`, `nearZ`),
then the explicit cut; otherwise `None`. The Renderer records a `FrameSignature` every declared
frame, temporal on or off, so re-enabling is distinguishable from a true first frame. Camera cuts are
explicit events (`SceneView::temporal.cameraCut`, raised by the Inspector's Camera cut button and
consumed for one frame) — no motion heuristic guesses a cut.

**Imports state the recorded previous-frame terminal use.** Every temporal-frame import of a
persistent or transient resource declares the *actual* last access the previous temporal frame left
it in, not a convenience default. In particular `lmx.render.historyColor` imports with
`previousUse = CopyDestination`, because the previous frame's last access to it was the commit copy
that overwrote it — declaring a prior `ShaderRead` would leave the cross-frame read-after-write
unbarriered, since a prior read only orders ahead of this frame's first *write*, not its first read.
The motion and scene-colour imports on the temporal path are held to the same rule.

**Additive multi-attachment growth.** RHI `RenderPassDesc` keeps `colorTarget` as attachment 0 and
gains `ExtraColorTarget extraColor[kMaxExtraColorTargets]` (`kMaxExtraColorTargets = 3`) with
`extraColorCount`; `GraphicsPipelineDesc` gains matching `extraColorFormats`/count. Every existing
field and signature is unchanged, and every `[checkpoint-a]` test case is unchanged (the filter
reports the same 1,856 assertions in 19 cases). The render graph's `PassDesc` gains
`extraColor`, each producing its own resource version, validated and scheduled exactly like the
primary attachment. This is the only shape multiple render targets take in Luminex; a future pass
needing a third or fourth target reuses the same fields rather than inventing a parallel path.

**Opt-in in this slice.** `SceneView::temporal.enabled` selects the temporal path; off is the default
for the editor and `--screenshot`, and `TemporalLab` turns it on when selected. With it off,
`declarePasses` declares exactly the pre-M6.1 passes, imports and attachments, and existing
pipelines keep their original entry points — verified by byte-identical fixed-camera screenshots and
an unchanged graph dump. M6.2 flips the default when TAA ships.

## Consequences

- Every future temporal or history-owning pass states its own previous-frame terminal use rather
  than assuming `ShaderRead`; a new consumer of `lmx.render.historyColor` must read the commit pass's
  actual last access, not copy the spec's original wording.
- The reset-reason order is a stable public contract: a later slice that adds a new invalidation
  cause inserts it into this order rather than reordering existing reasons, since `TemporalStatus`
  and diagnostics expose reason values externally.
- `kMaxExtraColorTargets = 3` bounds render-graph and Metal 4 pipeline attachment arrays; a pass
  needing a fourth simultaneous colour output requires revisiting this ADR, not silently growing the
  array.
- The history and motion allocations are permanent for a renderer's life: they are made with the
  scene targets and never freed on disable, since freeing would drop memory in-flight frames still
  hold. `TemporalStatus::historyBytes` reports the cost, which a session that never enables temporal
  pays too.
- The motion convention and sentinel are binding on every future consumer (TAA, MetalFX) so history
  rejection and disocclusion logic added in M6.2+ can rely on `isinf` without re-deriving the
  contract.

## Alternatives considered

- **A separate motion pass instead of an extra scene-pass attachment.** Rejected: it would redraw
  every opaque and sky object a second time per frame for values already available in the scene
  pass's vertex/fragment stage, and would need its own depth test to agree with the primary pass's
  results pixel-for-pixel — the multi-attachment path shares one raster pass and one depth test by
  construction.
- **Depth-derived camera motion (reproject the current depth buffer through the previous
  view-projection) instead of an explicit per-object motion target.** Rejected for this slice: it
  cannot express rigid-object motion or the invalid-deformation sentinel, both of which the roadmap's
  M6 shared rules require now, and would still need a full motion pass later for objects — adopting
  it would not avoid the cost the first alternative was rejected for.
- **Always-on temporal inputs instead of opt-in.** Rejected: M6.1 ships one diagnostic history with
  no accumulation or display consumer yet; forcing every scene through the reset-derivation and
  extra-attachment path before a real reconstruction technique exists would risk masking a
  regression in the M5.5 baseline behind an untested default. The roadmap already assigns the default
  flip to M6.2.
