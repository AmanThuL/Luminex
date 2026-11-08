// Safe-math fixed-slot visibility; explicit resources keep reflection exact.
import RojoRHI;
import VisibilityRoots;
import Visibility;
import SceneTables;
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
    if (!valid) return;
    CandidateRecord record = (rojoRHI::load<CandidateRecord>(root.candidatesAddr, candidate));
    RunRecord run = (rojoRHI::load<RunRecord>(root.runsAddr, record.run));
    uint view = viewForCandidate(candidate);

    uint position = candidate;
    bool fits = position < params.rowCapacity && run.argumentIndex < params.argumentCapacity;
    if (candidate < params.rowCapacity &&
        ((rojoRHI::load<InstanceRow>(root.instancesAddr, record.instanceRow)).flags & kInstanceDisabled) == 0u)
        rojoRHI::store<uint>(root.rowsAddr, candidate, record.instanceRow);
    if (retained != 0u)
    {
        if (fits) { rojoRHI::store<uint>(root.rowsAddr, position, record.instanceRow); count(view, 7u, 1u); }
        else count(view, 9u, 1u);
    }
    MeshRow mesh = (rojoRHI::load<MeshRow>(root.meshesAddr, (rojoRHI::load<InstanceRow>(root.instancesAddr, record.instanceRow)).meshRow));
    {
        writeArgument(run.argumentIndex, candidate, fits ? retained : 0u, mesh);
        if (retained != 0u) count(view, run.argumentIndex < params.argumentCapacity ? 8u : 10u, fits ? 1u : (run.argumentIndex >= params.argumentCapacity ? 1u : 0u));
    }
}
