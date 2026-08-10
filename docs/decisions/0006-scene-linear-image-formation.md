# ADR 0006: Scene-linear image formation

**Status**: Accepted (2026-08-10) · **Roadmap**: ../roadmap.md (M4)

## Context
M3 shaded and encoded to sRGB in the same fragment, clamped to [0, 1] with no exposure control — a
ceiling that made an energy-conserving material model pointless, since anything above 1.0 was
thrown away before it could be seen.

## Decision
Scene and sky render into an `RGBA16Float` target holding pre-exposed scene-linear radiance: every
fragment multiplies its linear output by `preExposure = exp2(EV)` (a manual slider, default 0)
before the target sees it, and nothing upstream of the display pass encodes sRGB. The editor's
clear color, authored in display space, is decoded to linear and pre-exposed once at pass
declaration, so background and shaded geometry agree on what space the target holds — M3's
raw-clear exception disappears. A dedicated fullscreen pass (`Shaders/DisplayTransform.slang`) is
the frame's one display boundary: the Khronos PBR Neutral tone map, then the sRGB encode, into an
8-bit SDR target, replaceable on its own. Depth is reversed and infinite-far (near maps to 1, far
falls toward 0 without reaching it, cleared to 0, `Greater`/`GreaterEqual` compares), concentrating
float precision at the far plane instead of the near one.

## Consequences
A later HDR-to-SDR technique replaces one pass rather than touching shading. Automatic exposure,
bloom, and temporal reconstruction stay deferred to M5/M6. Every reversed-Z consumer (the shadow
ortho fit, the sky's far-plane pin, PCSS) must agree on the convention; PCSS's preserved view/NDC
unit mismatch from M3 remains unfixed — only the pipeline-level shadow depth bias
(`kShadowDepthBias`, shared by PCF and PCSS) had its sign re-tuned to match the reversal.
