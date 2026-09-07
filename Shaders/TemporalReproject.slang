// The reprojection diagnostic (spec 6): fetches the previous frame's colour through this frame's
// motion and reports how far it landed from what this frame shaded. It measures the motion vectors
// and the history, and nothing downstream of it shades from the result -- accumulation is a later
// slice's work.
//
// Slots: textures 0 history, 1 scene colour, 2 motion; storage texture 3 diagnostic; sampler 0
// linear; buffer 0 params.
import Motion;

struct TemporalReprojectParams
{
    uint width;  // Render extent, which every one of the three inputs shares.
    uint height;
};

[[vk::binding(0, 0)]]
Texture2D<float4> gHistory : register(t0); // → MSL [[texture(0)]]

[[vk::binding(1, 0)]]
Texture2D<float4> gSceneColor : register(t1); // → MSL [[texture(1)]]

[[vk::binding(2, 0)]]
Texture2D<float2> gMotion : register(t2); // → MSL [[texture(2)]]

[[vk::binding(3, 0)]]
RWTexture2D<float4> gDiagnostic : register(u3); // → MSL [[texture(3)]]

[[vk::binding(0, 1)]]
SamplerState gLinearSampler : register(s0); // → MSL [[sampler(0)]]

[[vk::binding(0, 0)]]
ConstantBuffer<TemporalReprojectParams> gParams : register(b0); // → MSL [[buffer(0)]]

// Alpha 0 is the diagnostic's "nothing to compare": the history fetch left the extent, or the
// surface said its motion cannot be reprojected at all.
static const float4 kNoComparison = float4(0.0, 0.0, 0.0, 0.0);

[shader("compute")]
[numthreads(8, 8, 1)]
void computeTemporalReproject(uint3 tid: SV_DispatchThreadID)
{
    if (tid.x >= gParams.width || tid.y >= gParams.height)
    {
        return;
    }

    int3 coord = int3(int2(tid.xy), 0);
    float2 motion = gMotion.Load(coord).xy;
    if (isinf(motion.x) || isinf(motion.y))
    {
        gDiagnostic[tid.xy] = kNoComparison;
        return;
    }

    float2 extent = float2(float(gParams.width), float(gParams.height));
    float2 uv = (float2(tid.xy) + 0.5) / extent;
    // Motion is a UV delta from the previous frame to this one, so history sits at uv - motion.
    float2 historyUv = uv - motion;
    if (any(historyUv < 0.0) || any(historyUv > 1.0))
    {
        gDiagnostic[tid.xy] = kNoComparison;
        return;
    }

    float3 history = gHistory.SampleLevel(gLinearSampler, historyUv, 0.0).rgb;
    float3 current = gSceneColor.Load(coord).rgb;
    gDiagnostic[tid.xy] = float4(abs(history - current), 1.0);
}
