// Test-only oracle for buffer hazards: one kernel that reads a storage buffer and writes a
// derived value into another. Whatever produced the source -- a fillBuffer, an earlier dispatch --
// is only visible here if a barrier ordered it, and the bias makes a stale read distinguishable
// from a correct one at every element.

import RojoRHI;
import OracleRoots;

// One element per thread, so the element count is the dispatch's threadgroup count times 64.
[shader("compute")]
[numthreads(64, 1, 1)]
void computeAddFromBuffer(uint3 tid: SV_DispatchThreadID)
{
    let params = rojoRHI::root<BufferHazardRoot>();
    rojoRHI::store<uint>(params.output, tid.x,
                        rojoRHI::load<uint>(params.source, tid.x) + params.bias);
}
