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
// The address root carries this dispatch's parameters, the history, scene-colour and motion
// views, the diagnostic's storage view and the clamped linear sampler.
import RojoRHI;
import Motion;
import TemporalRoots;

// Alpha 0 is the diagnostic's "nothing to compare": the history fetch left the extent, or the
// surface said its motion cannot be reprojected at all.
static const float4 kNoComparison = float4(0.0, 0.0, 0.0, 0.0);

[shader("compute")]
[numthreads(8, 8, 1)]
void computeTemporalReproject(uint3 tid: SV_DispatchThreadID)
{
    let root = rojoRHI::root<TemporalReprojectRoot>();
    let params = rojoRHI::load<TemporalReprojectParams>(root.paramsAddr, 0);
    let historyTexture = rojoRHI::texture2D<float4>(root.history);
    let sceneColorTexture = rojoRHI::texture2D<float4>(root.sceneColor);
    let motionTexture = rojoRHI::texture2D<float2>(root.motion);
    let diagnosticTexture = rojoRHI::rwTexture2D<float4>(root.diagnostic);
    let linearSampler = rojoRHI::sampler(root.sampler);
    if (tid.x >= params.width || tid.y >= params.height)
    {
        return;
    }

    float2 render = float2(float(params.renderWidth), float(params.renderHeight));
    float2 output = float2(float(params.width), float(params.height));
    float2 ratio = render / output;

    // Source/Render/Temporal.h's renderSamplePosition: where in the jittered render image this
    // output pixel reads its current colour.
    float2 samplePosition = (float2(tid.xy) + 0.5) * ratio + params.jitterOffset;
    // Motion is a render-extent signal, and the texel to take it from is the one that sample falls
    // in -- Shaders/Passes/Temporal/TemporalUpscale.slang's nearest texel, so the diagnostic
    // measures the motion of the surface it compares. Clamped so the last row and column of a
    // rounded render extent stay addressable.
    int2 motionCoord = clamp(int2(floor(samplePosition)), int2(0, 0), int2(render) - 1);
    float2 motion = motionTexture.Load(int3(motionCoord, 0)).xy;
    if (isinf(motion.x) || isinf(motion.y))
    {
        diagnosticTexture[tid.xy] = kNoComparison;
        return;
    }

    float2 uv = (float2(tid.xy) + 0.5) / output;
    // Motion is a UV delta from the previous frame to this one, so history sits at uv - motion.
    float2 historyUv = uv - motion;
    if (any(historyUv < 0.0) || any(historyUv > 1.0))
    {
        diagnosticTexture[tid.xy] = kNoComparison;
        return;
    }

    float3 history = historyTexture.SampleLevel(linearSampler, historyUv, 0.0).rgb;
    // The sample position clamped into the active rectangle and taken as a UV over the allocation,
    // so no filter weight reaches the region outside the rectangle.
    float2 allocated = float2(float(params.allocatedWidth), float(params.allocatedHeight));
    float2 sampleUv = clamp(samplePosition, float2(0.5, 0.5), render - 0.5) / allocated;
    float3 current = sceneColorTexture.SampleLevel(linearSampler, sampleUv, 0.0).rgb;
    diagnosticTexture[tid.xy] = float4(abs(history - current), 1.0);
}
