// Fixed GPU visibility ABI. Absolute indices address combined scene-then-shadow tables.
import SceneTables;
struct CandidateRecord { uint instanceRow; uint run; };
struct RunRecord { uint firstCandidate; uint candidateCount; uint firstSlot; uint argumentIndex; };
struct ChunkRecord { uint run; uint firstCandidate; uint candidateCount; uint padding; };
struct VisibilityViewParams
{
    float4 planes[5];
    uint flags; uint firstCandidate; uint candidateCount; uint firstRun;
    uint runCount; uint firstChunk; uint chunkCount; uint padding;
};
struct VisibilityParams
{
    uint layout; uint rowCapacity; uint argumentCapacity; uint stateCapacity;
    uint candidateCount; uint viewCount; uint2 padding;
};
uint classifyBounds(InstanceRow row, VisibilityViewParams view)
{
    if ((view.flags & 1u) == 0u) return 2u | (1u << 2u);
    if ((view.flags & 2u) != 0u) return 2u | (2u << 2u);
    for (uint r = 0u; r < 4u; ++r)
        if (!all(isfinite(row.model[r]))) return 2u | (4u << 2u);
    if ((view.flags & 4u) == 0u || (row.flags & kInstanceBoundsUnreliable) != 0u ||
        !all(isfinite(row.worldBoundsMin)) || !all(isfinite(row.worldBoundsMax)) ||
        any(row.worldBoundsMin > row.worldBoundsMax)) return 2u | (3u << 2u);
    for (uint p = 0u; p < 5u; ++p)
    {
        float4 plane = view.planes[p];
        float3 positive = select(plane.xyz >= 0.0f, row.worldBoundsMax, row.worldBoundsMin);
        float x = plane.x * positive.x;
        float y = plane.y * positive.y;
        float z = plane.z * positive.z;
        float xy = x + y;
        float xyz = xy + z;
        float distance = xyz + plane.w;
        if (distance < 0.0f) return 1u;
    }
    return 0u;
}
