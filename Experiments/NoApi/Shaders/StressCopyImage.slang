// M5.1 Stage 4 hazard-matrix oracle (spec section 7), maintained-RHI side: a data-dependent
// identity copy through a storage texture. The hazard matrix's producer/consumer content is always
// defined on the CPU (Workload/StressCases.h's hazardExpectedTexel) and uploaded once as gSrc;
// every GPU "producer" role that needs to move that content into the case's target texture through
// a real compute pass does so with this one kernel, so the case's pass/barrier structure -- not a
// bespoke per-case formula -- is what a correct or incorrect ordering can be distinguished by.

struct CopyImageParams
{
    uint extent; // Square region the kernel covers, in texels of the addressed mip.
};

[[vk::binding(0, 0)]]
RWTexture2D<float4> gDst : register(u0); // -> MSL [[texture(0)]]

[[vk::binding(1, 0)]]
Texture2D<float4> gSrc : register(t1); // -> MSL [[texture(1)]]

[[vk::binding(0, 1)]]
ConstantBuffer<CopyImageParams> gParams : register(b1); // -> MSL [[buffer(1)]]

[shader("compute")]
[numthreads(8, 8, 1)]
void computeCopyImage(uint3 tid: SV_DispatchThreadID)
{
    if (tid.x >= gParams.extent || tid.y >= gParams.extent)
    {
        return;
    }
    gDst[tid.xy] = gSrc.Load(int3(tid.xy, 0));
}
