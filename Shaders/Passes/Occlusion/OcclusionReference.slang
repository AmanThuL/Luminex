// Independent candidate selection and byte IDs share scene transforms and alpha coverage.
import RojoRHI;
import OcclusionRoots;
import AlphaMask;
import SceneTables;
import VertexStorage;

struct VSOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};
[shader("vertex")]
VSOutput vertexMain(rojoRHI::VertexIds ids)
{
    OcclusionReferenceRoot root = rojoRHI::root<OcclusionReferenceRoot>();
    ReferenceParams gReference = rojoRHI::load<ReferenceParams>(root.paramsAddr, 0);
    Vertex vertex = loadVertex(root.verticesAddr, rojoRHI::vertexIndex(ids));
    InstanceRow instance = rojoRHI::load<InstanceRow>(root.instancesAddr, gReference.instanceRow);
    MaterialRow material = rojoRHI::load<MaterialRow>(root.materialsAddr, instance.materialRow);
    float4 positionW = mul(instance.model, float4(vertex.position, 1.0));
    VSOutput output;
    output.position = mul(gReference.viewProjection, positionW);
    output.uv = mul(material.uvTransform, float4(vertex.uv, 0.0, 1.0)).xy;
    return output;
}
float4 byteId()
{
    OcclusionReferenceRoot root = rojoRHI::root<OcclusionReferenceRoot>();
    ReferenceParams gReference = rojoRHI::load<ReferenceParams>(root.paramsAddr, 0);
    uint id = gReference.instanceRow + 1;
    return float4(id & 255, (id >> 8) & 255, (id >> 16) & 255, (id >> 24) & 255) / 255.0;
}
[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    return byteId();
}
[shader("fragment")]
float4 fragmentMask(VSOutput input) : SV_Target
{
    OcclusionReferenceRoot root = rojoRHI::root<OcclusionReferenceRoot>();
    ReferenceParams gReference = rojoRHI::load<ReferenceParams>(root.paramsAddr, 0);
    InstanceRow instance = rojoRHI::load<InstanceRow>(root.instancesAddr, gReference.instanceRow);
    MaterialRow material = rojoRHI::load<MaterialRow>(root.materialsAddr, instance.materialRow);
    applyAlphaMask(rojoRHI::texture2D(root.diffuse).Sample(rojoRHI::sampler(root.sampler), input.uv).a * material.albedo.a,
                   material.alphaCutoff);
    return byteId();
}
