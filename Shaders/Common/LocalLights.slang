// Direct and clustered selection share one ascending accumulation loop. Omitted terms must be
// exactly zero, so preserving row order preserves the nonzero floating-point summation order.
module LocalLights;

import RojoRHI;
import Lighting;
import SceneTables;
import LocalLightTypes;

// Mirrors lmx::engine::LocalLightMode.
public static const uint kLocalLightModeOff = 0u;
public static const uint kLocalLightModeDirect = 1u;
public static const uint kLocalLightModeClustered = 2u;

// Mirrors lmx::render::kClusterTruncatedBit: bit 31 of a record's count marks a froxel that lost
// at least one intersecting light, so a reader masks it off before iterating.
public static const uint kClusterTruncatedBit = 0x80000000u;

//======================================================================================================================
// The froxel holding a fragment, in the flat order `(slice * gridY + y) * gridX + x`.
//
// The tile divides the pixel index rather than the pixel centre, which is what
// `clusterTileEdges` builds its conservative bounds from; the subtraction is unsigned and the
// clamp then folds a pixel before the active origin into the last tile, exactly as the mirror
// documents. The slice is the lowest `s` whose boundary pair brackets the rasterised depth, and a
// depth no slice claims falls into the open last slice.
//
// The boundary table is read in place, word `sliceDepthWord + s` of `sliceDepthBase`, so `params`'
// own `sliceDepth` is never read. Indexing a loaded copy's array with the loop counter would place
// the whole copy in thread memory, which Metal keeps on the stack for every shaded fragment.
public uint LocalLightCluster(LocalLightParams params, rojoRHI::Addr sliceDepthBase,
                              uint sliceDepthWord, float4 svPosition)
{
    uint pixelX = (uint)svPosition.x - params.activeOriginX;
    uint pixelY = (uint)svPosition.y - params.activeOriginY;
    uint tileX = min(pixelX * params.gridX / params.activeWidth, params.gridX - 1u);
    uint tileY = min(pixelY * params.gridY / params.activeHeight, params.gridY - 1u);

    // Rasterized D32Float depth is within [0, 1], matching clusterSlice's input contract.
    uint slice = params.gridZ - 1u;
    for (uint s = 0u; s < params.gridZ; ++s)
    {
        float nearDepth = rojoRHI::load<float>(sliceDepthBase, sliceDepthWord + s);
        float farDepth = rojoRHI::load<float>(sliceDepthBase, sliceDepthWord + s + 1u);
        if (nearDepth >= svPosition.z && svPosition.z > farDepth)
        {
            slice = s;
            break;
        }
    }
    return (slice * params.gridY + tileY) * params.gridX + tileX;
}

//======================================================================================================================
// The local-light sum for one shaded surface, before pre-exposure. `Off` visits no row and returns
// exact +0, which is the additive identity for every value the scene pass can already hold -- that
// is what lets a scene with no local lights render byte for byte what it rendered before this
// module existed. `paramsAddr` addresses the frame's `LocalLightParams`; `lights`, `grid` and
// `indices` address the light rows, the froxel records and the index lists; a path loads only what
// it selects, so Off loads none and Direct no list.
public float3 AccumulateLocalLights(rojoRHI::Addr paramsAddr, rojoRHI::Addr lights,
                                    rojoRHI::Addr grid, rojoRHI::Addr indices, float4 svPosition,
                                    float3 position, float3 normal, float3 toEye, float3 baseColor,
                                    float3 f0, float metallic, float alpha)
{
    LocalLightParams params = rojoRHI::load<LocalLightParams>(paramsAddr, 0);
    // Conservative isotropic NDF filtering (Tokuyoshi/Kaplanyan 2021, equation 13):
    // https://jcgt.org/published/0010/02/02/ . The normal's pixel footprint broadens only the
    // punctual lobe; authored roughness and the existing directional/IBL paths stay unchanged.
    // Evaluate derivatives before the per-froxel divergent loop, so Direct and Clustered filter
    // the same footprint even when neighbouring pixels visit different light lists.
    float3 normalDx = ddx(normal);
    float3 normalDy = ddy(normal);
    const float variance = 0.15915494f; // 1 / (2 pi), the paper's pixel-filter variance.
    float kernel = min(2.0f * variance * (dot(normalDx, normalDx) + dot(normalDy, normalDy)),
                       0.18f);
    float filteredAlpha = sqrt(saturate(alpha * alpha + kernel));
    bool clustered = params.mode == kLocalLightModeClustered;
    uint offset = 0u;
    uint count = 0u;
    if (params.mode == kLocalLightModeDirect)
    {
        count = params.rowCount;
    }
    else if (clustered)
    {
        ClusterRecord record =
            rojoRHI::load<ClusterRecord>(grid, LocalLightCluster(params, paramsAddr,
                                                                 kLocalLightParamsSliceDepthWord,
                                                                 svPosition));
        offset = record.offset;
        count = record.count & ~kClusterTruncatedBit;
    }

    float3 sum = float3(0.0f);
    for (uint i = 0u; i < count; ++i)
    {
        uint index = clustered ? rojoRHI::load<uint>(indices, offset + i) : i;
        sum += ComputePunctualLight(rojoRHI::load<LightRow>(lights, index), position, normal, toEye,
                                    baseColor, f0, metallic, filteredAlpha);
    }
    return sum;
}
