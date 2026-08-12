// M5.1 Stage 4 S-BIND oracle (spec section 7), maintained-RHI side: a per-draw quad sampling one of
// 256 unique textures, tinted by a per-draw uniform block. Vertex data is a bare vertex id (a fixed
// unit quad, no vertex buffer needed); per-draw placement and tint are the QuadParams block below,
// delivered through CommandList::setUniforms every draw -- exactly the per-draw root/uniform block
// S-BIND's frozen definition names.

struct QuadParams
{
    float2 offset;     // NDC-space quad center.
    float2 halfExtent; // NDC-space quad half-size.
    float4 tint;       // Multiplies the sampled texel; distinguishes draw i from its neighbours.
};

[[vk::binding(0, 0)]]
Texture2D<float4> gTexture : register(t0); // -> MSL [[texture(0)]]

[[vk::binding(0, 1)]]
SamplerState gSampler : register(s0); // -> MSL [[sampler(0)]]

[[vk::binding(0, 2)]]
ConstantBuffer<QuadParams> gParams : register(b2); // -> MSL [[buffer(2)]]

struct VSOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

// vid in [0,6): a two-triangle quad (0,1,2)(0,2,3) via a bare vertex id -- no vertex buffer needed.
[shader("vertex")]
VSOutput vertexQuad(uint vid: SV_VertexID)
{
    float2 corners[4] = {
        float2(-1.0, -1.0), float2(1.0, -1.0), float2(1.0, 1.0), float2(-1.0, 1.0)
    };
    uint indices[6] = { 0, 1, 2, 0, 2, 3 };
    float2 corner = corners[indices[vid]];
    VSOutput o;
    o.position = float4(gParams.offset + corner * gParams.halfExtent, 0.0, 1.0);
    o.uv = corner * 0.5 + 0.5;
    return o;
}

[shader("fragment")]
float4 fragmentQuad(VSOutput in): SV_Target
{
    return gTexture.Sample(gSampler, in.uv) * gParams.tint;
}
