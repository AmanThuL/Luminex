// Temporal upscaling (M6.3 spec 7): one dispatch per *output* pixel reconstructs the presented
// image out of a jittered render image smaller than it. It is Shaders/TemporalResolve.slang's
// accumulation with three differences -- the current colour is resampled at the position this
// output pixel's centre was sampled at rather than loaded one-to-one, the previous depth slot is
// addressed at the extent it was rendered at rather than at this frame's, and the blend weight
// carries a sample-proximity term, so an output pixel whose nearest jittered sample fell near its
// own centre trusts this frame more than one that fell between samples. Over a jitter period every
// output pixel receives near-centre samples, which is what lets the accumulation converge on
// detail no single render-extent frame holds.
//
// A separate file rather than a branch in the native kernel, so scale 1 keeps M6.2's compiled
// output exactly. Everything here runs on pre-exposed scene-linear radiance and stays there;
// nothing encodes or tone maps, and the alpha channel of the colour history is the per-pixel
// accumulation age, not coverage.
//
// Slots are the resolve's: textures 0 scene colour, 1 depth, 2 previous depth, 3 motion, 4
// reactive, 5 history; storage textures 6 output, 7 rejection, 8 reprojected; sampler 0 clamped
// linear; buffers 0 exposure pair (read), 1 params. The render-extent inputs (0-4) are read inside
// their origin-anchored active rectangle; the history and all three outputs are at the output
// extent.
import Motion;
import TemporalCommon;

struct TemporalUpscaleParams
{
    // Shaders/TemporalResolve.slang's TemporalResolveParams, field for field and in its order, so
    // the two kernels' blocks stay comparable and the CPU mirror grows by appending only.
    uint width;  // Active render extent, which every render-extent input is read within.
    uint height;
    uint historyValid;      // 0 on a reset frame: steps 2-5, 7 and 8 are skipped and no history is read.
    uint writeDiagnostics;  // Bit 0 writes rejection; bit 1 writes reprojected history.
    // Unjittered, so a reconstructed world position does not carry the frame's sub-pixel offset.
    float4x4 inverseViewProjection;
    float4x4 previousViewProjection;
    // The previous frame's positive near-plane distance. Both sides of the disocclusion comparison
    // are depths in the *previous* frame's clip space, so one near plane inverts both of them.
    float previousNearZ;
    // The resolve's trailing float3, spelled as three floats because fields follow it here: this
    // target lays a struct out naturally, where a float3 takes 16-byte alignment and would push
    // everything appended below off the offsets the CPU mirror computes. Three floats occupy the
    // same twelve bytes and keep the block above a multiple of 16, which is what the pad is for.
    float pad[3];
    // Appended by this kernel.
    uint outputWidth;  // Dispatch bound: one invocation per output pixel.
    uint outputHeight;
    uint allocatedWidth;  // Allocation of every render-extent input, which their UVs are taken over.
    uint allocatedHeight;
    uint previousRenderWidth;  // The extent the previous depth slot was rendered at.
    uint previousRenderHeight;
    // Where the jittered raster placed the scene, in render texels of texture space: the CPU-side
    // jitterTexelOffset(jitter) of Source/Render/Temporal.h.
    float2 jitterOffset;
};

[[vk::binding(0, 0)]]
Texture2D<float4> gSceneColor : register(t0); // → MSL [[texture(0)]]

[[vk::binding(1, 0)]]
Texture2D<float> gDepth : register(t1); // → MSL [[texture(1)]]

[[vk::binding(2, 0)]]
Texture2D<float> gPreviousDepth : register(t2); // → MSL [[texture(2)]]

[[vk::binding(3, 0)]]
Texture2D<float2> gMotion : register(t3); // → MSL [[texture(3)]]

[[vk::binding(4, 0)]]
Texture2D<float> gReactive : register(t4); // → MSL [[texture(4)]]

[[vk::binding(5, 0)]]
Texture2D<float4> gHistory : register(t5); // → MSL [[texture(5)]]

[[vk::binding(6, 0)]]
RWTexture2D<float4> gOutput : register(u6); // → MSL [[texture(6)]]

[[vk::binding(7, 0)]]
RWTexture2D<float4> gRejection : register(u7); // → MSL [[texture(7)]]

[[vk::binding(8, 0)]]
RWTexture2D<float4> gReprojected : register(u8); // → MSL [[texture(8)]]

[[vk::binding(0, 1)]]
SamplerState gLinearSampler : register(s0); // → MSL [[sampler(0)]]

[[vk::binding(0, 0)]]
StructuredBuffer<float> gExposure : register(t0); // → MSL [[buffer(0)]]

