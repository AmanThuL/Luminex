// One pass writes depth and one writes the unchanged shadow-filter result.
import RojoRHI;
import OracleRoots;
import Shadow;

struct Vertex
{
    float3 position;
};

struct VSOutput
{
    float4 position : SV_Position;
    // Keep shadow coordinates homogeneous until fragment interpolation completes.
    float4 shadowPos : POSITION0;
};

[shader("vertex")]
VSOutput vertexMain(rojoRHI::VertexIds ids)
{
    ShadowSmokeRoot root = rojoRHI::root<ShadowSmokeRoot>();
    ShadowSmokeUniforms uniforms = rojoRHI::load<ShadowSmokeUniforms>(root.uniforms, 0);
    Vertex vertex = rojoRHI::load<Vertex>(root.vertices, rojoRHI::vertexIndex(ids));
    float4 positionW = float4(vertex.position, 1.0);
    VSOutput o;
    o.position = mul(uniforms.lightViewProj, positionW);
    o.shadowPos = mul(uniforms.shadowTransform, positionW);
    return o;
}

[shader("fragment")]
void fragmentDepthOnly()
{
}

[shader("fragment")]
float4 fragmentMain(VSOutput in): SV_Target
{
    ShadowSmokeRoot root = rojoRHI::root<ShadowSmokeRoot>();
    ShadowSmokeUniforms uniforms = rojoRHI::load<ShadowSmokeUniforms>(root.uniforms, 0);
    float shadow = CalcShadowFactor(in.shadowPos, rojoRHI::texture2D(root.shadow), rojoRHI::comparisonSampler(root.sampler), uniforms.filter);
    return float4(shadow, shadow, shadow, 1.0);
}
