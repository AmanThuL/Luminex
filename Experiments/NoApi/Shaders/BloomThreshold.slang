// P07 bloom.threshold's binding frontend (spec section 6): a soft threshold on pre-exposed
// luminance, written at half the scene's resolution into R6 mip 0. Mirrors
// Shaders/BloomThreshold.slang's 2x2 box read into the same NoApiShared.slang formula.
//
// Slots: texture 0 scene colour; storage texture 1 bloom mip 0; buffer 0 params.
import NoApiShared;

struct ThresholdParams
{
    float threshold;
};

[[vk::binding(0, 0)]]
Texture2D<float4> gSceneColor : register(t0); // → MSL [[texture(0)]]

[[vk::binding(1, 0)]]
RWTexture2D<float4> gDst : register(u1); // → MSL [[texture(1)]]

[[vk::binding(0, 0)]]
ConstantBuffer<ThresholdParams> gParams : register(b0); // → MSL [[buffer(0)]]

// The representative graph fixes both extents (spec section 6): scene 1024x1024, bloom mip 0
// 512x512.
static const uint2 kSrcExtent = uint2(1024, 1024);
static const uint2 kDstExtent = uint2(512, 512);

//======================================================================================================================
float3 loadClamped(uint2 coord)
{
    uint2 clamped = min(coord, kSrcExtent - uint2(1, 1));
    return gSceneColor.Load(int3(int2(clamped), 0)).rgb;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void computeBloomThreshold(uint3 tid: SV_DispatchThreadID)
{
    if (tid.x >= kDstExtent.x || tid.y >= kDstExtent.y)
    {
        return;
    }

    uint2 base = tid.xy * 2u;
    float3 color = (loadClamped(base) + loadClamped(base + uint2(1, 0)) +
                    loadClamped(base + uint2(0, 1)) + loadClamped(base + uint2(1, 1))) *
                   0.25;

    gDst[tid.xy] = float4(BloomThresholdContribution(color, gParams.threshold), 1.0);
}
