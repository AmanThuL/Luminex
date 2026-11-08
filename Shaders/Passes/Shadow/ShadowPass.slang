import RojoRHI;
import SceneTables;
import ShadowRoots;
import VertexStorage;

[shader("vertex")]
float4 vertexMain(rojoRHI::VertexIds ids): SV_Position
{
    ShadowDrawRoot draw = rojoRHI::root<ShadowDrawRoot>();
    ShadowPassRoot pass = rojoRHI::load<ShadowPassRoot>(draw.pass, 0);
    uint firstEntry = draw.indirect != 0 ? rojoRHI::drawId(ids) : draw.firstEntry;
    uint instanceRow = rojoRHI::load<uint>(pass.visibleRows, firstEntry + rojoRHI::instanceIndex(ids));
    InstanceRow instance = rojoRHI::load<InstanceRow>(pass.instances, instanceRow);
    ShadowPassUniforms uniforms = rojoRHI::load<ShadowPassUniforms>(pass.uniforms, 0);
    Vertex vertex = loadVertex(pass.vertices, rojoRHI::vertexIndex(ids));
    return mul(uniforms.lightViewProj, mul(instance.model, float4(vertex.position, 1.0)));
}

[shader("fragment")]
void fragmentMain()
{
}
