// Post-display local-light diagnostics. Uses ordinary scene fast-math, never safe clustering math.
// The address root carries the uploaded parameters, the light rows, the froxel grid, the index
// lists and the sampled depth and display views.
import RojoRHI;
import Lighting;
import LocalLights;
import LocalLightTypes;
import SceneTables;
import LightRoots;

[shader("vertex")]
float4 vertexMain(rojoRHI::VertexIds ids) : SV_Position
{
    uint vertex = rojoRHI::vertexIndex(ids);
    float2 uv = float2((vertex << 1) & 2, vertex & 2);
    return float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
}

[shader("fragment")]
float4 fragmentMain(float4 position : SV_Position) : SV_Target
{
    let root = rojoRHI::root<LightDebugRoot>();
    let gParams = rojoRHI::load<DebugParams>(root.params, 0);
    let gDepth = rojoRHI::texture2D<float>(root.depth);
    let gDisplay = rojoRHI::texture2D<float4>(root.display);
    uint2 outputPixel = uint2(position.xy);
    uint2 pixel = min(outputPixel * uint2(gParams.activeWidth, gParams.activeHeight) /
        uint2(gParams.outputWidth, gParams.outputHeight),
        uint2(gParams.activeWidth - 1u, gParams.activeHeight - 1u));
    float depth = gDepth.Load(int3(pixel, 0));
    if (depth <= 0.0f) return float4(0, 0, 0, 1);
    LocalLightParams cluster = {};
    cluster.mode = kLocalLightModeClustered;
    cluster.rowCount = gParams.rowCount;
    cluster.gridX = 16u; cluster.gridY = 9u; cluster.gridZ = 24u;
    cluster.activeOriginX = 0u; cluster.activeOriginY = 0u;
    cluster.activeWidth = gParams.activeWidth; cluster.activeHeight = gParams.activeHeight;
    ClusterRecord record = rojoRHI::load<ClusterRecord>(root.grid,
        LocalLightCluster(cluster, root.params, kDebugParamsSliceDepthWord,
                          float4(float2(pixel) + 0.5f, depth, 1)));
    uint count = record.count & ~kClusterTruncatedBit;
    bool truncated = (record.count & kClusterTruncatedBit) != 0u;
    if (gParams.mode == 1u)
    {
        if (count == 0u) return float4(0, 0, 0, 1);
        if (count <= 4u) return float4(0, 0, 1, 1);
        if (count <= 16u) return float4(0, 1, 0, 1);
        if (count <= 64u) return float4(1, 1, 0, 1);
        return float4(1, 0, 0, 1);
    }
    if (gParams.mode == 2u)
        return truncated ? float4(1, 0, 1, 1) :
            float4(gDisplay.Load(int3(outputPixel, 0)).rgb * 0.25f, 1);
    float2 uv = (float2(pixel) + 0.5f) / float2(gParams.activeWidth, gParams.activeHeight);
    float4 h = mul(gParams.inverseViewProjection, float4(uv.x * 2 - 1, 1 - uv.y * 2, depth, 1));
    float3 world = h.xyz / h.w;
    uint entry = 0u;
    for (uint row = 0u; row < gParams.rowCount; ++row)
    {
        while (entry < count && rojoRHI::load<uint>(root.indices, record.offset + entry) < row) ++entry;
        if (entry < count && rojoRHI::load<uint>(root.indices, record.offset + entry) == row) continue;
        LightRow light = rojoRHI::load<LightRow>(root.lights, row);
        float3 toLight = light.position - world;
        float distance = length(toLight);
        float3 ray = distance > 0.0f ? -toLight / distance : float3(0);
        if (distance < light.range && SpotTerm(dot(ray, light.direction), light.spotScale, light.spotOffset) > 0.0f)
            return truncated ? float4(1, 1, 0, 1) : float4(1, 0, 0, 1);
    }
    return float4(0, 0, 0, 1);
}
