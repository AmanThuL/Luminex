// Native-resolution temporal accumulation (M6.2 spec 6). One dispatch per output pixel reprojects
// the previous frame's accumulated colour through this frame's motion, decides whether that history
// describes the same surface, corrects it for the exposure it was recorded at, clips it to the
// neighbourhood this frame actually shaded, and blends. The result is written straight into this
// frame's colour history slot: there is no copy, because this frame's output *is* the next frame's
// history.
//
// Everything here runs on pre-exposed scene-linear radiance and stays there; nothing in this file
// encodes or tone maps. The alpha channel of the colour history is not coverage but a per-pixel
// accumulation age, which the debug views read and the CPU count in TemporalStatus mirrors.
//
// The address root carries this dispatch's parameters, the persistent exposure pair (read), the
// scene colour, depth, previous depth, motion, reactive and history views, the storage views of
// the output and the two diagnostics, and the clamped linear sampler.
//
// The frozen constants, the rejection reason codes and the colour and depth helpers live in
// Shaders/Passes/Temporal/TemporalCommon.slang, which Shaders/Passes/Temporal/TemporalUpscale.slang
// shares with this file; the algorithm they belong to is this one's.
import RojoRHI;
import Motion;
import TemporalCommon;
import TemporalRoots;

// Five-tap Catmull-Rom: a sharper history fetch than bilinear, which is what stops an accumulated
// image from softening a little more every frame it is reprojected.
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
    // Removing the four corner taps also removes their positive weight. Renormalize the five
    // remaining taps so a constant radiance stays constant at every fractional history position.
    float weightSum = w12.x * (w0.y + w12.y + w3.y) + (w0.x + w3.x) * w12.y;
    // The five taps carry a small negative lobe, which can push a high-contrast edge below zero.
    return max(result / weightSum, 0.0);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void computeTemporalResolve(uint3 tid: SV_DispatchThreadID)
{
    let root = rojoRHI::root<TemporalResolveRoot>();
    let params = rojoRHI::load<TemporalResolveParams>(root.paramsAddr, 0);
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
    if (tid.x >= params.width || tid.y >= params.height)
    {
        return;
    }

    int3 coord = int3(int2(tid.xy), 0);
    float2 extent = float2(float(params.width), float(params.height));
    float3 current = sceneColorTexture.Load(coord).rgb;
    float reactive = reactiveTexture.Load(coord).r;

    // Step 8: a reset frame has no history that describes this scene. It writes what it shaded and
    // starts the per-pixel count over; the pass binds the same textures either way, so nothing
    // below is reached rather than nothing being bound.
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

    // Step 1: motion dilation. The closest-depth texel of the 3x3 neighbourhood lends its motion to
    // the whole footprint, so a silhouette reprojects with the foreground it belongs to rather than
    // with the background its own texel happens to sit on.
    float2 motion = motionTexture.Load(coord).xy;
    float closestDepth = depthTexture.Load(coord).r;
    for (int dy = -1; dy <= 1; ++dy)
    {
        for (int dx = -1; dx <= 1; ++dx)
        {
            int2 neighbor = clamp(int2(tid.xy) + int2(dx, dy), int2(0, 0), int2(extent) - 1);
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

    // Step 2: history position. Motion is a UV delta from the previous frame to this one.
    float2 uv = (float2(tid.xy) + 0.5) / extent;
    float2 historyUv = uv - motion;
    if (reason == kReasonNone && (any(historyUv < 0.0) || any(historyUv > 1.0)))
    {
        reason = kReasonOffScreen;
    }

    // Step 3: disocclusion. The surface this pixel sees is transformed into the previous frame's
    // clip space and its expected depth compared with what the previous frame's depth slot holds
    // where the history was fetched. Both sides of that comparison are dilated the way step 1
    // dilated the motion: the reconstruction uses the closest depth of this frame's 3x3, and the
    // previous depth is the closest of the 3x3 around the history position. Point-sampling one
    // texel on each side would report every sub-pixel coverage flip of a jittered thin edge as a
    // disocclusion -- the geometry did not move there, only the sample did -- and a surface that
    // really was uncovered has no near depth anywhere in its neighbourhood, so the dilated test
    // still catches it. A cleared or sky texel carries no depth to compare and is left to the
    // neighbourhood clip rather than rejected on a reconstructed infinity.
    if (reason == kReasonNone && closestDepth > 0.0)
    {
        float3 ndc = float3(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, closestDepth);
        float4 world = mul(params.inverseViewProjection, float4(ndc, 1.0));
        float4 previousClip = mul(params.previousViewProjection, float4(world.xyz / world.w, 1.0));
        if (previousClip.w > 0.0)
        {
            float expectedDepth = previousClip.z / previousClip.w;
            int2 historyTexel = clamp(int2(historyUv * extent), int2(0, 0), int2(extent) - 1);
            float previousDepth = 0.0;
            for (int dy = -1; dy <= 1; ++dy)
            {
                for (int dx = -1; dx <= 1; ++dx)
                {
                    int2 neighbor =
                        clamp(historyTexel + int2(dx, dy), int2(0, 0), int2(extent) - 1);
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

    // Step 4: history fetch and exposure correction. The pair is what the GPU actually applied, so
    // the ratio holds across a manual EV edit and across a mode switch alike.
    float3 history = current;
    float previousAge = 0.0;
    if (reason == kReasonNone)
    {
        history = sampleHistoryCatmullRom(historyTexture, linearSampler, historyUv, extent);
        float applied = rojoRHI::load<float>(root.exposureAddr, 0);
        float previous = rojoRHI::load<float>(root.exposureAddr, 1);
        history *= previous > 0.0 ? applied / previous : 1.0;
        previousAge = floor(historyTexture.SampleLevel(linearSampler, historyUv, 0.0).a);
    }

    if ((params.writeDiagnostics & 2u) != 0u)
    {
        reprojectedTexture[tid.xy] = float4(history, reason == kReasonNone ? 1.0 : 0.0);
    }

    // Step 5: neighbourhood clipping against the 3x3 mean and standard deviation this frame shaded.
    bool clipped = false;
    if (reason == kReasonNone)
    {
        float3 sum = float3(0.0, 0.0, 0.0);
        float3 sumSquares = float3(0.0, 0.0, 0.0);
        for (int dy = -1; dy <= 1; ++dy)
        {
            for (int dx = -1; dx <= 1; ++dx)
            {
                int2 neighbor = clamp(int2(tid.xy) + int2(dx, dy), int2(0, 0), int2(extent) - 1);
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

    // Step 6: blend weight. A fast-moving pixel trusts its history less, a reactive one less still,
    // and a rejected one not at all.
    float motionPixels = length(motion * extent);
    float alpha = lerp(kBaseAlpha, kMaxMotionAlpha, saturate(motionPixels / kMotionAlphaPixels));
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

    // Step 7: the age this pixel has accumulated, which is what the HistoryAge view reads back.
    float age = reason != kReasonNone ? 1.0 : min(previousAge + 1.0, kMaxHistoryAge);
    outputTexture[tid.xy] = float4(resolved, age);

    if ((params.writeDiagnostics & 1u) != 0u)
    {
        rejectionTexture[tid.xy] = float4(float(reason | (clipped ? kClippedBit : 0u)) / 255.0,
                                          alpha, clipped ? 1.0 : 0.0, 1.0);
    }
}
