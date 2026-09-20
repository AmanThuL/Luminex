// The bloom upsample-accumulate step (spec 10): a bilinear upsample of the smaller downsample-
// chain mip, added onto the larger mip's own base content. `gBase` always comes from the downsample
// chain; `gSmall` comes from that chain for the first step and from the previous accumulation output
// thereafter. The sum lands in a *different* graph texture (`gDst`) rather than back into the
// downsample chain, because one pass may read and write one texture only through disjoint ranges
// (spec 6) and base/dst would otherwise name the same mip of the same resource. Renderer.cpp's two
// graph-created bloom textures exist for exactly this reason -- see its comment for the full
// accounting.
//
// Slots: storage texture 0 base mip (read, downsample chain), 1 small mip (read, downsample or
// accumulate chain), 2 destination mip (write, accumulate chain); buffer 0 params.

struct BloomUpsampleParams
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
ConstantBuffer<BloomUpsampleParams> gParams : register(b0); // → MSL [[buffer(0)]]

[shader("compute")]
[numthreads(8, 8, 1)]
void computeBloomUpsample(uint3 tid: SV_DispatchThreadID)
{
    if (tid.x >= gParams.dstWidth || tid.y >= gParams.dstHeight)
    {
        return;
    }

    // Map pixel centres using the actual extents, including odd mip sizes. Clamp before taking
    // the fractional part so the outermost samples extend the edge texel without extrapolation.
    uint2 smallExtent = uint2(gParams.smallWidth, gParams.smallHeight);
    float2 smallPosition = (float2(tid.xy) + 0.5) * float2(smallExtent) /
                           float2(gParams.dstWidth, gParams.dstHeight) - 0.5;
    smallPosition = clamp(smallPosition, 0.0, float2(smallExtent - uint2(1, 1)));
    uint2 lo = uint2(smallPosition);
    uint2 hi = min(lo + uint2(1, 1), smallExtent - uint2(1, 1));
    float2 weight = frac(smallPosition);

    float4 base = gBase[tid.xy];
    float4 top = lerp(gSmall[lo], gSmall[uint2(hi.x, lo.y)], weight.x);
    float4 bottom = lerp(gSmall[uint2(lo.x, hi.y)], gSmall[hi], weight.x);
    float4 upsampled = lerp(top, bottom, weight.y);
    gDst[tid.xy] = base + upsampled;
}
