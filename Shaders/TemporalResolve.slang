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
// Slots: textures 0 scene colour, 1 depth, 2 previous depth, 3 motion, 4 reactive, 5 history;
// storage textures 6 output, 7 rejection, 8 reprojected; sampler 0 clamped linear; buffers 0
// exposure pair (read), 1 params.
import Motion;

struct TemporalResolveParams
{
    uint width;  // Render extent, shared by every input and both outputs.
    uint height;
    uint historyValid;      // 0 on a reset frame: steps 1-6 are skipped and no history is read.
    uint writeDiagnostics;  // Bit 0 writes rejection; bit 1 writes reprojected history.
    // Unjittered, so a reconstructed world position does not carry the frame's sub-pixel offset.
    float4x4 inverseViewProjection;
    float4x4 previousViewProjection;
    // The previous frame's positive near-plane distance. Both sides of the disocclusion comparison
    // are depths in the *previous* frame's clip space, so one near plane inverts both of them.
    float previousNearZ;
    float3 pad; // Keeps the block a multiple of 16 bytes.
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
ConstantBuffer<TemporalResolveParams> gParams : register(b1); // → MSL [[buffer(1)]]

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
static const float kBaseAlpha = 0.1;
static const float kMaxMotionAlpha = 0.25;
static const float kMotionAlphaPixels = 8.0;
static const float kClipGamma = 1.0;
static const float kDisocclusionTolerance = 0.05;

static const float3 kLuminanceWeights = float3(0.2126, 0.7152, 0.0722);
// The largest age the alpha channel carries; the count saturates rather than wrapping.
static const float kMaxHistoryAge = 65535.0;

float luminance(float3 color)
{
    return dot(color, kLuminanceWeights);
}

// YCoCg is the space the neighbourhood box is built in: it separates the luminance a temporal
// artefact shows up in from the chroma, so a box tight enough to reject a ghost does not also
// desaturate the colour that is still valid.
float3 rgbToYcocg(float3 c)
{
    return float3(0.25 * c.r + 0.5 * c.g + 0.25 * c.b, 0.5 * (c.r - c.b),
                  -0.25 * c.r + 0.5 * c.g - 0.25 * c.b);
}

float3 ycocgToRgb(float3 c)
{
    float t = c.x - c.z;
    return float3(t + c.y, c.x + c.z, t - c.y);
}

// Reversed infinite-far depth inverts to a positive view distance through the near plane. A
// cleared or sky texel holds 0, which has no finite distance; the caller keeps it out of the
// comparison rather than letting the guard decide what "very far" means.
float viewDistance(float depth, float nearZ)
{
    return nearZ / max(depth, 1e-7);
}

// Moves `history` along the segment toward `current` until it lies inside [minimum, maximum].
// Clipping toward the colour this frame shaded -- rather than clamping per channel -- is what
// keeps the result on the line between the two, so a rejected chroma cannot invent a hue neither
// frame held.
float3 clipTowardCurrent(float3 history, float3 current, float3 minimum, float3 maximum)
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

// Five-tap Catmull-Rom: a sharper history fetch than bilinear, which is what stops an accumulated
// image from softening a little more every frame it is reprojected.
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
    if (tid.x >= gParams.width || tid.y >= gParams.height)
    {
        return;
    }

    int3 coord = int3(int2(tid.xy), 0);
    float2 extent = float2(float(gParams.width), float(gParams.height));
    float3 current = gSceneColor.Load(coord).rgb;
    float reactive = gReactive.Load(coord).r;

    // Step 8: a reset frame has no history that describes this scene. It writes what it shaded and
    // starts the per-pixel count over; the pass binds the same textures either way, so nothing
    // below is reached rather than nothing being bound.
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

    // Step 1: motion dilation. The closest-depth texel of the 3x3 neighbourhood lends its motion to
    // the whole footprint, so a silhouette reprojects with the foreground it belongs to rather than
    // with the background its own texel happens to sit on.
    float2 motion = gMotion.Load(coord).xy;
    float closestDepth = gDepth.Load(coord).r;
    for (int dy = -1; dy <= 1; ++dy)
    {
        for (int dx = -1; dx <= 1; ++dx)
        {
            int2 neighbor = clamp(int2(tid.xy) + int2(dx, dy), int2(0, 0), int2(extent) - 1);
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
        float4 world = mul(gParams.inverseViewProjection, float4(ndc, 1.0));
        float4 previousClip = mul(gParams.previousViewProjection, float4(world.xyz / world.w, 1.0));
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

    // Step 4: history fetch and exposure correction. The pair is what the GPU actually applied, so
    // the ratio holds across a manual EV edit and across a mode switch alike.
    float3 history = current;
    float previousAge = 0.0;
    if (reason == kReasonNone)
    {
        history = sampleHistoryCatmullRom(historyUv, extent);
        float applied = gExposure[0];
        float previous = gExposure[1];
        history *= previous > 0.0 ? applied / previous : 1.0;
        previousAge = floor(gHistory.SampleLevel(gLinearSampler, historyUv, 0.0).a);
    }

    if ((gParams.writeDiagnostics & 2u) != 0u)
    {
        gReprojected[tid.xy] = float4(history, reason == kReasonNone ? 1.0 : 0.0);
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
    gOutput[tid.xy] = float4(resolved, age);

    if ((gParams.writeDiagnostics & 1u) != 0u)
    {
        gRejection[tid.xy] = float4(float(reason | (clipped ? kClippedBit : 0u)) / 255.0, alpha,
                                    clipped ? 1.0 : 0.0, 1.0);
    }
}
