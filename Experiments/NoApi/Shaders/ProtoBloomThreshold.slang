// P07 bloom.threshold's address-first frontend (spec section 6), over the same NoApiShared math the
// incumbent's BloomThreshold.slang uses.
//
// Binding surface: one root block at buffer 0 carrying the scene colour's sampled slot, the bloom
// mip's storage slot, and the table's address under both views.
import NoApiShared;
import NoApiBindless;

struct BloomThresholdRoot
{
    uint4 slots;    // x scene colour slot, y destination storage slot
    float4 params;  // x threshold
    ConstBufferPointer<TextureSlot> textures;
    ConstBufferPointer<StorageSlot> images;
};

[[vk::binding(0, 0)]]
ConstantBuffer<BloomThresholdRoot> gRoot : register(b0); // → MSL [[buffer(0)]]

// The representative graph fixes both extents (spec section 6): scene 1024x1024, bloom mip 0
// 512x512.
static const uint2 kSrcExtent = uint2(1024, 1024);
static const uint2 kDstExtent = uint2(512, 512);

//======================================================================================================================
float3 loadClamped(Texture2D<float4> source, uint2 coord)
{
    uint2 clamped = min(coord, kSrcExtent - uint2(1, 1));
    return source.Load(int3(int2(clamped), 0)).rgb;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void computeBloomThreshold(uint3 tid: SV_DispatchThreadID)
{
    if (tid.x >= kDstExtent.x || tid.y >= kDstExtent.y)
    {
        return;
    }

    Texture2D<float4> sceneColor = gRoot.textures[gRoot.slots.x].handle;
    RWTexture2D<float4> destination = gRoot.images[gRoot.slots.y].handle;

    uint2 base = tid.xy * 2u;
    float3 color = (loadClamped(sceneColor, base) + loadClamped(sceneColor, base + uint2(1, 0)) +
                    loadClamped(sceneColor, base + uint2(0, 1)) +
                    loadClamped(sceneColor, base + uint2(1, 1))) *
                   0.25;

    destination[tid.xy] = float4(BloomThresholdContribution(color, gRoot.params.x), 1.0);
}
