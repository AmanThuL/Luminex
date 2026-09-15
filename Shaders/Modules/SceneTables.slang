// Shared CPU/Slang scene-row ABI. Entries own resources and binding declarations.
static const uint kInstanceMotionInvalid = 1u;
static const uint kMaterialHasNormalMap = 1u;
static const uint kMaterialMasked = 2u;
static const uint kMaterialDoubleSided = 4u;
static const uint kSceneInstancesSlot = 5u;
static const uint kSceneMaterialsSlot = 6u;
static const uint kSceneMeshesSlot = 7u;
static const uint kDrawUniformsSlot = 1u;

struct InstanceRow
{
    float4x4 model;
    float4x4 previousModel;
    float4x4 normalMatrix;
    uint meshRow;
    uint materialRow;
    uint flags;
    float emissiveScale;
};

struct MaterialRow
{
    float4x4 uvTransform;
    float4 albedo;
    float3 emissive;
    float roughness;
    float metallic;
    float occlusionStrength;
    float alphaCutoff;
    uint flags;
};

struct MeshRow
{
    uint firstIndex;
    uint indexCount;
    uint firstVertex;
    uint vertexCount;
};

struct DrawUniforms
{
    uint instanceRow;
    uint padding0;
    uint padding1;
    uint padding2;
};
