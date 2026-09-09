// The reprojection diagnostic (spec 6): fetches the previous frame's colour through this frame's
// motion and reports how far it landed from what this frame shaded. It measures the motion vectors
// and the history, and nothing downstream of it shades from the result -- accumulation is a later
// slice's work.
//
// It runs at the output extent, because that is what the history and the diagnostic are: the
// history is compared against the current colour resampled out of the render extent at the
// position that output pixel was sampled at, so a render extent below the output one measures the
// same thing the display sees rather than a rectangle of it.
//
// Slots: textures 0 history, 1 scene colour, 2 motion; storage texture 3 diagnostic; sampler 0
// linear; buffer 0 params.
import Motion;

struct TemporalReprojectParams
{
    // The output extent: the dispatch bound, and what the history and the diagnostic are.
    uint width;
    uint height;
    uint renderWidth;  // Active rectangle of the scene colour and the motion target.
    uint renderHeight;
    uint allocatedWidth;  // Allocation of those two, which every UV over them is taken over.
    uint allocatedHeight;
    // Where the jittered raster placed the scene, in render texels of texture space: the CPU-side
    // jitterTexelOffset(jitter) of Source/Render/Temporal.h.
    float2 jitterOffset;
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

    float2 render = float2(float(gParams.renderWidth), float(gParams.renderHeight));
    float2 output = float2(float(gParams.width), float(gParams.height));
    float2 ratio = render / output;

    // Source/Render/Temporal.h's renderSamplePosition: where in the jittered render image this
    // output pixel reads its current colour.
    float2 samplePosition = (float2(tid.xy) + 0.5) * ratio + gParams.jitterOffset;
    // Motion is a render-extent signal, and the texel to take it from is the one that sample falls
    // in -- Shaders/TemporalUpscale.slang's nearest texel, so the diagnostic measures the motion of
    // the surface it compares. Clamped so the last row and column of a rounded render extent stay
    // addressable.
    int2 motionCoord = clamp(int2(floor(samplePosition)), int2(0, 0), int2(render) - 1);
    float2 motion = gMotion.Load(int3(motionCoord, 0)).xy;
    if (isinf(motion.x) || isinf(motion.y))
    {
        gDiagnostic[tid.xy] = kNoComparison;
        return;
    }

    float2 uv = (float2(tid.xy) + 0.5) / output;
    // Motion is a UV delta from the previous frame to this one, so history sits at uv - motion.
    float2 historyUv = uv - motion;
    if (any(historyUv < 0.0) || any(historyUv > 1.0))
    {
        gDiagnostic[tid.xy] = kNoComparison;
        return;
    }

    float3 history = gHistory.SampleLevel(gLinearSampler, historyUv, 0.0).rgb;
    // The sample position clamped into the active rectangle and taken as a UV over the allocation,
    // so no filter weight reaches the region outside the rectangle.
    float2 allocated = float2(float(gParams.allocatedWidth), float(gParams.allocatedHeight));
    float2 sampleUv = clamp(samplePosition, float2(0.5, 0.5), render - 0.5) / allocated;
    float3 current = gSceneColor.SampleLevel(gLinearSampler, sampleUv, 0.0).rgb;
    gDiagnostic[tid.xy] = float4(abs(history - current), 1.0);
}
