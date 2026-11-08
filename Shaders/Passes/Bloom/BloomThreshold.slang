// Bloom prefilter (spec 10): a soft threshold on pre-exposed luminance -- bloom tracks what the
// display sees, so manual and auto exposure shift it consistently with everything else -- written
// at half the scene's resolution into mip 0 of the graph's bloom chain texture. A 2x2 box read
// keeps the prefilter itself from aliasing before the downsample chain gets a chance to.
//
// The address root carries this dispatch's parameters and exact mip view indices.

import RojoRHI;
import BloomRoots;

static const float3 kLuminanceWeights = float3(0.2126, 0.7152, 0.0722);

//======================================================================================================================
float3 loadClamped(Texture2D<float4> sceneColor, uint2 coord, uint2 extent)
{
    uint2 clamped = min(coord, extent - uint2(1, 1));
    return sceneColor.Load(int3(int2(clamped), 0)).rgb;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void computeBloomThreshold(uint3 tid: SV_DispatchThreadID)
{
    let root = rojoRHI::root<BloomThresholdRoot>();
    let params = rojoRHI::load<BloomThresholdParams>(root.paramsAddr, 0);
    let sceneColor = rojoRHI::texture2D<float4>(root.sceneColor);
    let destination = rojoRHI::rwTexture2D<float4>(root.destination);
    if (tid.x >= params.dstWidth || tid.y >= params.dstHeight)
    {
        return;
    }

    uint2 srcExtent = uint2(params.srcWidth, params.srcHeight);
    uint2 base = tid.xy * 2u;
    float3 color = (loadClamped(sceneColor, base, srcExtent) +
                    loadClamped(sceneColor, base + uint2(1, 0), srcExtent) +
                    loadClamped(sceneColor, base + uint2(0, 1), srcExtent) +
                    loadClamped(sceneColor, base + uint2(1, 1), srcExtent)) *
                   0.25;

    float luminance = dot(color, kLuminanceWeights);
    // Soft knee-free threshold: the fraction of luminance above the cutoff carries through
    // unchanged, so a probe well above threshold keeps its own color rather than saturating to
    // white -- what the bloom energy oracle checks against a CPU-side prediction.
    float contribution = max(luminance - params.threshold, 0.0) / max(luminance, 1e-4);
    destination[tid.xy] = float4(color * contribution, 1.0);
}
