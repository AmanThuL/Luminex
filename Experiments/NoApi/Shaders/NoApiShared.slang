// Shared math for the M5.1 representative graph's distilled P05-P12 kernels
// (docs/specs/2026-08-12-m5.1-rhi-execution-model-design.md section 6): the exposure histogram
// bin/resolve formulas, the bloom box filters, and the composite tone map, factored out of any one
// pass's binding surface so both encoders' frontends -- this file's thin per-pass wrappers today,
// and the prototype's address-first wrappers later (spec section 2) -- apply byte-identical math
// over whatever resource model each side binds through. Owns no resources or entry points, on
// Shaders/Lighting.slang's pattern.
//
// The histogram/resolve formulas mirror Shaders/HistogramAccumulate.slang and
// Shaders/ExposureResolve.slang; the bloom filters mirror Shaders/BloomDownsample.slang and
// Shaders/BloomUpsample.slang; the tone map mirrors Shaders/DisplayTransform.slang's own
// pbrNeutralToneMap, and the sRGB encode below mirrors Shaders/Encode.slang's module. None of the
// first four export a module a distilled kernel could import at all, and slangc resolves `import`
// against the compiled file's own directory -- these frontends compile from
// Experiments/NoApi/Shaders, not Shaders -- so even Encode's own module is out of reach across that
// directory boundary. Every formula is restated here instead, kept deliberately line-for-line
// identical to its production counterpart.
module NoApiShared;

// Rec. 709 luma weights, matching HistogramAccumulate.slang/BloomThreshold.slang.
public static const float3 kLuminanceWeights = float3(0.2126, 0.7152, 0.0722);

//======================================================================================================================
// Recovers scene-referred luminance from a pre-exposed display sample, mirroring
// HistogramAccumulate.slang's own division by the frame's applied preExposure.
public float SceneReferredLuminance(float3 displayedColor, float preExposure)
{
    float displayedLuminance = dot(displayedColor, kLuminanceWeights);
    return displayedLuminance / max(preExposure, 1e-6);
}

//======================================================================================================================
public float LogLuminance(float sceneLuminance)
{
    return log2(max(sceneLuminance, 1e-6));
}

//======================================================================================================================
public uint HistogramBin(float logLuminance, float logLuminanceMin, float logLuminanceMax, uint bins)
{
    float range = max(logLuminanceMax - logLuminanceMin, 1e-6);
    float t = saturate((logLuminance - logLuminanceMin) / range);
    return min(uint(t * float(bins)), bins - 1u);
}

//======================================================================================================================
public float BinCenterLogLuminance(uint bin, float logLuminanceMin, float logLuminanceMax, uint bins)
{
    float binWidth = (logLuminanceMax - logLuminanceMin) / float(bins);
    return logLuminanceMin + (float(bin) + 0.5) * binWidth;
}

//======================================================================================================================
// Mirrors BloomThreshold.slang's per-texel formula; the caller supplies the already 2x2-box-read
// colour.
public float3 BloomThresholdContribution(float3 color, float threshold)
{
    float luminance = dot(color, kLuminanceWeights);
    float contribution = max(luminance - threshold, 0.0) / max(luminance, 1e-4);
    return color * contribution;
}

//======================================================================================================================
// Mirrors BloomDownsample.slang's 2x2 box filter.
public float4 BoxFilter4(float4 a, float4 b, float4 c, float4 d)
{
    return (a + b + c + d) * 0.25;
}

//======================================================================================================================
// Mirrors BloomUpsample.slang's accumulate step: a base mip plus an already-upsampled small mip.
public float4 UpsampleAccumulate(float4 base, float4 smallSample)
{
    return base + smallSample;
}

//======================================================================================================================
// Mirrors Shaders/Encode.slang's linearToSrgb, restated here rather than imported cross-directory
// (slangc resolves `import` against the compiled file's own directory, and this module's frontends
// compile from Experiments/NoApi/Shaders, not Shaders) so Composite.slang's binding frontend can
// still reuse one shared encode step alongside its NoApiShared-hosted tone map.
public float LinearToSrgbChannel(float c)
{
    c = saturate(c);
    return c <= 0.0031308 ? 12.92 * c : 1.055 * pow(c, 1.0 / 2.4) - 0.055;
}

public float3 LinearToSrgb(float3 c)
{
    return float3(LinearToSrgbChannel(c.r), LinearToSrgbChannel(c.g), LinearToSrgbChannel(c.b));
}

//======================================================================================================================
// Mirrors DisplayTransform.slang's pbrNeutralToneMap (Khronos PBR Neutral) verbatim.
public float3 PbrNeutralToneMap(float3 color)
{
    static const float kStartCompression = 0.8 - 0.04;
    static const float kDesaturation = 0.15;

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
