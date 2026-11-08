// Vendor input translation stays downstream of the engine's motion/exposure contract.
// The address root carries the active extent, the persistent exposure pair (read), the motion and
// reactive views and the storage views of the packed motion, packed reactive and exposure texel.
import RojoRHI;
import TemporalRoots;

[shader("compute")]
[numthreads(8, 8, 1)]
void computeVendorTemporalPack(uint3 tid: SV_DispatchThreadID)
{
    let root = rojoRHI::root<VendorTemporalPackRoot>();
    let params = rojoRHI::load<VendorPackParams>(root.paramsAddr, 0);
    let motionTexture = rojoRHI::texture2D<float2>(root.motion);
    let reactiveTexture = rojoRHI::texture2D<float>(root.reactive);
    let packedMotionTexture = rojoRHI::rwTexture2D<float2>(root.packedMotion);
    let packedReactiveTexture = rojoRHI::rwTexture2D<float>(root.packedReactive);
    let exposureTexelTexture = rojoRHI::rwTexture2D<float>(root.exposureTexel);
    if (tid.x >= params.width || tid.y >= params.height)
        return;
    float2 motion = motionTexture.Load(int3(tid.xy, 0));
    bool invalid = any(isinf(motion));
    packedMotionTexture[tid.xy] = invalid ? float2(0.0, 0.0) : motion;
    packedReactiveTexture[tid.xy] = invalid ? 1.0 : reactiveTexture.Load(int3(tid.xy, 0));
    if (tid.x == 0 && tid.y == 0)
        // MetalFX multiplies color by this value in its working space; cancel the engine's
        // pre-exposure there. Its output remains in the input's pre-exposed radiance domain.
        exposureTexelTexture[uint2(0, 0)] = 1.0 / rojoRHI::load<float>(root.exposureAddr, 0);
}
