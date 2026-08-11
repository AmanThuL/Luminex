// P10/P11 bloom.up{1,2}'s shared address-first frontend (spec section 6), over the same NoApiShared
// math the incumbent's BloomUp.slang uses. One pipeline serves both passes; base, small, and
// destination are three slot indices in the root block.
import NoApiShared;
import NoApiBindless;

struct BloomUpRoot
{
    uint4 extents; // x smallWidth, y smallHeight, z dstWidth, w dstHeight
    uint4 slots;   // x base storage slot, y small storage slot, z destination storage slot
    ConstBufferPointer<StorageSlot> images;
    uint2 padding;
};

[[vk::binding(0, 0)]]
ConstantBuffer<BloomUpRoot> gRoot : register(b0); // → MSL [[buffer(0)]]

[shader("compute")]
[numthreads(8, 8, 1)]
void computeBloomUp(uint3 tid: SV_DispatchThreadID)
{
    if (tid.x >= gRoot.extents.z || tid.y >= gRoot.extents.w)
    {
        return;
    }

    RWTexture2D<float4> base = gRoot.images[gRoot.slots.x].handle;
    RWTexture2D<float4> small = gRoot.images[gRoot.slots.y].handle;
    RWTexture2D<float4> destination = gRoot.images[gRoot.slots.z].handle;

    uint2 smallExtent = uint2(gRoot.extents.x, gRoot.extents.y);
    uint2 smallCoord = min(tid.xy / 2u, smallExtent - uint2(1, 1));

    destination[tid.xy] = UpsampleAccumulate(base[tid.xy], small[smallCoord]);
}
