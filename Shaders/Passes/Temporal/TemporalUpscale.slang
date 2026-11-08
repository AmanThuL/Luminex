// Temporal upscaling (M6.3 spec 7): one dispatch per *output* pixel reconstructs the presented
// image out of a jittered render image smaller than it. It is
// Shaders/Passes/Temporal/TemporalResolve.slang's accumulation with three differences -- the
// current colour is resampled at the position this output pixel's centre was sampled at rather than
// loaded one-to-one, the previous depth slot is addressed at the extent it was rendered at rather
// than at this frame's, and the blend weight carries a sample-proximity term, so an output pixel
// whose nearest jittered sample fell near its own centre trusts this frame more than one that fell
// between samples. Over a jitter period every output pixel receives near-centre samples, which is
// what lets the accumulation converge on detail no single render-extent frame holds.
//
// A separate file rather than a branch in the native kernel, so scale 1 keeps M6.2's compiled
// output exactly. Everything here runs on pre-exposed scene-linear radiance and stays there;
// nothing encodes or tone maps, and the alpha channel of the colour history is the per-pixel
// accumulation age, not coverage.
//
// The address root is the resolve's TemporalResolveRoot: parameters, the persistent exposure pair
// (read), the scene colour, depth, previous depth, motion, reactive and history views, the storage
// views of the output and the two diagnostics, and the clamped linear sampler. The render-extent
// inputs are read inside their origin-anchored active rectangle; the history and all three
// outputs are at the output extent.
import RojoRHI;
import Motion;
import TemporalCommon;
import TemporalRoots;

// Five-tap Catmull-Rom over the output-extent history. Copied from
// Shaders/Passes/Temporal/TemporalResolve.slang's sampleHistoryCatmullRom, which stays the source
// of truth for the weights, the five-tap renormalisation and the negative-lobe clamp: the copy
// exists because the fetch names its own texture, not because the two differ.
float3 sampleHistoryCatmullRom(Texture2D<float4> historyTexture, SamplerState linearSampler,
                               float2 uv, float2 extent)
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

    float3 result = historyTexture.SampleLevel(linearSampler, float2(uv12.x, uv0.y), 0.0).rgb *
                    (w12.x * w0.y);
    result += historyTexture.SampleLevel(linearSampler, float2(uv0.x, uv12.y), 0.0).rgb *
              (w0.x * w12.y);
    result += historyTexture.SampleLevel(linearSampler, uv12, 0.0).rgb * (w12.x * w12.y);
    result += historyTexture.SampleLevel(linearSampler, float2(uv3.x, uv12.y), 0.0).rgb *
              (w3.x * w12.y);
    result += historyTexture.SampleLevel(linearSampler, float2(uv12.x, uv3.y), 0.0).rgb *
              (w12.x * w3.y);
    float weightSum = w12.x * (w0.y + w12.y + w3.y) + (w0.x + w3.x) * w12.y;
    return max(result / weightSum, 0.0);
}

// A tap position in render texels, turned into a UV the sampler may take. Copied from
// Shaders/Passes/Temporal/SpatialUpscale.slang's activeUv: the clamp keeps every tap's bilinear
// footprint inside the active rectangle, and the division is by the *allocated* extent because that
// is the texture the UV addresses.
float2 activeUv(TemporalUpscaleParams params, float2 position)
{
    float2 render = float2(float(params.width), float(params.height));
    float2 allocated = float2(float(params.allocatedWidth), float(params.allocatedHeight));
    return clamp(position, float2(0.5, 0.5), render - 0.5) / allocated;
}

