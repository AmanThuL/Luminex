// Shared CPU/Slang scene-row ABI. Entries own resources and binding declarations.
static const uint kInstanceMotionInvalid = 1u;
static const uint kInstanceBoundsUnreliable = 2u;
static const uint kVisibleRowsSlot = 4u;
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
    float3 worldBoundsMin;
    uint boundsPadding0;
    float3 worldBoundsMax;
    uint boundsPadding1;
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
    float3 boundsMin;
    uint boundsPadding0;
    float3 boundsMax;
    uint boundsPadding1;
};

struct DrawUniforms
{
    uint firstEntry;
    uint padding0;
    uint padding1;
    uint padding2;
};
