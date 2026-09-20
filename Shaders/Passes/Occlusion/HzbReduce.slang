// Exact reversed-Z minimum reduction. Source level addresses the whole sampled texture;
// destination is a one-mip storage view. Odd edge children outside the active rectangle do not exist.
struct HzbReduceParams
{
    uint sourceWidth;
    uint sourceHeight;
    uint destinationWidth;
    uint destinationHeight;
    uint sourceLevel;
};
[[vk::binding(0, 0)]]
Texture2D<float> gSource : register(t0);
[[vk::binding(1, 0)]]
RWTexture2D<float> gDestination : register(u1);
[[vk::binding(0, 0)]]
ConstantBuffer<HzbReduceParams> gParams : register(b0);

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= gParams.destinationWidth || tid.y >= gParams.destinationHeight)
        return;
    float farthest = 1.0;
    for (uint y = 0; y < 2; ++y)
    {
        for (uint x = 0; x < 2; ++x)
        {
            uint2 p = tid.xy * 2 + uint2(x, y);
            if (p.x < gParams.sourceWidth && p.y < gParams.sourceHeight)
                farthest = min(farthest, gSource.Load(int3(p, gParams.sourceLevel)));
        }
    }
    gDestination[tid.xy] = farthest;
}