// Five-tap Catmull-Rom over the render image, at a position in render texels. It is
// Shaders/Passes/Temporal/SpatialUpscale.slang's sampleCurrentCatmullRom exactly, which is what
// makes the spatial reference and this kernel differ by the accumulation alone.
float3 sampleCurrentCatmullRom(TemporalUpscaleParams params, Texture2D<float4> sceneColorTexture,
                               SamplerState linearSampler, float2 samplePosition)
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

    float3 result = sceneColorTexture
                        .SampleLevel(linearSampler, activeUv(params, float2(p12.x, p0.y)), 0.0)
                        .rgb *
                    (w12.x * w0.y);
    result += sceneColorTexture
                  .SampleLevel(linearSampler, activeUv(params, float2(p0.x, p12.y)), 0.0)
                  .rgb *
              (w0.x * w12.y);
    result += sceneColorTexture.SampleLevel(linearSampler, activeUv(params, p12), 0.0).rgb *
              (w12.x * w12.y);
    result += sceneColorTexture
                  .SampleLevel(linearSampler, activeUv(params, float2(p3.x, p12.y)), 0.0)
                  .rgb *
              (w3.x * w12.y);
    result += sceneColorTexture
                  .SampleLevel(linearSampler, activeUv(params, float2(p12.x, p3.y)), 0.0)
                  .rgb *
              (w12.x * w3.y);
    float weightSum = w12.x * (w0.y + w12.y + w3.y) + (w0.x + w3.x) * w12.y;
    return max(result / weightSum, 0.0);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void computeTemporalUpscale(uint3 tid: SV_DispatchThreadID)
{
    let root = rojoRHI::root<TemporalResolveRoot>();
    let params = rojoRHI::load<TemporalUpscaleParams>(root.paramsAddr, 0);
    let sceneColorTexture = rojoRHI::texture2D<float4>(root.sceneColor);
    let depthTexture = rojoRHI::texture2D<float>(root.depth);
    let previousDepthTexture = rojoRHI::texture2D<float>(root.previousDepth);
    let motionTexture = rojoRHI::texture2D<float2>(root.motion);
    let reactiveTexture = rojoRHI::texture2D<float>(root.reactive);
    let historyTexture = rojoRHI::texture2D<float4>(root.history);
    let outputTexture = rojoRHI::rwTexture2D<float4>(root.output);
    let rejectionTexture = rojoRHI::rwTexture2D<float4>(root.rejection);
    let reprojectedTexture = rojoRHI::rwTexture2D<float4>(root.reprojected);
    let linearSampler = rojoRHI::sampler(root.sampler);
    if (tid.x >= params.outputWidth || tid.y >= params.outputHeight)
    {
        return;
    }

    float2 render = float2(float(params.width), float(params.height));
    float2 output = float2(float(params.outputWidth), float(params.outputHeight));

    // Step 1: sample position. Source/Render/Temporal.h's renderSamplePosition, the nearest render
    // texel to it, and the unjittered UV of this output pixel's own centre.
    float2 samplePos = (float2(tid.xy) + 0.5) * (render / output) + params.jitterOffset;
    int2 c = clamp(int2(floor(samplePos)), int2(0, 0), int2(render) - 1);
    float2 uv = (float2(tid.xy) + 0.5) / output;

    // Step 6's fetch, hoisted the way the resolve hoists its own one-to-one load: a reset frame
    // writes this colour and reads no history at all, so it has to exist before that branch.
    float3 current =
        sampleCurrentCatmullRom(params, sceneColorTexture, linearSampler, samplePos);
    float reactive = reactiveTexture.Load(int3(c, 0)).r;

    // Step 9's reset case: this frame's picture, with the per-pixel count started over. The pass
    // binds the same textures either way, so nothing below is reached rather than nothing bound.
    if (params.historyValid == 0u)
    {
        outputTexture[tid.xy] = float4(current, 1.0);
        if ((params.writeDiagnostics & 1u) != 0u)
        {
            rejectionTexture[tid.xy] = float4(float(kReasonNone) / 255.0, 1.0, 0.0, 1.0);
        }
        if ((params.writeDiagnostics & 2u) != 0u)
        {
            reprojectedTexture[tid.xy] = float4(0.0, 0.0, 0.0, 0.0);
        }
        return;
    }

    uint reason = kReasonNone;

    // Step 2: motion dilation over the 3x3 render texels around the nearest sample, clamped to the
    // active rectangle. The closest-depth texel lends its motion to the whole footprint, so a
    // silhouette reprojects with the foreground it belongs to.
    float2 motion = motionTexture.Load(int3(c, 0)).xy;
    float closestDepth = depthTexture.Load(int3(c, 0)).r;
    for (int dy = -1; dy <= 1; ++dy)
    {
        for (int dx = -1; dx <= 1; ++dx)
        {
            int2 neighbor = clamp(c + int2(dx, dy), int2(0, 0), int2(render) - 1);
            int3 neighborCoord = int3(neighbor, 0);
            float neighborDepth = depthTexture.Load(neighborCoord).r;
            // Reversed depth: the greater value is the nearer surface.
            if (neighborDepth > closestDepth)
            {
                closestDepth = neighborDepth;
                motion = motionTexture.Load(neighborCoord).xy;
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
        float4 world = mul(params.inverseViewProjection, float4(ndc, 1.0));
        float4 previousClip = mul(params.previousViewProjection, float4(world.xyz / world.w, 1.0));
        if (previousClip.w > 0.0)
        {
            float expectedDepth = previousClip.z / previousClip.w;
            float2 previousRender = float2(float(params.previousRenderWidth),
                                           float(params.previousRenderHeight));
            int2 historyTexel =
                clamp(int2(historyUv * previousRender), int2(0, 0), int2(previousRender) - 1);
            float previousDepth = 0.0;
            for (int dy = -1; dy <= 1; ++dy)
            {
                for (int dx = -1; dx <= 1; ++dx)
                {
                    int2 neighbor = clamp(historyTexel + int2(dx, dy), int2(0, 0),
                                          int2(previousRender) - 1);
                    previousDepth =
                        max(previousDepth, previousDepthTexture.Load(int3(neighbor, 0)).r);
                }
            }
            if (expectedDepth > 0.0 && previousDepth > 0.0)
            {
                float expectedDistance = viewDistance(expectedDepth, params.previousNearZ);
                float previousDistance = viewDistance(previousDepth, params.previousNearZ);
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
        history = sampleHistoryCatmullRom(historyTexture, linearSampler, historyUv, output);
        float applied = rojoRHI::load<float>(root.exposureAddr, 0);
        float previous = rojoRHI::load<float>(root.exposureAddr, 1);
        history *= previous > 0.0 ? applied / previous : 1.0;
        previousAge = floor(historyTexture.SampleLevel(linearSampler, historyUv, 0.0).a);
    }

    if ((params.writeDiagnostics & 2u) != 0u)
    {
        reprojectedTexture[tid.xy] = float4(history, reason == kReasonNone ? 1.0 : 0.0);
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
                float3 sample = rgbToYcocg(sceneColorTexture.Load(int3(neighbor, 0)).rgb);
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
    outputTexture[tid.xy] = float4(resolved, age);

    if ((params.writeDiagnostics & 1u) != 0u)
    {
        rejectionTexture[tid.xy] = float4(float(reason | (clipped ? kClippedBit : 0u)) / 255.0,
                                          alpha, clipped ? 1.0 : 0.0, 1.0);
    }
}
