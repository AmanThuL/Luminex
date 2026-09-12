// The frame's one scene-linear -> display step, run as a fullscreen pass over the HDR scene
// colour: bloom composition, a neutral tone map into [0, 1], then the sRGB encode. Nothing upstream
// of this pass encodes, so this file is the whole of the image's display side and can be replaced
// on its own; the one thing downstream that encodes is the temporal debug view drawn over the
// finished image, which imports the same Tonemap and Encode modules so the two agree.
// Slots: texture 0 scene color, 1 bloom result; buffer 0 params.
//
// Bloom composites here, in scene-linear space before the tone map (spec 10): `gBloom` holds the
// upsample-accumulate chain's result at half the scene's resolution, reconstructed with bilinear
// filtering at display pixel centres, and `bloomIntensity` is zero whenever Renderer.cpp's bloom
// toggle is off -- disabled frames skip the texture load and add exactly x + 0 to their scene
// color, so they stay byte-identical to a frame with no bloom pass at all. Renderer.cpp still binds
// a black 1x1 fallback because the argument-table slot must contain a valid resource.
import Encode;
import Tonemap;

[[vk::binding(0, 0)]]
Texture2D<float4> gSceneColor : register(t0); // → MSL [[texture(0)]]

[[vk::binding(1, 0)]]
Texture2D<float4> gBloom : register(t1); // → MSL [[texture(1)]]

struct DisplayParams
{
    float bloomIntensity;
    float peak;
    uint linear;
    uint calibration;
};

[[vk::binding(0, 0)]]
ConstantBuffer<DisplayParams> gParams : register(b0); // → MSL [[buffer(0)]]

struct VSOutput
{
    float4 position : SV_Position;
};

[shader("vertex")]
VSOutput vertexMain(uint vid: SV_VertexID)
{
    // Three-vertex fullscreen triangle.
    float2 uv = float2((vid << 1) & 2, vid & 2);
    VSOutput o;
    o.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    return o;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input): SV_Target
{
    // Load rather than Sample: this pass and the scene target share an extent, so every fragment
    // owns exactly one texel and no filter may blur the transform's input.
    float3 sceneColor = gSceneColor.Load(int3(int2(input.position.xy), 0)).rgb;
    // gBloom holds its result at half the scene's resolution (Renderer.cpp's bloom chain). Bloom
    // off binds a 1x1 fallback solely to keep the argument table complete; skip the load entirely
    // in that mode so pixels outside its one texel never make an invalid texture access before the
    // zero intensity is applied.
    float3 bloom = 0.0;
    if (gParams.bloomIntensity != 0.0)
    {
        uint sceneWidth, sceneHeight, bloomWidth, bloomHeight;
        gSceneColor.GetDimensions(sceneWidth, sceneHeight);
        gBloom.GetDimensions(bloomWidth, bloomHeight);
        uint2 bloomExtent = uint2(bloomWidth, bloomHeight);
        float2 bloomPosition = input.position.xy * float2(bloomExtent) /
                               float2(sceneWidth, sceneHeight) - 0.5;
        bloomPosition = clamp(bloomPosition, 0.0, float2(bloomExtent - uint2(1, 1)));
        uint2 lo = uint2(bloomPosition);
        uint2 hi = min(lo + uint2(1, 1), bloomExtent - uint2(1, 1));
        float2 weight = frac(bloomPosition);
        float3 top = lerp(gBloom.Load(int3(int2(lo), 0)).rgb,
                          gBloom.Load(int3(int2(hi.x, lo.y), 0)).rgb, weight.x);
        float3 bottom = lerp(gBloom.Load(int3(int2(lo.x, hi.y), 0)).rgb,
                             gBloom.Load(int3(int2(hi), 0)).rgb, weight.x);
        bloom = lerp(top, bottom, weight.y);
    }
    float3 composed = sceneColor + bloom * gParams.bloomIntensity;
    // The display target is opaque -- the scene's alpha is material coverage, not an output.
    float3 output = gParams.peak * pbrNeutralToneMap(composed / gParams.peak);
    if (gParams.calibration != 0)
    {
        uint width, height;
        gSceneColor.GetDimensions(width, height);
        float2 uv = input.position.xy / float2(width, height);
        if (uv.y < 0.45)
        {
            uint patch = min(uint(uv.x * 6), 5u);
            float values[6] = {0.25, 0.5, 1.0, 2.0, 4.0, gParams.peak};
            output = min(values[patch], gParams.peak).xxx;
        }
        else if (uv.y < 0.70)
            output = min(uv.x * 4, gParams.peak).xxx;
        else
            output = gParams.peak * pbrNeutralToneMap((uv.x * 4).xxx / gParams.peak);
    }
    return float4(gParams.linear != 0 ? output : linearToSrgb(output), 1.0);
}
