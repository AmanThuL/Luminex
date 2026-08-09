# Low Shadow Contrast

**Status**: Closed (2026-08-09)

## Symptom

Cast shadows appeared absent in the default scene and Sponza even though the shadow pass completed.
DamagedHelmet also showed no ground shadow because that scene has no receiving ground geometry.

## Evidence

A labeled frame capture contained a populated shadow map with the expected car and cylinder
silhouettes. Decoded matrices were finite, reprojection placed the shadow at the same floor pixels,
and the measured shadow-to-lit display ratio was about 0.91: only nine percent darkening. That ruled
out a cleared map, stale transform, missing texture binding, and an incorrect filter selection.

The launch lighting used three equal unshadowed/shadowed lights, while only the first light received
the shadow term. Ambient was also passed as an authored-looking value without the project-standard
sRGB-to-linear conversion, making it roughly five times brighter than the intended linear energy.
Together, fill and ambient erased most visible contrast.

## Root cause

This was a lighting-balance and color-boundary defect, not a shadow-map defect. The only shadowed
light was not a dominant key, and ambient bypassed the scene-build color conversion used by other
authored values. The launch camera further hid part of the default scene's shadow beneath the car.

## Correction

- Ambient became Engine-owned scene data and is decoded once at scene construction.
- The authored light strengths became a dominant key with two weak fills: `{0.7, 0.2, 0.2}`, each
  decoded to linear space.
- The shader continues to shadow only the key light; broadening the shadow term was not required.

The predicted display-space ratio after both corrections is about 0.54, or 46 percent darkening.
Fixed-camera output confirmed that the floor lobe and cylinder streak read as shadows.

## Prevention

- State authored-versus-linear color at the Engine/Render boundary and test the conversion.
- Give diagnostic captures access to the same scene and render settings as interactive runs.
- Diagnose from the earliest incorrect render target; a healthy shadow map redirects investigation
  toward projection, sampling, lighting balance, or composition.
- Keep a receiving surface in any scene used as a cast-shadow acceptance case.

