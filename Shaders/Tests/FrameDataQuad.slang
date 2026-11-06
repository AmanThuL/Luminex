// FrameDataBench oracle (Benchmarks/FrameData): a solid-colour quad placed and tinted by one
// per-draw parameter block the root addresses. Dynamic workloads upload that block as frame data
// every draw; static workloads address a buffer created once before timing. Only the block's
// leading 32 bytes are ever read -- a dynamic workload's larger block carries trailing padding this
// shader never touches.
import RojoRHI;

struct QuadParams
{
    float2 offset;     // NDC-space quad center.
    float2 halfExtent; // NDC-space quad half-size.
    float4 tint;       // Solid quad colour.
};

struct FrameDataQuadRoot
{
    rojoRHI::Addr params; // -> QuadParams
};

struct VSOutput
{
    float4 position : SV_Position;
};

// vid in [0,6): a two-triangle quad via a bare vertex index -- no vertex buffer needed.
[shader("vertex")]
VSOutput vertexMain(rojoRHI::VertexIds ids)
{
    QuadParams params = rojoRHI::load<QuadParams>(rojoRHI::root<FrameDataQuadRoot>().params, 0);
    float2 corners[4] = {
        float2(-1.0, -1.0), float2(1.0, -1.0), float2(1.0, 1.0), float2(-1.0, 1.0)
    };
    uint indices[6] = { 0, 1, 2, 0, 2, 3 };
    float2 corner = corners[indices[rojoRHI::vertexIndex(ids)]];
    VSOutput o;
    o.position = float4(params.offset + corner * params.halfExtent, 0.0, 1.0);
    return o;
}

[shader("fragment")]
float4 fragmentMain(VSOutput in): SV_Target
{
    return rojoRHI::load<QuadParams>(rojoRHI::root<FrameDataQuadRoot>().params, 0).tint;
}
