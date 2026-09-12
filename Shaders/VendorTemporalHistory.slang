// Engine-owned history diagnostic for the vendor path; no vendor internal state is exposed.
// Uses TemporalUpscaleParams ABI and the native kernel's motion dilation, depth rejection,
// Catmull-Rom history fetch and applied/previous exposure correction, without accumulation.
import Motion;
import TemporalCommon;

struct TemporalUpscaleParams
{

    uint width;
    uint height;
    uint historyValid;
    uint writeDiagnostics;

    float4x4 inverseViewProjection;
    float4x4 previousViewProjection;

    float previousNearZ;

    float pad[3];

    uint outputWidth;
    uint outputHeight;
    uint allocatedWidth;
    uint allocatedHeight;
    uint previousRenderWidth;
    uint previousRenderHeight;

    float2 jitterOffset;
};

[[vk::binding(1, 0)]]
Texture2D<float> gDepth : register(t1);

[[vk::binding(2, 0)]]
Texture2D<float> gPreviousDepth : register(t2);

[[vk::binding(3, 0)]]
Texture2D<float2> gMotion : register(t3);

[[vk::binding(5, 0)]]
Texture2D<float4> gHistory : register(t5);

[[vk::binding(8, 0)]]
RWTexture2D<float4> gReprojected : register(u8);

[[vk::binding(0, 1)]]
SamplerState gLinearSampler : register(s0);

[[vk::binding(0, 0)]]
StructuredBuffer<float> gExposure : register(t0);

[[vk::binding(1, 0)]]
ConstantBuffer<TemporalUpscaleParams> gParams : register(b1);

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

[shader("compute")]
[numthreads(8, 8, 1)]
void computeVendorTemporalHistory(uint3 tid: SV_DispatchThreadID)
{
    if (tid.x >= gParams.outputWidth || tid.y >= gParams.outputHeight)
    {
        return;
    }

    float2 render = float2(float(gParams.width), float(gParams.height));
    float2 output = float2(float(gParams.outputWidth), float(gParams.outputHeight));

    float2 samplePos = (float2(tid.xy) + 0.5) * (render / output) + gParams.jitterOffset;
    int2 c = clamp(int2(floor(samplePos)), int2(0, 0), int2(render) - 1);
    float2 uv = (float2(tid.xy) + 0.5) / output;

    if (gParams.historyValid == 0u)
    {
        gReprojected[tid.xy] = float4(0.0);
        return;
    }

    uint reason = kReasonNone;

    float2 motion = gMotion.Load(int3(c, 0)).xy;
    float closestDepth = gDepth.Load(int3(c, 0)).r;
    for (int dy = -1; dy <= 1; ++dy)
    {
        for (int dx = -1; dx <= 1; ++dx)
        {
            int2 neighbor = clamp(c + int2(dx, dy), int2(0, 0), int2(render) - 1);
            int3 neighborCoord = int3(neighbor, 0);
            float neighborDepth = gDepth.Load(neighborCoord).r;

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

    float2 historyUv = uv - motion;
    if (reason == kReasonNone && (any(historyUv < 0.0) || any(historyUv > 1.0)))
    {
        reason = kReasonOffScreen;
    }

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

                reason = kReasonDisoccluded;
            }
        }
    }

    float3 history = float3(0.0);

    if (reason == kReasonNone)
    {
        history = sampleHistoryCatmullRom(historyUv, output);
        float applied = gExposure[0];
        float previous = gExposure[1];
        history *= previous > 0.0 ? applied / previous : 1.0;

    }

    if ((gParams.writeDiagnostics & 2u) != 0u)
    {
        gReprojected[tid.xy] = float4(history, reason == kReasonNone ? 1.0 : 0.0);
    }

}
