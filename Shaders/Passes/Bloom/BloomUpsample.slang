// The bloom upsample-accumulate step (spec 10): a bilinear upsample of the smaller downsample-
// chain mip, added onto the larger mip's own base content. `baseTexture` always comes from the downsample
// chain; `smallTexture` comes from that chain for the first step and from the previous accumulation output
// thereafter. The sum lands in a *different* graph texture (`destination`) rather than back into the
// downsample chain, because one pass may read and write one texture only through disjoint ranges
// (spec 6) and base/dst would otherwise name the same mip of the same resource. Renderer.cpp's two
// graph-created bloom textures exist for exactly this reason -- see its comment for the full
// accounting.
//
// The address root carries this dispatch's parameters and exact mip view indices.

import RojoRHI;
import BloomRoots;

[shader("compute")]
[numthreads(8, 8, 1)]
void computeBloomUpsample(uint3 tid: SV_DispatchThreadID)
{
    let root = rojoRHI::root<BloomUpsampleRoot>();
    let params = rojoRHI::load<BloomUpsampleParams>(root.paramsAddr, 0);
    let baseTexture = rojoRHI::rwTexture2D<float4>(root.base);
    let smallTexture = rojoRHI::rwTexture2D<float4>(root.small);
    let destination = rojoRHI::rwTexture2D<float4>(root.destination);
    if (tid.x >= params.dstWidth || tid.y >= params.dstHeight)
    {
        return;
    }

    // Map pixel centres using the actual extents, including odd mip sizes. Clamp before taking
    // the fractional part so the outermost samples extend the edge texel without extrapolation.
    uint2 smallExtent = uint2(params.smallWidth, params.smallHeight);
    float2 smallPosition = (float2(tid.xy) + 0.5) * float2(smallExtent) /
                           float2(params.dstWidth, params.dstHeight) - 0.5;
    smallPosition = clamp(smallPosition, 0.0, float2(smallExtent - uint2(1, 1)));
    uint2 lo = uint2(smallPosition);
    uint2 hi = min(lo + uint2(1, 1), smallExtent - uint2(1, 1));
    float2 weight = frac(smallPosition);

    float4 base = baseTexture[tid.xy];
    float4 top = lerp(smallTexture[lo], smallTexture[uint2(hi.x, lo.y)], weight.x);
    float4 bottom = lerp(smallTexture[uint2(lo.x, hi.y)], smallTexture[hi], weight.x);
    float4 upsampled = lerp(top, bottom, weight.y);
    destination[tid.xy] = base + upsampled;
}
