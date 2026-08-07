// Depth-tested lambert mesh: vertex pulling from slot 0, per-draw uniforms at slot 1
// (slot indices map 1:1 to MTL4ArgumentTable indices — see shader-style.md).
struct Vertex
{
    float3 position;
    float3 normal;
    float3 color;
};

[[vk::binding(0, 0)]]
StructuredBuffer<Vertex> gVertices : register(t0);  // → MSL [[buffer(0)]]

struct ObjectUniforms
{
    float4x4 mvp;
    float4x4 model;
    float4 baseColor;
};

// b1, not b0: Slang's Metal target maps the register *index* straight to the MSL buffer index and
// ignores the register class, so `t0` + `b0` both ask for [[buffer(0)]] and slangc's output fails
// to compile ("cannot reserve 'buffer' resource location at index 0" — measured, Task 8). The
// register index is therefore the MTL4ArgumentTable slot; D3D/Vulkan keep their own spaces via the
// class letter and [[vk::binding]].
[[vk::binding(1, 0)]]
ConstantBuffer<ObjectUniforms> gObject : register(b1);  // → MSL [[buffer(1)]]

static const float3 kLightDir = normalize(float3(0.4, 1.0, 0.3));
static const float kAmbient = 0.25;

struct VSOutput
{
    float4 position : SV_Position;
    float3 normal : NORMAL0;
    float3 color : COLOR0;
};

[shader("vertex")]
VSOutput vertexMain(uint vid: SV_VertexID)
{
    Vertex v = gVertices[vid];
    VSOutput o;
    o.position = mul(gObject.mvp, float4(v.position, 1.0));
    // Rotation/uniform-scale only in M2 scenes, so the model matrix's upper 3x3 is a valid
    // normal transform; revisit with non-uniform scale.
    o.normal = normalize(mul((float3x3)gObject.model, v.normal));
    o.color = v.color * gObject.baseColor.rgb;
    return o;
}

[shader("fragment")]
float4 fragmentMain(VSOutput in): SV_Target
{
    float ndotl = max(dot(normalize(in.normal), kLightDir), 0.0);
    return float4(in.color * (kAmbient + (1.0 - kAmbient) * ndotl), 1.0);
}
