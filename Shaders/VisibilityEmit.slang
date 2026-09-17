// Safe-math fixed-slot visibility; explicit resources keep reflection exact.
import Visibility;
import SceneTables;
import VisibilityPrefix;
[[vk::binding(0, 0)]] StructuredBuffer<CandidateRecord> gCandidates : register(t0);
[[vk::binding(1, 0)]] StructuredBuffer<RunRecord> gRuns : register(t1);
[[vk::binding(2, 0)]] StructuredBuffer<ChunkRecord> gChunks : register(t2);
[[vk::binding(3, 0)]] StructuredBuffer<InstanceRow> gInstances : register(t3);
[[vk::binding(4, 0)]] StructuredBuffer<MeshRow> gMeshes : register(t4);
[[vk::binding(5, 0)]] StructuredBuffer<VisibilityViewParams> gViews : register(t5);
[[vk::binding(6, 0)]] ConstantBuffer<VisibilityParams> gParams : register(b6);
[[vk::binding(7, 0)]] RWStructuredBuffer<uint> gStates : register(u7);
[[vk::binding(8, 0)]] RWStructuredBuffer<uint> gCounters : register(u8);
[[vk::binding(10, 0)]] RWStructuredBuffer<uint> gOffsets : register(u10);
[[vk::binding(11, 0)]] RWStructuredBuffer<uint> gRows : register(u11);
[[vk::binding(12, 0)]] RWStructuredBuffer<uint> gArguments : register(u12);
uint viewForCandidate(uint candidate)
{
    return candidate >= gViews[1].firstCandidate ? 1u : 0u;
}
void count(uint view, uint word, uint value)
{
    uint ignored;
    InterlockedAdd(gCounters[view * 20u + word], value, ignored);
}
void writeArgument(uint index, uint first, uint retained, MeshRow mesh)
{
    if (index >= gParams.argumentCapacity) return;
    uint base = index * 5u;
    bool geometry = retained != 0u || gParams.layout != 0u;
    gArguments[base] = geometry ? mesh.indexCount : 0u;
    gArguments[base + 1u] = retained;
    gArguments[base + 2u] = geometry ? mesh.firstIndex : 0u;
    gArguments[base + 3u] = 0u;
    gArguments[base + 4u] = geometry ? first : 0u;
}
[shader("compute")]
[numthreads(256, 1, 1)]
void computeMain(uint3 group : SV_GroupID, uint lane : SV_GroupIndex)
{
    uint totalChunks = gViews[1].firstChunk + gViews[1].chunkCount;
    if (group.x >= totalChunks) return;
    ChunkRecord chunk = gChunks[group.x];
    uint candidate = chunk.firstCandidate + lane;
    bool valid = lane < chunk.candidateCount;
    uint retained = 0u;
    if (valid && candidate < gParams.stateCapacity)
        retained = (gStates[candidate] & 3u) != 1u ? 1u : 0u;
    uint inclusive = scanPrefix(lane, retained);
    if (!valid) return;
    CandidateRecord record = gCandidates[candidate];
    RunRecord run = gRuns[record.run];
    uint view = viewForCandidate(candidate);

    uint position = run.firstSlot + gOffsets[group.x] + inclusive - retained;
    bool fits = position < gParams.rowCapacity && run.argumentIndex < gParams.argumentCapacity;
    if (retained != 0u)
    {
        if (fits) { gRows[position] = record.instanceRow; count(view, 7u, 1u); }
        else count(view, 9u, 1u);
    }
    MeshRow mesh = gMeshes[gInstances[record.instanceRow].meshRow];
    if (lane == chunk.candidateCount - 1u && candidate + 1u == run.firstCandidate + run.candidateCount)
    {
        uint total = gOffsets[group.x] + inclusive;
        uint available = run.firstSlot < gParams.rowCapacity ? gParams.rowCapacity - run.firstSlot : 0u;
        uint emitted = run.argumentIndex < gParams.argumentCapacity ? min(total, available) : 0u;
        writeArgument(run.argumentIndex, run.firstSlot, emitted, mesh);
        if (emitted != 0u) count(view, 8u, 1u);
        if (total != 0u && run.argumentIndex >= gParams.argumentCapacity) count(view, 10u, 1u);
    }
}
