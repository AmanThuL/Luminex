// Fixed GPU visibility ABI. Absolute indices address combined scene-then-shadow tables.
import VisibilityRoots;
import SceneTables;
static const uint kVisibilityCounterWords = 21u;
uint classifyBounds(InstanceRow row, VisibilityViewParams view)
{
    if ((row.flags & kInstanceDisabled) != 0u) return 1u | (6u << 2u);
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
