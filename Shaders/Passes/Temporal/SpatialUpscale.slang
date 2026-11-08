// The spatial upscale (M6.3 spec 7): one invocation per output pixel resamples the jittered render
// image with a five-tap Catmull-Rom fetch at the position that pixel's centre was sampled at. It is
// the reference an accumulating upscale is measured against -- the same picture without the
// accumulation -- and it is what the Raw mode commits into its colour slot when the render extent
// is smaller than the output one, in place of the one-to-one copy.
//
// Everything here stays on pre-exposed scene-linear radiance; nothing encodes or tone maps.
//
// The address root carries this dispatch's parameters, the scene-colour view, the colour slot's
// storage view and the clamped linear sampler.
import RojoRHI;
import TemporalRoots;

// A tap position in render texels, turned into a UV the sampler may take. The clamp keeps the
// bilinear footprint of every tap inside the active rectangle, so no filter weight reaches the
// stale region the render extent leaves outside it; the division is by the *allocated* extent,
// because that is the texture the UV addresses.
float2 activeUv(SpatialUpscaleParams params, float2 position)
{
    float2 render = float2(float(params.renderWidth), float(params.renderHeight));
    float2 allocated = float2(float(params.allocatedWidth), float(params.allocatedHeight));
    return clamp(position, float2(0.5, 0.5), render - 0.5) / allocated;
}

// Five-tap Catmull-Rom over the render image. Copied from
// Shaders/Passes/Temporal/TemporalResolve.slang's sampleHistoryCatmullRom, which stays the source
// of truth for the kernel: the weights, the five-tap renormalisation and the negative-lobe clamp
// are that function's, with the taps taken over the active rectangle instead of a whole-texture UV.
float3 sampleCurrentCatmullRom(SpatialUpscaleParams params, Texture2D<float4> sceneColorTexture,
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
    // Removing the four corner taps also removes their positive weight. Renormalize the five
    // remaining taps so a constant radiance stays constant at every fractional sample position.
    float weightSum = w12.x * (w0.y + w12.y + w3.y) + (w0.x + w3.x) * w12.y;
    // The five taps carry a small negative lobe, which can push a high-contrast edge below zero.
    return max(result / weightSum, 0.0);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void computeSpatialUpscale(uint3 tid: SV_DispatchThreadID)
{
    let root = rojoRHI::root<SpatialUpscaleRoot>();
    let params = rojoRHI::load<SpatialUpscaleParams>(root.paramsAddr, 0);
    let sceneColorTexture = rojoRHI::texture2D<float4>(root.sceneColor);
    let outputTexture = rojoRHI::rwTexture2D<float4>(root.output);
    let linearSampler = rojoRHI::sampler(root.sampler);
    if (tid.x >= params.outputWidth || tid.y >= params.outputHeight)
    {
        return;
    }

    // Source/Render/Temporal.h's renderSamplePosition: where the jittered raster put the scene the
    // centre of this output pixel is looking at.
    float2 ratio = float2(float(params.renderWidth) / float(params.outputWidth),
                          float(params.renderHeight) / float(params.outputHeight));
    float2 samplePosition = (float2(tid.xy) + 0.5) * ratio + params.jitterOffset;
    // Alpha 1 is one frame of accumulation: this picture is built from this frame alone, which is
    // the age the colour slot's alpha carries everywhere else.
    outputTexture[tid.xy] = float4(
        sampleCurrentCatmullRom(params, sceneColorTexture, linearSampler, samplePosition), 1.0);
}
