// Fullscreen-less triangle fed by a structured buffer bound at slot 0
// (slot indices map 1:1 to MTL4ArgumentTable buffer indices — see shader-style.md).
struct Vertex
{
    float2 position;
    float3 color;
};

StructuredBuffer<Vertex> gVertices;  // buffer slot 0

struct VSOutput
{
    float4 position : SV_Position;
    float3 color : COLOR0;
};

[shader("vertex")]
VSOutput vertexMain(uint vid: SV_VertexID)
{
    Vertex v = gVertices[vid];
    VSOutput o;
    o.position = float4(v.position, 0.0, 1.0);
    o.color = v.color;
    return o;
}

[shader("fragment")]
float4 fragmentMain(VSOutput in): SV_Target
{
    return float4(in.color, 1.0);
}
