// P10/P11 bloom.up{1,2}'s shared binding frontend (spec section 6): the upsample-accumulate step,
// run once per mip transition with the base/small/destination extents supplied per pass by
// RhiAdapter.cpp -- the same pipeline serves both P10 (base R6 mip 1, small R6 mip 2, dest R7 mip 1)
// and P11 (base R6 mip 0, small R7 mip 1, dest R7 mip 0). Mirrors Shaders/BloomUpsample.slang; math
// lives in NoApiShared.slang.
//
// Slots: storage texture 0 base mip (read), 1 small mip (read), 2 destination mip (write); buffer 0
// params.
import NoApiShared;

struct UpsampleParams
{
    uint smallWidth;
    uint smallHeight;
    uint dstWidth;
    uint dstHeight;
};

[[vk::binding(0, 0)]]
RWTexture2D<float4> gBase : register(u0); // → MSL [[texture(0)]]

[[vk::binding(1, 0)]]
RWTexture2D<float4> gSmall : register(u1); // → MSL [[texture(1)]]

[[vk::binding(2, 0)]]
RWTexture2D<float4> gDst : register(u2); // → MSL [[texture(2)]]

[[vk::binding(0, 0)]]
ConstantBuffer<UpsampleParams> gParams : register(b0); // → MSL [[buffer(0)]]

[shader("compute")]
[numthreads(8, 8, 1)]
void computeBloomUp(uint3 tid: SV_DispatchThreadID)
{
    if (tid.x >= gParams.dstWidth || tid.y >= gParams.dstHeight)
    {
        return;
    }

    uint2 smallExtent = uint2(gParams.smallWidth, gParams.smallHeight);
    uint2 smallCoord = min(tid.xy / 2u, smallExtent - uint2(1, 1));

    gDst[tid.xy] = UpsampleAccumulate(gBase[tid.xy], gSmall[smallCoord]);
}
