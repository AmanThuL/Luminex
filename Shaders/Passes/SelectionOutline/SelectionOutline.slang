// Editor-only encoded SDR overlay. It never feeds lighting, reconstruction or capture output.
import RojoRHI;
import SelectionOutlineRoots;

struct VSOutput
{
    float4 position : SV_Position;
};
[shader("vertex")]
VSOutput vertexMain(rojoRHI::VertexIds ids)
{
    uint vid = rojoRHI::vertexIndex(ids);
    float2 uv = float2((vid << 1) & 2, vid & 2);
    VSOutput output;
    output.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    return output;
}
[shader("fragment")]
float4 fragmentMain(VSOutput input): SV_Target
{
    SelectionOutlineRoot gRoot = rojoRHI::root<SelectionOutlineRoot>();
    OutlineParams gParams = rojoRHI::load<OutlineParams>(gRoot.paramsAddr, 0);
    let gDisplay = rojoRHI::texture2D<float4>(gRoot.display);
    let gMask = rojoRHI::texture2D<float>(gRoot.mask);
    let gSelectedDepth = rojoRHI::texture2D<float>(gRoot.selectedDepth);
    let gSceneDepth = rojoRHI::texture2D<float>(gRoot.sceneDepth);
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
    // Encoded sRGB #4CABFD is shared by both editor themes; interiors keep their display bytes.
    static const float3 kOutlineColor = float3(76.0, 171.0, 253.0) / 255.0;
    return float4(lerp(scene.rgb, kOutlineColor, edge * 0.72), scene.a);
}

// Rejected selections still refresh the texture the UI selected before frame declaration.
[shader("fragment")]
float4 fragmentCopy(VSOutput input): SV_Target
{
    SelectionCopyRoot gRoot = rojoRHI::root<SelectionCopyRoot>();
    let gDisplay = rojoRHI::texture2D<float4>(gRoot.display);
    return gDisplay.Load(int3(int2(input.position.xy), 0));
}
