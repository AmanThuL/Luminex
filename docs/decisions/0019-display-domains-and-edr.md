# ADR 0019 — Display domains and EDR evaluation

**Status**: Proposed

## Context

The renderer already ends scene-linear image formation with Khronos PBR Neutral and an sRGB
encode into an opaque BGRA8Unorm target. UI composition and capture previously described that
boundary only implicitly. The explicit contract must preserve the accepted scene, exposure and
temporal semantics in ADRs 0006 and 0013–0017.

An isolated macOS probe evaluates extended-linear presentation on the development machine's
built-in Liquid Retina XDR display. It is evidence for a future display choice, not production
presentation code. The [design spec](../specs/2026-09-12-m6.5-display-boundary-edr-design.md)
and [execution plan](../plans/2026-09-12-m6.5-display-boundary-edr.md) define the pending gate.

## Verified SDR contract

`Render/DisplayDomain.h` owns the output view, transfer, primaries, tone map, white levels, bit
depth and opaque-alpha fact. `Renderer::displayDomain()` exposes SDR, sRGB, BT.709/D65, PBR
Neutral, reference white 1, peak white 1, eight bits and opaque alpha. No shader changes accompany
this description. Diagnostics retain their selected debug-view identity in frame metadata.

The main UI blends straight alpha in encoded sRGB on BGRA8Unorm. UI colors are display-referred;
encoded 1 is SDR white. Fonts rasterize at the per-window framebuffer scale. The scene image maps
1:1 to backing pixels once resize debounce settles; the prior target stretches during debounce.
Detached ImGui platform windows have independent SDR layers and remain outside the RHI swapchain.

PNG writes sRGB, gAMA, cHRM, `lmx:display` and `lmx:frame` metadata without changing pixel values.
Screenshots choose PNG or BMP by extension. Sequences default to PNG and permit explicit BMP;
manifest v2 records the display object, container and absence of composited UI. The comparison
tool retains v1 compatibility, loads named frame files and rejects duplicate file references.
BMP encoding is unchanged.

## Required policy for a future extended-range view

- Reference white is the display's SDR white, expressed as relative 1.0; it is not a fixed nit target.
- Peak follows the current available headroom, with bounded adaptation and no overshoot above it.
  Potential headroom alone is insufficient. The probe caps at four reference whites, rises with a
  0.5-second time constant and clamps downward immediately; these probe constants are not a
  production setting or adopted universal tuning.
- UI never exceeds SDR reference white. Detached windows remain SDR.
- A display view consumes scene-linear, pre-exposed and temporally reconstructed content without
  redefining exposure, motion, temporal resets or history ownership.
- Headroom at one or failed layer configuration must produce an explicitly named SDR fallback.
  A float layer limited to one is only a range-fallback test, not proof of complete presentation
  fallback or recreation behavior.
- Every capture names its view and transfer. An SDR operating-system screenshot does not establish
  physical extended-range luminance or preservation of highlights above reference white.

## Probe evidence and limits

Apple M3 Max, macOS 26.5.2, Xcode 26.6, built-in Liquid Retina XDR. Final windowed SDR and EDR
runs each present 360 frames with zero skips and Metal API validation enabled. After startup,
SDL and NSScreen current headroom agree at approximately 11.8033; potential headroom is 16,
reference headroom reports zero. The selected EDR peak approaches four.

At startup NSScreen current headroom can be one while SDL reports 16. SDL 3.4.12's
[upstream Cocoa implementation](https://github.com/libsdl-org/SDL/blob/release-3.4.12/src/video/cocoa/SDL_cocoamodes.m)
substitutes potential headroom when current is at most one. The final probe uses NSScreen current
headroom for peak and keeps SDL as an observation. Earlier smoke results using SDL for peak are
retained but do not validate this final policy.

The proposed `peak * PbrNeutral(input / peak)` formula changes midtones because Neutral's black
offset changes with the scaled input. It disproves the spec's claimed identical midtones: the existing CPU oracle gives
0.18 → 0.1400000 under the SDR curve but 0.0506250 at peak four (−63.84%); 0.5 → 0.4600000
but 0.3400000 (−26.09%). These are display-linear outputs, before encoding.
The experiment leaves scene/exposure/temporal code unchanged; this is a display-curve limitation.
Both experiment intermediates are float, including the SDR reference. Ten per-pass timing samples
per mode describe that experiment and do not establish production SDR overhead or a speedup.

The owner observed the paired windows and reported clearly brighter EDR highlights and similar
white UI text brightness on both sides. After the requested brightness/preset exercise, the
owner reported that the patches remained distinguishable and UI became brighter; whether this
meant both windows followed system brightness or EDR alone boosted UI white awaits clarification.
No per-preset value table, calibrated measurement or photograph is claimed. The probe's
forced-headroom control tests range fallback, not operating-system drawable refusal. Its display
ramps are synthetic probe patterns, distinct from the production golden fixtures. Raw float
readbacks exclude UI. No second display is required or presumed tested.

The final smoke retained ten samples per pass and mode (milliseconds, median [minimum, maximum]):

| Mode | Display | UI |
|---|---|---|
| SDR experiment reference | 0.093208 [0.007042, 0.127250] | 0.289479 [0.286167, 0.324625] |
| EDR experiment | 0.119417 [0.009250, 0.136667] | 0.324917 [0.319250, 0.799792] |

At 2560 × 1440, one drawable's raw payload is 14,745,600 bytes in SDR versus 29,491,200 in
EDR, excluding drawable count and driver allocations. Both experiment display intermediates are
10,980,608 bytes, so these observations do not establish a production memory delta. Frame-64
EDR readback has values above one during peak ramp-up (maximum 1.3671875) and a white-one patch;
SDR's float intermediate white patch is 0.99951171875, quantizing to 255 on its BGRA8 drawable. A
later stable manual readback reaches 3.998046875 with 3,384,639 RGB channels above one. Enabling
the forced-headroom-one control yields maximum 1.0 and zero channels above one, then returns to
peak four when cleared. This verifies the range control only; metadata still names a linear EDR
view and the layer remains float. The control does not establish production SDR fallback.

## Proposed decision: defer adoption

Keep production SDR-only. Physical highlight extension works on this display, while final UI-white clarification remains
pending. In addition, the specified curve materially darkens midtones and the probe does not establish a production
SDR cost baseline or full presentation fallback. Defer until a separately scoped display curve
preserves the intended midtone appearance, with a measured production SDR comparison and complete
layer/fallback/capture behavior. Do not silently change this formula or upstream exposure to hide
the observed deficiency. Record the final decision after bounded observation;
any adoption is a separate later slice and supplies its own presentation interface/capability
contract and full fallback/capture validation. No experimental source merges into production.

Experiment source is preserved by immutable tag `m6.5-edr-evidence`; the independent source
archive SHA-256 is `d44224edeb81b380c84349105b652d33ee5a7375dac94fb86cab61ac82ad830d`.
The retained `Tools/EdrProbe/imgui-linear.patch` reproduces the private backend changes against
the pinned maintained ImGui copy, without committing ThirdParty contents.

The [milestone evidence](../milestones/m6.5.md) records automated output checks and their limits.
