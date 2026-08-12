// P05 histogram.accumulate's address-first frontend (spec section 6). Same math module as the
// incumbent's HistAccumulate.slang -- `NoApiShared` is imported by both, so the two kernels differ
// only in where the scene colour, the exposure value, and the histogram come from.
//
// Binding surface: one root block at buffer 0 (the compute root index) carrying the scene colour's
// bindless slot, the table's address, and the addresses of the previous frame's exposure value and
// of the histogram. Nothing is bound.
import NoApiShared;
import NoApiBindless;

struct HistAccumulateRoot
{
    uint4 slots;                              // x scene colour slot; yzw unused
    float4 range;                             // x logLuminanceMin, y logLuminanceMax
    ConstBufferPointer<TextureSlot> textures; // The one bindless table, viewed as 2D textures.
    ConstBufferPointer<float> exposure;       // R5, the previous frame's resolved exposure.
    Ptr<Atomic<uint>> histogram;              // R4, accumulated into with device atomics.
    uint2 padding;
};

[[vk::binding(0, 0)]]
ConstantBuffer<HistAccumulateRoot> gRoot : register(b0); // → MSL [[buffer(0)]]

static const uint kHistogramBins = 256;
// The representative graph fixes the scene extent (spec section 6).
static const uint kSceneExtent = 1024;

[shader("compute")]
[numthreads(8, 8, 1)]
void computeHistAccumulate(uint3 tid: SV_DispatchThreadID)
{
    if (tid.x >= kSceneExtent || tid.y >= kSceneExtent)
    {
        return;
    }

    Texture2D<float4> sceneColor = gRoot.textures[gRoot.slots.x].handle;

    float preExposure = gRoot.exposure[0];
    float3 displayedColor = sceneColor.Load(int3(int2(tid.xy), 0)).rgb;
    float sceneLuminance = SceneReferredLuminance(displayedColor, preExposure);
    float logLuminance = LogLuminance(sceneLuminance);
    uint bin = HistogramBin(logLuminance, gRoot.range.x, gRoot.range.y, kHistogramBins);

    gRoot.histogram[bin].add(1u);
}