[[vk::binding(1, 0)]]
ConstantBuffer<TemporalUpscaleParams> gParams : register(b1); // → MSL [[buffer(1)]]

// Five-tap Catmull-Rom over the output-extent history. Copied from
// Shaders/TemporalResolve.slang's sampleHistoryCatmullRom, which stays the source of truth for the
// weights, the five-tap renormalisation and the negative-lobe clamp: the copy exists because the
// fetch names its own texture, not because the two differ.
float3 sampleHistoryCatmullRom(float2 uv, float2 extent)
{
    float2 samplePosition = uv * extent;
    float2 texelCenter = floor(samplePosition - 0.5) + 0.5;
    float2 f = samplePosition - texelCenter;

    float2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    float2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    float2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    float2 w3 = f * f * (-0.5 + 0.5 * f);

    float2 w12 = w1 + w2;
    float2 offset12 = w2 / w12;

    float2 uv0 = (texelCenter - 1.0) / extent;
    float2 uv3 = (texelCenter + 2.0) / extent;
    float2 uv12 = (texelCenter + offset12) / extent;

    float3 result = gHistory.SampleLevel(gLinearSampler, float2(uv12.x, uv0.y), 0.0).rgb *
                    (w12.x * w0.y);
    result += gHistory.SampleLevel(gLinearSampler, float2(uv0.x, uv12.y), 0.0).rgb * (w0.x * w12.y);
    result += gHistory.SampleLevel(gLinearSampler, uv12, 0.0).rgb * (w12.x * w12.y);
    result += gHistory.SampleLevel(gLinearSampler, float2(uv3.x, uv12.y), 0.0).rgb * (w3.x * w12.y);
    result += gHistory.SampleLevel(gLinearSampler, float2(uv12.x, uv3.y), 0.0).rgb *
              (w12.x * w3.y);
    float weightSum = w12.x * (w0.y + w12.y + w3.y) + (w0.x + w3.x) * w12.y;
    return max(result / weightSum, 0.0);
}

// A tap position in render texels, turned into a UV the sampler may take. Copied from
// Shaders/SpatialUpscale.slang's activeUv: the clamp keeps every tap's bilinear footprint inside
// the active rectangle, and the division is by the *allocated* extent because that is the texture
// the UV addresses.
float2 activeUv(float2 position)
{
    float2 render = float2(float(gParams.width), float(gParams.height));
    float2 allocated = float2(float(gParams.allocatedWidth), float(gParams.allocatedHeight));
    return clamp(position, float2(0.5, 0.5), render - 0.5) / allocated;
}

