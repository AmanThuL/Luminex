// P08/P09 bloom.down{1,2}'s shared address-first frontend (spec section 6), over the same
// NoApiShared math the incumbent's BloomDown.slang uses. One pipeline serves both passes; the
// per-pass source and destination are two slot indices in the root block rather than two rebound
// storage-texture views.
import NoApiShared;
import NoApiBindless;

struct BloomDownRoot
{
    uint4 extents; // x srcWidth, y srcHeight, z dstWidth, w dstHeight
    uint4 slots;   // x source storage slot, y destination storage slot
    ConstBufferPointer<StorageSlot> images;
    uint2 padding;
};

[[vk::binding(0, 0)]]
ConstantBuffer<BloomDownRoot> gRoot : register(b0); // → MSL [[buffer(0)]]

//======================================================================================================================
float4 loadClamped(RWTexture2D<float4> source, uint2 coord, uint2 extent)
{
    return source[min(coord, extent - uint2(1, 1))];
}

[shader("compute")]
[numthreads(8, 8, 1)]
void computeBloomDown(uint3 tid: SV_DispatchThreadID)
{
    if (tid.x >= gRoot.extents.z || tid.y >= gRoot.extents.w)
    {
        return;
    }

    RWTexture2D<float4> source = gRoot.images[gRoot.slots.x].handle;
    RWTexture2D<float4> destination = gRoot.images[gRoot.slots.y].handle;

    uint2 srcExtent = uint2(gRoot.extents.x, gRoot.extents.y);
    uint2 base = tid.xy * 2u;
    destination[tid.xy] = BoxFilter4(loadClamped(source, base, srcExtent),
                                     loadClamped(source, base + uint2(1, 0), srcExtent),
                                     loadClamped(source, base + uint2(0, 1), srcExtent),
                                     loadClamped(source, base + uint2(1, 1), srcExtent));
}
