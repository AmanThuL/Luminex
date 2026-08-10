// The bloom upsample-accumulate step (spec 10): a nearest 2x upsample of the smaller downsample-
// chain mip, added onto the larger mip's own base content. The two reads come from the downsample
// chain's texture (`gBase`/`gSmall`, both read-only here); the sum lands in a *different* graph
// texture (`gDst`) rather than back into the downsample chain, because one pass may read and write
// one texture only through disjoint ranges (spec 6) and base/dst would otherwise name the same
// mip of the same resource. Renderer.cpp's two graph-created bloom textures exist for exactly this
// reason -- see its comment for the full accounting.
//
// Slots: storage texture 0 base mip (read, downsample chain), 1 small mip (read, downsample
// chain), 2 destination mip (write, accumulate chain); buffer 0 params.

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

    uint2 smallExtent = uint2(gParams.smallWidth, gParams.smallHeight);
    uint2 smallCoord = min(tid.xy / 2u, smallExtent - uint2(1, 1));

    float4 base = gBase[tid.xy];
    float4 upsampled = gSmall[smallCoord];
    gDst[tid.xy] = base + upsampled;
}
