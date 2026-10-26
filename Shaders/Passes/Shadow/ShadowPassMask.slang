import RojoRHI;
import SceneTables;
import ShadowRoots;
import VertexStorage;
import AlphaMask;

struct VSOutput
{
    nointerpolation uint instanceRow : TEXCOORD1;
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VSOutput vertexMain(rojoRHI::VertexIds ids)
{
    ShadowDrawRoot draw = rojoRHI::root<ShadowDrawRoot>();
    ShadowPassRoot pass = rojoRHI::load<ShadowPassRoot>(draw.pass, 0);
    uint firstEntry = draw.indirect != 0 ? rojoRHI::drawId(ids) : draw.firstEntry;
    Vertex vertex = loadVertex(pass.vertices, rojoRHI::vertexIndex(ids));
    VSOutput output;
    output.instanceRow = rojoRHI::load<uint>(pass.visibleRows, firstEntry + rojoRHI::instanceIndex(ids));
    InstanceRow instance = rojoRHI::load<InstanceRow>(pass.instances, output.instanceRow);
    MaterialRow material = rojoRHI::load<MaterialRow>(pass.materials, instance.materialRow);
    ShadowPassUniforms uniforms = rojoRHI::load<ShadowPassUniforms>(pass.uniforms, 0);
    output.position = mul(uniforms.lightViewProj, mul(instance.model, float4(vertex.position, 1.0)));
    output.uv = mul(material.uvTransform, float4(vertex.uv, 0.0, 1.0)).xy;
    return output;
}

[shader("fragment")]
void fragmentMain(VSOutput input)
{
    ShadowDrawRoot draw = rojoRHI::root<ShadowDrawRoot>();
    ShadowPassRoot pass = rojoRHI::load<ShadowPassRoot>(draw.pass, 0);
    InstanceRow instance = rojoRHI::load<InstanceRow>(pass.instances, input.instanceRow);
    MaterialRow material = rojoRHI::load<MaterialRow>(pass.materials, instance.materialRow);
    applyAlphaMask(rojoRHI::texture2D(draw.baseColor).Sample(rojoRHI::sampler(pass.linearSampler), input.uv).a * material.albedo.a,
                   material.alphaCutoff);
}
