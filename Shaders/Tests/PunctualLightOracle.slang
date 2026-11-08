// Test-only oracle for Shaders/Common/Lighting.slang's ComputePunctualLight: one thread per sample,
// evaluating the shipped function on a host-supplied light row and surface and writing the
// scene-linear result straight back. Nothing here reimplements the light model -- the point is to
// run the production function and let Tests/Render/Passes/LocalLights/GpuLocalLightTests.cpp hold
// it against Source/Engine/Lights/LocalLightMath.cpp's CPU mirror.
//
// The math mode is the file default, which is the mode the scene pass compiles under, so the
// agreement this measures is the agreement the renderer will actually have.
//
// The address root carries the sample table and the float4 output array.

import RojoRHI;
import Lighting;
import SceneTables;
import LocalLightOracleRoots;

[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain(uint3 tid: SV_DispatchThreadID)
{
    let root = rojoRHI::root<PunctualLightRoot>();
    PunctualSample item = rojoRHI::load<PunctualSample>(root.samples, tid.x);
    float3 radiance = ComputePunctualLight(item.light, item.position, item.normal,
                                           item.toEye, item.baseColour, item.f0,
                                           item.metallic, item.alpha);
    rojoRHI::store<float4>(root.output, tid.x, float4(radiance, 0.0f));
}
