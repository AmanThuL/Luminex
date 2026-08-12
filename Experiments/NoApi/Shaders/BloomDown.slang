// P08/P09 bloom.down{1,2}'s shared binding frontend (spec section 6): one 2x2 box-filter step of
// the downsample chain, run once per mip transition with the source and destination extents (and
// therefore the dispatch grid) supplied per pass by RhiAdapter.cpp -- the same pipeline serves both
// P08 (R6 mip 0 -> mip 1) and P09 (R6 mip 1 -> mip 2) through different storage-texture views.
// Mirrors Shaders/BloomDownsample.slang; math lives in NoApiShared.slang.
//
// Slots: storage texture 0 source mip (read), 1 destination mip (write); buffer 0 params.
import NoApiShared;

struct DownsampleParams
{
    uint srcWidth;
    uint srcHeight;
    uint dstWidth;
    uint dstHeight;
};

[[vk::binding(0, 0)]]
RWTexture2D<float4> gSrc : register(u0); // → MSL [[texture(0)]]

[[vk::binding(1, 0)]]
RWTexture2D<float4> gDst : register(u1); // → MSL [[texture(1)]]

[[vk::binding(0, 0)]]
ConstantBuffer<DownsampleParams> gParams : register(b0); // → MSL [[buffer(0)]]

//======================================================================================================================
float4 loadClamped(uint2 coord, uint2 extent)
{
    return gSrc[min(coord, extent - uint2(1, 1))];
}

[shader("compute")]
[numthreads(8, 8, 1)]
void computeBloomDown(uint3 tid: SV_DispatchThreadID)
{
    if (tid.x >= gParams.dstWidth || tid.y >= gParams.dstHeight)
    {
        return;
    }

    uint2 srcExtent = uint2(gParams.srcWidth, gParams.srcHeight);
    uint2 base = tid.xy * 2u;
    gDst[tid.xy] = BoxFilter4(loadClamped(base, srcExtent), loadClamped(base + uint2(1, 0), srcExtent),
                              loadClamped(base + uint2(0, 1), srcExtent),
                              loadClamped(base + uint2(1, 1), srcExtent));
}
