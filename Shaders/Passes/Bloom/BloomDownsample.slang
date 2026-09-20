// One step of the bloom downsample chain (spec 10): a 2x2 box filter from one mip of the graph's
// bloom texture into the next, through the per-mip storage views the render graph derives barriers
// for. Run once per mip transition -- Renderer.cpp declares one pass per level, because dispatches
// within one compute pass carry no ordering guarantee and each level depends on the one before it.
//
// Slots: storage texture 0 source mip (read), 1 destination mip (write); buffer 0 params.

struct BloomDownsampleParams
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
ConstantBuffer<BloomDownsampleParams> gParams : register(b0); // → MSL [[buffer(0)]]

//======================================================================================================================
float4 loadClamped(uint2 coord, uint2 extent)
{
    return gSrc[min(coord, extent - uint2(1, 1))];
}

[shader("compute")]
[numthreads(8, 8, 1)]
void computeBloomDownsample(uint3 tid: SV_DispatchThreadID)
{
    if (tid.x >= gParams.dstWidth || tid.y >= gParams.dstHeight)
    {
        return;
    }

    uint2 srcExtent = uint2(gParams.srcWidth, gParams.srcHeight);
    uint2 base = tid.xy * 2u;
    float4 sum = loadClamped(base, srcExtent) + loadClamped(base + uint2(1, 0), srcExtent) +
                loadClamped(base + uint2(0, 1), srcExtent) + loadClamped(base + uint2(1, 1), srcExtent);
    gDst[tid.xy] = sum * 0.25;
}