// Five-tap Catmull-Rom over the render image, at a position in render texels. It is
// Shaders/SpatialUpscale.slang's sampleCurrentCatmullRom exactly, which is what makes the spatial
// reference and this kernel differ by the accumulation alone.
float3 sampleCurrentCatmullRom(float2 samplePosition)
{
    float2 texelCenter = floor(samplePosition - 0.5) + 0.5;
    float2 f = samplePosition - texelCenter;

    float2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    float2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    float2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    float2 w3 = f * f * (-0.5 + 0.5 * f);

    float2 w12 = w1 + w2;
    float2 offset12 = w2 / w12;

    float2 p0 = texelCenter - 1.0;
    float2 p3 = texelCenter + 2.0;
    float2 p12 = texelCenter + offset12;

    float3 result =
        gSceneColor.SampleLevel(gLinearSampler, activeUv(float2(p12.x, p0.y)), 0.0).rgb *
        (w12.x * w0.y);
    result += gSceneColor.SampleLevel(gLinearSampler, activeUv(float2(p0.x, p12.y)), 0.0).rgb *
              (w0.x * w12.y);
    result += gSceneColor.SampleLevel(gLinearSampler, activeUv(p12), 0.0).rgb * (w12.x * w12.y);
    result += gSceneColor.SampleLevel(gLinearSampler, activeUv(float2(p3.x, p12.y)), 0.0).rgb *
              (w3.x * w12.y);
    result += gSceneColor.SampleLevel(gLinearSampler, activeUv(float2(p12.x, p3.y)), 0.0).rgb *
              (w12.x * w3.y);
    float weightSum = w12.x * (w0.y + w12.y + w3.y) + (w0.x + w3.x) * w12.y;
    return max(result / weightSum, 0.0);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void computeTemporalUpscale(uint3 tid: SV_DispatchThreadID)
{
    if (tid.x >= gParams.outputWidth || tid.y >= gParams.outputHeight)
    {
        return;
    }

    float2 render = float2(float(gParams.width), float(gParams.height));
    float2 output = float2(float(gParams.outputWidth), float(gParams.outputHeight));

    // Step 1: sample position. Source/Render/Temporal.h's renderSamplePosition, the nearest render
    // texel to it, and the unjittered UV of this output pixel's own centre.
    float2 samplePos = (float2(tid.xy) + 0.5) * (render / output) + gParams.jitterOffset;
    int2 c = clamp(int2(floor(samplePos)), int2(0, 0), int2(render) - 1);
    float2 uv = (float2(tid.xy) + 0.5) / output;

    // Step 6's fetch, hoisted the way the resolve hoists its own one-to-one load: a reset frame
    // writes this colour and reads no history at all, so it has to exist before that branch.
    float3 current = sampleCurrentCatmullRom(samplePos);
    float reactive = gReactive.Load(int3(c, 0)).r;

    // Step 9's reset case: this frame's picture, with the per-pixel count started over. The pass
    // binds the same textures either way, so nothing below is reached rather than nothing bound.
    if (gParams.historyValid == 0u)
    {
        gOutput[tid.xy] = float4(current, 1.0);
        if ((gParams.writeDiagnostics & 1u) != 0u)
        {
            gRejection[tid.xy] = float4(float(kReasonNone) / 255.0, 1.0, 0.0, 1.0);
        }
        if ((gParams.writeDiagnostics & 2u) != 0u)
        {
            gReprojected[tid.xy] = float4(0.0, 0.0, 0.0, 0.0);
        }
        return;
    }

    uint reason = kReasonNone;

    // Step 2: motion dilation over the 3x3 render texels around the nearest sample, clamped to the
    // active rectangle. The closest-depth texel lends its motion to the whole footprint, so a
    // silhouette reprojects with the foreground it belongs to.
    float2 motion = gMotion.Load(int3(c, 0)).xy;
    float closestDepth = gDepth.Load(int3(c, 0)).r;
    for (int dy = -1; dy <= 1; ++dy)
    {
        for (int dx = -1; dx <= 1; ++dx)
        {
            int2 neighbor = clamp(c + int2(dx, dy), int2(0, 0), int2(render) - 1);
            int3 neighborCoord = int3(neighbor, 0);
            float neighborDepth = gDepth.Load(neighborCoord).r;
            // Reversed depth: the greater value is the nearer surface.
            if (neighborDepth > closestDepth)
            {
                closestDepth = neighborDepth;
                motion = gMotion.Load(neighborCoord).xy;
            }
        }
    }
    if (isinf(motion.x) || isinf(motion.y))
    {
        reason = kReasonInvalid;
        motion = float2(0.0, 0.0);
    }

    // Step 3: history position. Motion is a normalised UV delta, which is what makes it mean the
    // same thing to an output-extent consumer at every render scale.
    float2 historyUv = uv - motion;
    if (reason == kReasonNone && (any(historyUv < 0.0) || any(historyUv > 1.0)))
    {
        reason = kReasonOffScreen;
    }

    // Step 4: disocclusion. The surface this output pixel sees is reconstructed from its own UV and
    // the dilated current depth, transformed into the previous frame's clip space, and compared
    // with the closest of the 3x3 around the history position in the previous depth slot -- which
    // is addressed at the extent that slot was rendered at, not at this frame's. Point-sampling one
    // texel on each side would report every sub-pixel coverage flip of a jittered thin edge as a
    // disocclusion; a surface that really was uncovered has no near depth anywhere in its
    // neighbourhood, so the dilated test still catches it.
    if (reason == kReasonNone && closestDepth > 0.0)
    {
        float3 ndc = float3(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, closestDepth);
        float4 world = mul(gParams.inverseViewProjection, float4(ndc, 1.0));
        float4 previousClip = mul(gParams.previousViewProjection, float4(world.xyz / world.w, 1.0));
        if (previousClip.w > 0.0)
        {
            float expectedDepth = previousClip.z / previousClip.w;
            float2 previousRender = float2(float(gParams.previousRenderWidth),
                                           float(gParams.previousRenderHeight));
            int2 historyTexel =
                clamp(int2(historyUv * previousRender), int2(0, 0), int2(previousRender) - 1);
            float previousDepth = 0.0;
            for (int dy = -1; dy <= 1; ++dy)
            {
                for (int dx = -1; dx <= 1; ++dx)
                {
                    int2 neighbor = clamp(historyTexel + int2(dx, dy), int2(0, 0),
                                          int2(previousRender) - 1);
                    previousDepth = max(previousDepth, gPreviousDepth.Load(int3(neighbor, 0)).r);
                }
            }
            if (expectedDepth > 0.0 && previousDepth > 0.0)
            {
                float expectedDistance = viewDistance(expectedDepth, gParams.previousNearZ);
                float previousDistance = viewDistance(previousDepth, gParams.previousNearZ);
                if (abs(previousDistance - expectedDistance) >
                    kDisocclusionTolerance * expectedDistance)
                {
                    reason = kReasonDisoccluded;
                }
            }
            else if (previousDepth <= 0.0)
            {
                // Nothing the previous frame drew stood where this surface should have been.
                reason = kReasonDisoccluded;
            }
        }
    }

    // Step 5: history fetch and exposure correction, over the output-extent history. The pair is
    // what the GPU actually applied, so the ratio holds across a manual EV edit alike.
    float3 history = current;
    float previousAge = 0.0;
    if (reason == kReasonNone)
    {
        history = sampleHistoryCatmullRom(historyUv, output);
        float applied = gExposure[0];
        float previous = gExposure[1];
        history *= previous > 0.0 ? applied / previous : 1.0;
        previousAge = floor(gHistory.SampleLevel(gLinearSampler, historyUv, 0.0).a);
    }

    if ((gParams.writeDiagnostics & 2u) != 0u)
    {
        gReprojected[tid.xy] = float4(history, reason == kReasonNone ? 1.0 : 0.0);
    }

    // Step 7: neighbourhood clipping against the mean and standard deviation of the 3x3 render
    // texels around the nearest sample -- the texels this frame actually shaded, at the extent it
    // shaded them.
    bool clipped = false;
    if (reason == kReasonNone)
    {
        float3 sum = float3(0.0, 0.0, 0.0);
        float3 sumSquares = float3(0.0, 0.0, 0.0);
        for (int dy = -1; dy <= 1; ++dy)
        {
            for (int dx = -1; dx <= 1; ++dx)
            {
                int2 neighbor = clamp(c + int2(dx, dy), int2(0, 0), int2(render) - 1);
                float3 sample = rgbToYcocg(gSceneColor.Load(int3(neighbor, 0)).rgb);
                sum += sample;
                sumSquares += sample * sample;
            }
        }
        float3 mean = sum / 9.0;
        float3 deviation = sqrt(max(sumSquares / 9.0 - mean * mean, 0.0));
        float3 minimum = mean - kClipGamma * deviation;
        float3 maximum = mean + kClipGamma * deviation;

        float3 historyYcocg = rgbToYcocg(history);
        float3 clippedYcocg =
            clipTowardCurrent(historyYcocg, rgbToYcocg(current), minimum, maximum);
        float3 clippedRgb = ycocgToRgb(clippedYcocg);
        clipped = any(abs(clippedRgb - history) > 1e-4);
        history = clippedRgb;
    }

    // Step 8: blend weight. A fast-moving pixel trusts its history less; then the sample-proximity
    // term scales that trust in this frame down toward kUpscaleMinWeight as the jittered sample
    // falls further from this output pixel's own centre; a reactive pixel trusts its history less
    // still, and a rejected one not at all.
    float motionPixels = length(motion * output);
    float alpha = lerp(kBaseAlpha, kMaxMotionAlpha, saturate(motionPixels / kMotionAlphaPixels));
    float sampleDistance = length(samplePos - (float2(c) + 0.5));
    alpha *= lerp(kUpscaleMinWeight, 1.0,
                  1.0 - saturate(sampleDistance / kUpscaleSampleRadius));
    alpha = max(alpha, reactive);
    if (reason != kReasonNone)
    {
        alpha = 1.0;
    }
    else if (alpha >= 1.0)
    {
        reason = kReasonReactive;
    }

    // Inverse-luminance weighting: without it a bright sample dominates the average and a
    // temporally unstable highlight keeps flickering through the accumulation.
    float weightCurrent = alpha / (1.0 + luminance(current));
    float weightHistory = (1.0 - alpha) / (1.0 + luminance(history));
    float weightSum = max(weightCurrent + weightHistory, 1e-7);
    float3 resolved = (current * weightCurrent + history * weightHistory) / weightSum;

    // Step 9: the age this pixel has accumulated, which is what the HistoryAge view reads back.
    float age = reason != kReasonNone ? 1.0 : min(previousAge + 1.0, kMaxHistoryAge);
    gOutput[tid.xy] = float4(resolved, age);

    if ((gParams.writeDiagnostics & 1u) != 0u)
    {
        gRejection[tid.xy] = float4(float(reason | (clipped ? kClippedBit : 0u)) / 255.0, alpha,
                                    clipped ? 1.0 : 0.0, 1.0);
    }
}
