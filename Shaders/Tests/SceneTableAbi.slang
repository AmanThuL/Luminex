import RojoRHI;
import SceneTables;
import OracleRoots;

[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain(uint3 tid : SV_DispatchThreadID)
{
    SceneTableAbiRoot root = rojoRHI::root<SceneTableAbiRoot>();
    InstanceRow instance = rojoRHI::load<InstanceRow>(root.instances, tid.x);
    MaterialRow material = rojoRHI::load<MaterialRow>(root.materials, tid.x);
    MeshRow mesh = rojoRHI::load<MeshRow>(root.meshes, tid.x);
    LightRow light = rojoRHI::load<LightRow>(root.lights, tid.x);
    uint base = tid.x * 116;
    for (uint column = 0; column < 4; ++column)
    {
        for (uint row = 0; row < 4; ++row)
        {
            uint offset = column * 4 + row;
            rojoRHI::store<uint>(root.output, base + offset, asuint(instance.model[row][column]));
            rojoRHI::store<uint>(root.output, base + 16 + offset, asuint(instance.previousModel[row][column]));
            rojoRHI::store<uint>(root.output, base + 32 + offset, asuint(instance.normalMatrix[row][column]));
            rojoRHI::store<uint>(root.output, base + 60 + offset, asuint(material.uvTransform[row][column]));
        }
    }
    rojoRHI::store<uint>(root.output, base + 48, instance.meshRow);
    rojoRHI::store<uint>(root.output, base + 49, instance.materialRow);
    rojoRHI::store<uint>(root.output, base + 50, instance.flags);
    rojoRHI::store<uint>(root.output, base + 51, asuint(instance.emissiveScale));
    for (uint component = 0; component < 3; ++component) {
        rojoRHI::store<uint>(root.output, base + 52 + component, asuint(instance.worldBoundsMin[component]));
        rojoRHI::store<uint>(root.output, base + 56 + component, asuint(instance.worldBoundsMax[component]));
        rojoRHI::store<uint>(root.output, base + 92 + component, asuint(mesh.boundsMin[component]));
        rojoRHI::store<uint>(root.output, base + 96 + component, asuint(mesh.boundsMax[component]));
    }
    rojoRHI::store<uint>(root.output, base + 55, instance.boundsPadding0);
    rojoRHI::store<uint>(root.output, base + 59, instance.boundsPadding1);
    rojoRHI::store<uint>(root.output, base + 95, mesh.boundsPadding0);
    rojoRHI::store<uint>(root.output, base + 99, mesh.boundsPadding1);
    for (uint component = 0; component < 4; ++component)
        rojoRHI::store<uint>(root.output, base + 76 + component, asuint(material.albedo[component]));
    for (uint component = 0; component < 3; ++component)
        rojoRHI::store<uint>(root.output, base + 80 + component, asuint(material.emissive[component]));
    rojoRHI::store<uint>(root.output, base + 83, asuint(material.roughness));
    rojoRHI::store<uint>(root.output, base + 84, asuint(material.metallic));
    rojoRHI::store<uint>(root.output, base + 85, asuint(material.occlusionStrength));
    rojoRHI::store<uint>(root.output, base + 86, asuint(material.alphaCutoff));
    rojoRHI::store<uint>(root.output, base + 87, material.flags);
    rojoRHI::store<uint>(root.output, base + 88, mesh.firstIndex);
    rojoRHI::store<uint>(root.output, base + 89, mesh.indexCount);
    rojoRHI::store<uint>(root.output, base + 90, mesh.firstVertex);
    rojoRHI::store<uint>(root.output, base + 91, mesh.vertexCount);
    for (uint component = 0; component < 3; ++component)
    {
        rojoRHI::store<uint>(root.output, base + 100 + component, asuint(light.position[component]));
        rojoRHI::store<uint>(root.output, base + 104 + component, asuint(light.strength[component]));
        rojoRHI::store<uint>(root.output, base + 108 + component, asuint(light.direction[component]));
        rojoRHI::store<uint>(root.output, base + 112 + component, asuint(light.boundCentre[component]));
    }
    rojoRHI::store<uint>(root.output, base + 103, asuint(light.range));
    rojoRHI::store<uint>(root.output, base + 107, asuint(light.spotScale));
    rojoRHI::store<uint>(root.output, base + 111, asuint(light.spotOffset));
    rojoRHI::store<uint>(root.output, base + 115, asuint(light.boundRadius));
}
