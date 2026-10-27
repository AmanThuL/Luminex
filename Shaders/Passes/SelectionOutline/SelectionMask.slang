// Editor selection coverage uses the same alpha rule as scene and shadow rendering.
import RojoRHI;
import AlphaMask;
import SceneTables;
import VertexStorage;
import SelectionOutlineRoots;

struct VSOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};
[shader("vertex")]
VSOutput vertexMain(rojoRHI::VertexIds ids)
{
    uint vid = rojoRHI::vertexIndex(ids);
    SelectionMaskRoot gRoot = rojoRHI::root<SelectionMaskRoot>();
    MaskParams gMask = rojoRHI::load<MaskParams>(gRoot.paramsAddr, 0);
    Vertex vertex = loadVertex(gRoot.verticesAddr, vid);
    VSOutput output;
    output.position = mul(gMask.viewProj, mul(rojoRHI::load<InstanceRow>(gRoot.instancesAddr, gMask.instanceRow).model, float4(vertex.position, 1.0)));
    output.uv = mul(rojoRHI::load<MaterialRow>(gRoot.materialsAddr, rojoRHI::load<InstanceRow>(gRoot.instancesAddr, gMask.instanceRow).materialRow).uvTransform, float4(vertex.uv, 0.0, 1.0)).xy;
    return output;
}
[shader("fragment")]
float fragmentMain(VSOutput input): SV_Target
{
    SelectionMaskRoot gRoot = rojoRHI::root<SelectionMaskRoot>();
    MaskParams gMask = rojoRHI::load<MaskParams>(gRoot.paramsAddr, 0);
    if ((rojoRHI::load<MaterialRow>(gRoot.materialsAddr, rojoRHI::load<InstanceRow>(gRoot.instancesAddr, gMask.instanceRow).materialRow).flags & kMaterialMasked) != 0)
        applyAlphaMask(rojoRHI::texture2D(gRoot.diffuse).Sample(rojoRHI::sampler(gRoot.sampler), input.uv).a * rojoRHI::load<MaterialRow>(gRoot.materialsAddr, rojoRHI::load<InstanceRow>(gRoot.instancesAddr, gMask.instanceRow).materialRow).albedo.a,
                       rojoRHI::load<MaterialRow>(gRoot.materialsAddr, rojoRHI::load<InstanceRow>(gRoot.instancesAddr, gMask.instanceRow).materialRow).alphaCutoff);
    return gMask.selected;
}

// Scene visibility uses exactly the same rasterization and alpha coverage as selected geometry.
[shader("fragment")]
void depthMain(VSOutput input)
{
    SelectionMaskRoot gRoot = rojoRHI::root<SelectionMaskRoot>();
    MaskParams gMask = rojoRHI::load<MaskParams>(gRoot.paramsAddr, 0);
    if ((rojoRHI::load<MaterialRow>(gRoot.materialsAddr, rojoRHI::load<InstanceRow>(gRoot.instancesAddr, gMask.instanceRow).materialRow).flags & kMaterialMasked) != 0)
        applyAlphaMask(rojoRHI::texture2D(gRoot.diffuse).Sample(rojoRHI::sampler(gRoot.sampler), input.uv).a * rojoRHI::load<MaterialRow>(gRoot.materialsAddr, rojoRHI::load<InstanceRow>(gRoot.instancesAddr, gMask.instanceRow).materialRow).albedo.a,
                       rojoRHI::load<MaterialRow>(gRoot.materialsAddr, rojoRHI::load<InstanceRow>(gRoot.instancesAddr, gMask.instanceRow).materialRow).alphaCutoff);
}
