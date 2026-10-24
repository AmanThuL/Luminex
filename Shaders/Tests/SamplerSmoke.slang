// Sampler smoke oracle using a sampled view and sampler index.
import RojoRHI;
import OracleRoots;

struct VSOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VSOutput vertexMain(rojoRHI::VertexIds ids)
{
    uint vid = rojoRHI::vertexIndex(ids);
    // Three-vertex fullscreen triangle.
    float2 uv = float2((vid << 1) & 2, vid & 2);
    VSOutput o;
    o.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    // UVs span 0..2 so the oracle exercises address modes outside the texture.
    o.uv = uv * 2.0;
    return o;
}

[shader("fragment")]
float4 fragmentMain(VSOutput in): SV_Target
{
    let params = rojoRHI::root<SamplerRoot>();
    return rojoRHI::texture2D<float4>(params.source).Sample(rojoRHI::sampler(params.sampler), in.uv);
}

// Restore 0..1 UVs when probing texture content rather than address behavior.
[shader("fragment")]
float4 fragmentIdentityUv(VSOutput in): SV_Target
{
    let params = rojoRHI::root<SamplerRoot>();
    return rojoRHI::texture2D<float4>(params.source).Sample(rojoRHI::sampler(params.sampler), in.uv * 0.5);
}

// Explicit level 1 bypasses magnification's implicit selection of level 0.
[shader("fragment")]
float4 fragmentMipLevel1(VSOutput in): SV_Target
{
    let params = rojoRHI::root<SamplerRoot>();
    return rojoRHI::texture2D<float4>(params.source).SampleLevel(rojoRHI::sampler(params.sampler), in.uv, 1.0);
}
