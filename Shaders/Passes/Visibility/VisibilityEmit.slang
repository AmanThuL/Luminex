// Safe-math fixed-slot visibility; explicit resources keep reflection exact.
import RojoRHI;
import VisibilityRoots;
import Visibility;
import SceneTables;
import VisibilityPrefix;
uint viewForCandidate(uint candidate)
{
    let root = rojoRHI::root<VisibilityEmitRoot>();
    return candidate >= (rojoRHI::load<VisibilityViewParams>(root.viewsAddr, 1)).firstCandidate ? 1u : 0u;
}
void count(uint view, uint word, uint value)
{
    let root = rojoRHI::root<VisibilityEmitRoot>();
    rojoRHI::atomicAdd(root.countersAddr, view * kVisibilityCounterWords + word, value);
}
void writeArgument(uint index, uint first, uint retained, MeshRow mesh)
{
    let root = rojoRHI::root<VisibilityEmitRoot>();
    let params = rojoRHI::load<VisibilityParams>(root.paramsAddr, 0);
    if (index >= params.argumentCapacity) return;
    uint base = index * 6u;
    bool geometry = retained != 0u || params.layout != 0u;
    rojoRHI::store<uint>(root.argumentsAddr, base, geometry ? first : 0u);
    rojoRHI::store<uint>(root.argumentsAddr, base + 1u, geometry ? mesh.indexCount : 0u);
    rojoRHI::store<uint>(root.argumentsAddr, base + 2u, retained);
    rojoRHI::store<uint>(root.argumentsAddr, base + 3u, geometry ? mesh.firstIndex : 0u);
    rojoRHI::store<uint>(root.argumentsAddr, base + 4u, 0u);
    rojoRHI::store<uint>(root.argumentsAddr, base + 5u, 0u);
}
[shader("compute")]
[numthreads(256, 1, 1)]
void computeMain(uint3 group : SV_GroupID, uint lane : SV_GroupIndex)
{
    let root = rojoRHI::root<VisibilityEmitRoot>();
    let params = rojoRHI::load<VisibilityParams>(root.paramsAddr, 0);
    uint totalChunks = (rojoRHI::load<VisibilityViewParams>(root.viewsAddr, 1)).firstChunk + (rojoRHI::load<VisibilityViewParams>(root.viewsAddr, 1)).chunkCount;
    if (group.x >= totalChunks) return;
    ChunkRecord chunk = (rojoRHI::load<ChunkRecord>(root.chunksAddr, group.x));
    uint candidate = chunk.firstCandidate + lane;
    bool valid = lane < chunk.candidateCount;
    uint retained = 0u;
    if (valid && candidate < params.stateCapacity)
        retained = ((rojoRHI::load<uint>(root.statesAddr, candidate)) & 3u) != 1u ? 1u : 0u;
    uint inclusive = scanPrefix(lane, retained);
    if (!valid) return;
    CandidateRecord record = (rojoRHI::load<CandidateRecord>(root.candidatesAddr, candidate));
    RunRecord run = (rojoRHI::load<RunRecord>(root.runsAddr, record.run));
    uint view = viewForCandidate(candidate);

    uint position = run.firstSlot + (rojoRHI::load<uint>(root.offsetsAddr, group.x)) + inclusive - retained;
    bool fits = position < params.rowCapacity && run.argumentIndex < params.argumentCapacity;
    if (retained != 0u)
    {
        if (fits) { rojoRHI::store<uint>(root.rowsAddr, position, record.instanceRow); count(view, 7u, 1u); }
        else count(view, 9u, 1u);
    }
    MeshRow mesh = (rojoRHI::load<MeshRow>(root.meshesAddr, (rojoRHI::load<InstanceRow>(root.instancesAddr, record.instanceRow)).meshRow));
    if (lane == chunk.candidateCount - 1u && candidate + 1u == run.firstCandidate + run.candidateCount)
    {
        uint total = (rojoRHI::load<uint>(root.offsetsAddr, group.x)) + inclusive;
        uint available = run.firstSlot < params.rowCapacity ? params.rowCapacity - run.firstSlot : 0u;
        uint emitted = run.argumentIndex < params.argumentCapacity ? min(total, available) : 0u;
        writeArgument(run.argumentIndex, run.firstSlot, emitted, mesh);
        if (emitted != 0u) count(view, 8u, 1u);
        if (total != 0u && run.argumentIndex >= params.argumentCapacity) count(view, 10u, 1u);
    }
}
