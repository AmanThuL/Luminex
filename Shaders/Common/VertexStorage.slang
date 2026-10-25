module VertexStorage;
import RojoRHI;

public struct VertexStorage
{
    public float px;
    public float py;
    public float pz;
    public float nx;
    public float ny;
    public float nz;
    public float tx;
    public float ty;
    public float tz;
    public float tw;
    public float u;
    public float v;
};

public struct Vertex
{
    public float3 position;
    public float3 normal;
    public float4 tangent;
    public float2 uv;
};

public Vertex loadVertex(rojoRHI::Addr vertices, uint index)
{
    VertexStorage stored = rojoRHI::load<VertexStorage>(vertices, index);
    Vertex vertex;
    vertex.position = float3(stored.px, stored.py, stored.pz);
    vertex.normal = float3(stored.nx, stored.ny, stored.nz);
    vertex.tangent = float4(stored.tx, stored.ty, stored.tz, stored.tw);
    vertex.uv = float2(stored.u, stored.v);
    return vertex;
}
