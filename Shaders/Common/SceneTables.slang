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
static const uint kSceneLightsSlot = 8u;
static const uint kLightClusterGridSlot = 9u;
static const uint kLightClusterIndexSlot = 10u;
static const uint kLocalLightParamsSlot = 11u;

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

// Mirrors lmx::render::LightRow. `range == 0` marks a free slot, `strength` is the light's colour
// already multiplied by its intensity, and `spotScale`/`spotOffset` encode the cone term for both
// light types, so nothing downstream branches on the type. The bounding sphere is CPU-derived.
struct LightRow
{
    float3 position;
    float range;
    float3 strength;
    float spotScale;
    float3 direction;
    float spotOffset;
    float3 boundCentre;
    float boundRadius;
};

struct DrawUniforms
{
    uint firstEntry;
    uint padding0;
    uint padding1;
    uint padding2;
};
