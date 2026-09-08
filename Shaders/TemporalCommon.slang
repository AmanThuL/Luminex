// The accumulation vocabulary the temporal kernels share: the frozen constants, the rejection
// reason codes, and the small colour and depth helpers the neighbourhood clip and the disocclusion
// test are written in. Shaders/TemporalResolve.slang is the source of truth for the algorithm
// these pieces belong to; this module exists so the native and the upscaling kernel cannot drift
// apart on a constant or a colour space.
//
// The five-tap Catmull-Rom fetch is deliberately *not* here: each kernel's fetch addresses its own
// texture -- the output-extent history in one case, the active rectangle of a render-extent image
// in the other -- so the two carry their own copies, naming TemporalResolve.slang as the source of
// truth for the weights.
module TemporalCommon;

// Why a pixel could not reuse its history, in the order the algorithm tests them: the first that
// applies wins. Mirrored by Source/Render/TemporalResolve.h for the debug view's legend and the
// tests' readback, and stored as R = code / 255 in the rejection transient.
public static const uint kReasonNone = 0u;
public static const uint kReasonOffScreen = 1u;
public static const uint kReasonInvalid = 2u;
public static const uint kReasonDisoccluded = 3u;
public static const uint kReasonReactive = 4u;
// Not a rejection: the neighbourhood clip moved the history, which is worth seeing beside the
// reason that did or did not reject it.
public static const uint kClippedBit = 0x80u;

// The accumulation constants, frozen by M6.2 spec 3 and mirrored by Source/Render/TemporalResolve.h.
public static const float kBaseAlpha = 0.1;
public static const float kMaxMotionAlpha = 0.25;
public static const float kMotionAlphaPixels = 8.0;
public static const float kClipGamma = 1.0;
public static const float kDisocclusionTolerance = 0.05;

// The upscaling sample-proximity constants, frozen by M6.3 spec 7 and mirrored by
// Source/Render/TemporalResolve.h.
public static const float kUpscaleSampleRadius = 1.0;
public static const float kUpscaleMinWeight = 0.25;

public static const float3 kLuminanceWeights = float3(0.2126, 0.7152, 0.0722);
// The largest age the alpha channel carries; the count saturates rather than wrapping.
public static const float kMaxHistoryAge = 65535.0;

public float luminance(float3 color)
{
    return dot(color, kLuminanceWeights);
}

// YCoCg is the space the neighbourhood box is built in: it separates the luminance a temporal
// artefact shows up in from the chroma, so a box tight enough to reject a ghost does not also
// desaturate the colour that is still valid.
public float3 rgbToYcocg(float3 c)
{
    return float3(0.25 * c.r + 0.5 * c.g + 0.25 * c.b, 0.5 * (c.r - c.b),
                  -0.25 * c.r + 0.5 * c.g - 0.25 * c.b);
}

public float3 ycocgToRgb(float3 c)
{
    float t = c.x - c.z;
    return float3(t + c.y, c.x + c.z, t - c.y);
}

// Reversed infinite-far depth inverts to a positive view distance through the near plane. A
// cleared or sky texel holds 0, which has no finite distance; the caller keeps it out of the
// comparison rather than letting the guard decide what "very far" means.
public float viewDistance(float depth, float nearZ)
{
    return nearZ / max(depth, 1e-7);
}

// Moves `history` along the segment toward `current` until it lies inside [minimum, maximum].
// Clipping toward the colour this frame shaded -- rather than clamping per channel -- is what
// keeps the result on the line between the two, so a rejected chroma cannot invent a hue neither
// frame held.
public float3 clipTowardCurrent(float3 history, float3 current, float3 minimum, float3 maximum)
{
    float3 direction = history - current;
    float t = 1.0;
    for (uint channel = 0; channel < 3; ++channel)
    {
        float d = direction[channel];
        if (d > 1e-7)
        {
            t = min(t, (maximum[channel] - current[channel]) / d);
        }
        else if (d < -1e-7)
        {
            t = min(t, (minimum[channel] - current[channel]) / d);
        }
    }
    return current + direction * saturate(t);
}
