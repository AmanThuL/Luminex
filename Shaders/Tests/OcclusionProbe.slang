// Emits every projection field for exact comparison with the ordered CPU oracle.
import Occlusion;
struct ProbeBounds
{
    float4 minimum;
    float4 maximum;
};
[[vk::binding(0, 0)]]
StructuredBuffer<ProbeBounds> gBounds : register(t0);
[[vk::binding(1, 0)]]
RWStructuredBuffer<uint> gOutput : register(u1);
[[vk::binding(13, 0)]]
ConstantBuffer<OcclusionParams> gParams : register(b13);
[[vk::binding(0, 2)]]
Texture2D<float> gPyramid : register(t0);
[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
    ProbeBounds bounds = gBounds[id.x];
    OcclusionProjection result = testOcclusionBounds(bounds.minimum.xyz, bounds.maximum.xyz,
                                                       gParams, gPyramid);
    uint base = id.x * 8;
    gOutput[base] = asuint(result.rectangle.x);
    gOutput[base + 1] = asuint(result.rectangle.y);
    gOutput[base + 2] = asuint(result.rectangle.z);
    gOutput[base + 3] = asuint(result.rectangle.w);
    gOutput[base + 4] = result.level;
    gOutput[base + 5] = asuint(result.zBox);
    gOutput[base + 6] = result.outcome;
    gOutput[base + 7] = result.occluded;
}
