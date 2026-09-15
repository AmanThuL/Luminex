import SceneTables;

[[vk::binding(0, 0)]]
RWStructuredBuffer<uint> gOutput : register(u0);
[[vk::binding(5, 0)]]
StructuredBuffer<InstanceRow> gInstances : register(t5);
[[vk::binding(6, 0)]]
StructuredBuffer<MaterialRow> gMaterials : register(t6);
[[vk::binding(7, 0)]]
StructuredBuffer<MeshRow> gMeshes : register(t7);

[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain(uint3 tid : SV_DispatchThreadID)
{
    InstanceRow instance = gInstances[tid.x];
    MaterialRow material = gMaterials[tid.x];
    MeshRow mesh = gMeshes[tid.x];
    uint base = tid.x * 84;
    for (uint column = 0; column < 4; ++column)
    {
        for (uint row = 0; row < 4; ++row)
        {
            uint offset = column * 4 + row;
            gOutput[base + offset] = asuint(instance.model[row][column]);
            gOutput[base + 16 + offset] = asuint(instance.previousModel[row][column]);
            gOutput[base + 32 + offset] = asuint(instance.normalMatrix[row][column]);
            gOutput[base + 52 + offset] = asuint(material.uvTransform[row][column]);
        }
    }
    gOutput[base + 48] = instance.meshRow;
    gOutput[base + 49] = instance.materialRow;
    gOutput[base + 50] = instance.flags;
    gOutput[base + 51] = asuint(instance.emissiveScale);
    for (uint component = 0; component < 4; ++component)
        gOutput[base + 68 + component] = asuint(material.albedo[component]);
    for (uint component = 0; component < 3; ++component)
        gOutput[base + 72 + component] = asuint(material.emissive[component]);
    gOutput[base + 75] = asuint(material.roughness);
    gOutput[base + 76] = asuint(material.metallic);
    gOutput[base + 77] = asuint(material.occlusionStrength);
    gOutput[base + 78] = asuint(material.alphaCutoff);
    gOutput[base + 79] = material.flags;
    gOutput[base + 80] = mesh.firstIndex;
    gOutput[base + 81] = mesh.indexCount;
    gOutput[base + 82] = mesh.firstVertex;
    gOutput[base + 83] = mesh.vertexCount;
}
