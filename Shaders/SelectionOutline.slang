// Editor-only encoded SDR overlay. It never feeds lighting, reconstruction or capture output.
[[vk::binding(0, 0)]]
Texture2D<float4> gDisplay : register(t0);
[[vk::binding(1, 0)]]
Texture2D<float> gMask : register(t1);
[[vk::binding(2, 0)]]
Texture2D<float> gSelectedDepth : register(t2);
[[vk::binding(3, 0)]]
Texture2D<float> gSceneDepth : register(t3);
struct OutlineParams
{
    float radius;
};
[[vk::binding(0, 0)]]
ConstantBuffer<OutlineParams> gParams : register(b0);
struct VSOutput
{
    float4 position : SV_Position;
};
[shader("vertex")]
VSOutput vertexMain(uint vid: SV_VertexID)
{
    float2 uv = float2((vid << 1) & 2, vid & 2);
    VSOutput output;
    output.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    return output;
}
[shader("fragment")]
float4 fragmentMain(VSOutput input): SV_Target
{
    int2 pixel = int2(input.position.xy);
    float4 scene = gDisplay.Load(int3(pixel, 0));
    uint width, height;
    gMask.GetDimensions(width, height);
    float center = gMask.Load(int3(pixel, 0));
    float edge = 0.0;
    int radius = int(ceil(gParams.radius));
    for (int y = -radius; y <= radius; ++y)
    {
        for (int x = -radius; x <= radius; ++x)
        {
            float distance = length(float2(x, y));
            if (distance > gParams.radius + 0.5)
                continue;
            // Clamp at the viewport edge: clipping by the view does not invent a border.
            int2 at = clamp(pixel + int2(x, y), int2(0, 0), int2(width, height) - 1);
            float neighbor = gMask.Load(int3(at, 0));
            if (center == neighbor)
                continue;
            // Derive edges only from the object's own silhouette, never from occlusion cuts.
            // Reverse-Z: reject hidden source geometry AND expansion onto closer foreground.
            int2 source = center > 0.5 ? pixel : at;
            float selectedDepth = gSelectedDepth.Load(int3(source, 0));
            if (selectedDepth < gSceneDepth.Load(int3(source, 0)) ||
                selectedDepth < gSceneDepth.Load(int3(pixel, 0)))
                continue;
            float falloff = saturate(gParams.radius + 0.5 - distance);
            edge = max(edge, abs(center - neighbor) * falloff);
        }
    }
    // Warm, restrained UI color; filled interiors keep their exact display bytes.
    return float4(lerp(scene.rgb, float3(1.0, 0.76, 0.38), edge * 0.72), scene.a);
}
