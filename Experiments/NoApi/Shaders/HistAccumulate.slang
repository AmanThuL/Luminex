// P05 histogram.accumulate's binding frontend (spec section 6): reads the scene colour (R2) and the
// previous frame's resolved exposure (R5), bins this frame's scene-referred log-luminance into the
// 256-bin histogram (R4). Math lives in NoApiShared.slang; this file only binds resources at the
// slots the incumbent adapter's RhiAdapter.cpp documents.
//
// Slots: texture 0 scene colour; buffers 0 histogram (read-write, atomic), 1 exposure (read), 2
// params -- the same numbering Shaders/HistogramAccumulate.slang uses, so the two are easy to
// compare side by side.
import NoApiShared;

struct HistParams
{
    float logLuminanceMin;
    float logLuminanceMax;
};

[[vk::binding(0, 0)]]
Texture2D<float4> gSceneColor : register(t0); // → MSL [[texture(0)]]

[[vk::binding(0, 0)]]
RWStructuredBuffer<uint> gHistogram : register(u0); // → MSL [[buffer(0)]]

[[vk::binding(1, 0)]]
StructuredBuffer<float> gExposure : register(t1); // → MSL [[buffer(1)]]

[[vk::binding(2, 0)]]
ConstantBuffer<HistParams> gParams : register(b2); // → MSL [[buffer(2)]]

static const uint kHistogramBins = 256;
// The representative graph fixes the scene extent (spec section 6); no width/height uniform is
// needed the way production's dynamically resizable scene requires one.
static const uint kSceneExtent = 1024;

[shader("compute")]
[numthreads(8, 8, 1)]
void computeHistAccumulate(uint3 tid: SV_DispatchThreadID)
{
    if (tid.x >= kSceneExtent || tid.y >= kSceneExtent)
    {
        return;
    }

    float preExposure = gExposure[0];
    float3 displayedColor = gSceneColor.Load(int3(int2(tid.xy), 0)).rgb;
    float sceneLuminance = SceneReferredLuminance(displayedColor, preExposure);
    float logLuminance = LogLuminance(sceneLuminance);
    uint bin = HistogramBin(logLuminance, gParams.logLuminanceMin, gParams.logLuminanceMax,
                            kHistogramBins);

    InterlockedAdd(gHistogram[bin], 1u);
}
