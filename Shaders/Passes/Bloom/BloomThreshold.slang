// Bloom prefilter (spec 10): a soft threshold on pre-exposed luminance -- bloom tracks what the
// display sees, so manual and auto exposure shift it consistently with everything else -- written
// at half the scene's resolution into mip 0 of the graph's bloom chain texture. A 2x2 box read
// keeps the prefilter itself from aliasing before the downsample chain gets a chance to.
//
// Slots: texture 0 scene color; storage texture 0 bloom mip 0; buffer 0 params.

struct BloomThresholdParams
{
    float threshold; // Luminance below this contributes nothing (Render Settings "Bloom threshold").
    uint srcWidth;    // Scene color extent.
    uint srcHeight;
    uint dstWidth;    // Bloom mip 0 extent, ~ half the scene's.
    uint dstHeight;
};

[[vk::binding(0, 0)]]
Texture2D<float4> gSceneColor : register(t0); // → MSL [[texture(0)]]

[[vk::binding(1, 0)]]
RWTexture2D<float4> gBloomMip0 : register(u1); // → MSL [[texture(1)]]

[[vk::binding(0, 0)]]
ConstantBuffer<BloomThresholdParams> gParams : register(b0); // → MSL [[buffer(0)]]

static const float3 kLuminanceWeights = float3(0.2126, 0.7152, 0.0722);

//======================================================================================================================
float3 loadClamped(uint2 coord, uint2 extent)
{
    uint2 clamped = min(coord, extent - uint2(1, 1));
    return gSceneColor.Load(int3(int2(clamped), 0)).rgb;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void computeBloomThreshold(uint3 tid: SV_DispatchThreadID)
{
    if (tid.x >= gParams.dstWidth || tid.y >= gParams.dstHeight)
    {
        return;
    }

    uint2 srcExtent = uint2(gParams.srcWidth, gParams.srcHeight);
    uint2 base = tid.xy * 2u;
    float3 color = (loadClamped(base, srcExtent) + loadClamped(base + uint2(1, 0), srcExtent) +
                    loadClamped(base + uint2(0, 1), srcExtent) +
                    loadClamped(base + uint2(1, 1), srcExtent)) *
                   0.25;

    float luminance = dot(color, kLuminanceWeights);
    // Soft knee-free threshold: the fraction of luminance above the cutoff carries through
    // unchanged, so a probe well above threshold keeps its own color rather than saturating to
    // white -- what the bloom energy oracle checks against a CPU-side prediction.
    float contribution = max(luminance - gParams.threshold, 0.0) / max(luminance, 1e-4);
    gBloomMip0[tid.xy] = float4(color * contribution, 1.0);
}
