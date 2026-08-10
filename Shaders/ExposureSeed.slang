// Writes the manual exposure multiplier into the persistent exposure buffer (spec 9's reset rule):
// Renderer.cpp declares this pass only on the frame one of the four reset triggers fires (first
// frame, scene switch, auto-exposure enable, resize), and only while auto-exposure is on -- an
// ordinary compute dispatch, not a CPU/GPU stall, which is what keeps the reset path inside the
// three-frames-in-flight contract everything else in the frame already honours.
//
// Slots: buffer 0 exposure (write), 1 params.

struct ExposureSeedParams
{
    float exposure; // exp2(manual EV), computed on the CPU once per reset frame.
};

[[vk::binding(0, 0)]]
RWStructuredBuffer<float> gExposure : register(u0); // → MSL [[buffer(0)]]

[[vk::binding(1, 0)]]
ConstantBuffer<ExposureSeedParams> gParams : register(b1); // → MSL [[buffer(1)]]

[shader("compute")]
[numthreads(1, 1, 1)]
void computeExposureSeed(uint3 tid: SV_DispatchThreadID)
{
    gExposure[0] = gParams.exposure;
}
