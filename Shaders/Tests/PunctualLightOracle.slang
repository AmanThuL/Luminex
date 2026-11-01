// Test-only oracle for Shaders/Common/Lighting.slang's ComputePunctualLight: one thread per sample,
// evaluating the shipped function on a host-supplied light row and surface and writing the
// scene-linear result straight back. Nothing here reimplements the light model -- the point is to
// run the production function and let Tests/Render/Passes/LocalLights/GpuLocalLightTests.cpp hold
// it against Source/Engine/Lights/LocalLightMath.cpp's CPU mirror.
//
// The math mode is the file default, which is the mode the scene pass compiles under, so the
// agreement this measures is the agreement the renderer will actually have.

import Lighting;
import SceneTables;
import LocalLightOracleRoots;

[[vk::binding(0, 0)]]
RWStructuredBuffer<float4> gOutput : register(u0); // → MSL [[buffer(0)]]

[[vk::binding(1, 0)]]
StructuredBuffer<PunctualSample> gSamples : register(t1); // → MSL [[buffer(1)]]

[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain(uint3 tid: SV_DispatchThreadID)
{
    PunctualSample item = gSamples[tid.x];
    float3 radiance = ComputePunctualLight(item.light, item.position, item.normal,
                                           item.toEye, item.baseColour, item.f0,
                                           item.metallic, item.alpha);
    gOutput[tid.x] = float4(radiance, 0.0f);
}
