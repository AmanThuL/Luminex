// The project's one tone map, kept as a module because two pipelines need it: the display
// transform, which is the frame's only step out of scene-linear space, and the temporal debug view
// that shows a colour signal, which has to reach the display target through exactly the transform
// the picture beside it did or the two could not be compared.
module Tonemap;

// Khronos PBR Neutral: a display-referred tone map that leaves in-gamut colour alone and rolls
// only what exceeds the display into range. Below the compression start it is the identity minus
// a small black offset, so mid-tones keep the value the shading produced; past it the peak channel
// is compressed and the colour desaturates toward that peak instead of clipping a channel.
static const float kStartCompression = 0.8 - 0.04;
static const float kDesaturation = 0.15;

public float3 pbrNeutralToneMap(float3 color)
{
    float x = min(color.r, min(color.g, color.b));
    float offset = (x < 0.08) ? (x - 6.25 * x * x) : 0.04;
    color -= offset;

    float peak = max(color.r, max(color.g, color.b));
    if (peak < kStartCompression)
    {
        return color;
    }

    const float d = 1.0 - kStartCompression;
    float newPeak = 1.0 - d * d / (peak + d - kStartCompression);
    color *= newPeak / peak;

    float g = 1.0 - 1.0 / (kDesaturation * (peak - newPeak) + 1.0);
    return lerp(color, newPeak.xxx, g);
}
