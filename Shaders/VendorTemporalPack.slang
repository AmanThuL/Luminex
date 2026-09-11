// Vendor input translation stays downstream of the engine's motion/exposure contract.
// Slots: textures 0 motion, 1 reactive, 2 packed motion, 3 packed reactive, 4 exposure texel;
// buffers 0 exposure pair, 1 active extent.
struct VendorPackParams
{
    uint width;
    uint height;
};

[[vk::binding(0, 0)]]
Texture2D<float2> gMotion : register(t0);
[[vk::binding(1, 0)]]
Texture2D<float> gReactive : register(t1);
[[vk::binding(2, 0)]]
RWTexture2D<float2> gPackedMotion : register(u2);
[[vk::binding(3, 0)]]
RWTexture2D<float> gPackedReactive : register(u3);
[[vk::binding(4, 0)]]
RWTexture2D<float> gExposureTexel : register(u4);
[[vk::binding(0, 0)]]
StructuredBuffer<float> gExposure : register(t0);
[[vk::binding(1, 0)]]
ConstantBuffer<VendorPackParams> gParams : register(b1);

[shader("compute")]
[numthreads(8, 8, 1)]
void computeVendorTemporalPack(uint3 tid: SV_DispatchThreadID)
{
    if (tid.x >= gParams.width || tid.y >= gParams.height)
        return;
    float2 motion = gMotion.Load(int3(tid.xy, 0));
    bool invalid = any(isinf(motion));
    gPackedMotion[tid.xy] = invalid ? float2(0.0, 0.0) : motion;
    gPackedReactive[tid.xy] = invalid ? 1.0 : gReactive.Load(int3(tid.xy, 0));
    if (tid.x == 0 && tid.y == 0)
        // MetalFX multiplies color by this value in its working space; cancel the engine's
        // pre-exposure there. Its output remains in the input's pre-exposed radiance domain.
        gExposureTexel[uint2(0, 0)] = 1.0 / gExposure[0];
}
