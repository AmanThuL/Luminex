// FrameDataBench oracle (Benchmarks/FrameData): a solid-colour quad placed and tinted by one
// per-draw parameter block delivered at buffer slot 0. Dynamic workloads deliver that block through
// deliverPerDrawData every draw; static workloads bind an already-created, unwritten buffer at the
// same slot. Only these leading 32 bytes are ever read -- a dynamic workload's larger per-draw block
// carries trailing padding this shader never touches.

struct QuadParams
{
    float2 offset;     // NDC-space quad center.
    float2 halfExtent; // NDC-space quad half-size.
    float4 tint;       // Solid quad colour.
};

[[vk::binding(0, 0)]]
ConstantBuffer<QuadParams> gParams : register(b0); // -> MSL [[buffer(0)]]

struct VSOutput
{
    float4 position : SV_Position;
};

// vid in [0,6): a two-triangle quad via a bare vertex id -- no vertex buffer needed.
[shader("vertex")]
VSOutput vertexMain(uint vid: SV_VertexID)
{
    float2 corners[4] = {
        float2(-1.0, -1.0), float2(1.0, -1.0), float2(1.0, 1.0), float2(-1.0, 1.0)
    };
    uint indices[6] = { 0, 1, 2, 0, 2, 3 };
    float2 corner = corners[indices[vid]];
    VSOutput o;
    o.position = float4(gParams.offset + corner * gParams.halfExtent, 0.0, 1.0);
    return o;
}

[shader("fragment")]
float4 fragmentMain(VSOutput in): SV_Target
{
    return gParams.tint;
}
