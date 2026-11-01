// One step of the bloom downsample chain (spec 10): a 2x2 box filter from one mip of the graph's
// bloom texture into the next, through the per-mip storage views the render graph derives barriers
// for. Run once per mip transition -- Renderer.cpp declares one pass per level, because dispatches
// within one compute pass carry no ordering guarantee and each level depends on the one before it.
//
// The address root carries this dispatch's parameters and exact mip view indices.

import RojoRHI;
import BloomRoots;

//======================================================================================================================
float4 loadClamped(RWTexture2D<float4> source, uint2 coord, uint2 extent)
{
    return source[min(coord, extent - uint2(1, 1))];
}

[shader("compute")]
[numthreads(8, 8, 1)]
void computeBloomDownsample(uint3 tid: SV_DispatchThreadID)
{
    let root = rojoRHI::root<BloomDownsampleRoot>();
    let params = rojoRHI::load<BloomDownsampleParams>(root.paramsAddr, 0);
    let source = rojoRHI::rwTexture2D<float4>(root.source);
    let destination = rojoRHI::rwTexture2D<float4>(root.destination);
    if (tid.x >= params.dstWidth || tid.y >= params.dstHeight)
    {
        return;
    }

    uint2 srcExtent = uint2(params.srcWidth, params.srcHeight);
    uint2 base = tid.xy * 2u;
    float4 sum = loadClamped(source, base, srcExtent) +
                loadClamped(source, base + uint2(1, 0), srcExtent) +
                loadClamped(source, base + uint2(0, 1), srcExtent) +
                loadClamped(source, base + uint2(1, 1), srcExtent);
    destination[tid.xy] = sum * 0.25;
}
