// Deterministic inclusive scan shared by visibility threadgroups.
groupshared uint gPrefix[256];
uint scanPrefix(uint lane, uint value)
{
    gPrefix[lane] = value;
    GroupMemoryBarrierWithGroupSync();
    for (uint step = 1u; step < 256u; step *= 2u)
    {
        uint prior = lane >= step ? gPrefix[lane - step] : 0u;
        GroupMemoryBarrierWithGroupSync();
        gPrefix[lane] += prior;
        GroupMemoryBarrierWithGroupSync();
    }
    return gPrefix[lane];
}
