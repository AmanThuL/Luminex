// The frame's one scene-linear -> display step, run as a fullscreen pass over the HDR scene
// colour: bloom composition, a neutral tone map into [0, 1], then the sRGB encode. Nothing upstream
// of this pass encodes, so this file is the whole of the image's display side and can be replaced
// on its own; the one thing downstream that encodes is the temporal debug view drawn over the
// finished image, which imports the same Tonemap and Encode modules so the two agree.
// Slots: texture 0 scene color, 1 bloom result; buffer 0 params.
//
// Bloom composites here, in scene-linear space before the tone map (spec 10): `gBloom` holds the
// upsample-accumulate chain's result at half the scene's resolution, sampled by halving the
// display-target texel coordinate, and `bloomIntensity` is zero whenever Renderer.cpp's bloom
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
        bloom = gBloom.Load(int3(int2(input.position.xy) >> 1, 0)).rgb;
    }
    float3 composed = sceneColor + bloom * gParams.bloomIntensity;
    // The display target is opaque -- the scene's alpha is material coverage, not an output.
    return float4(linearToSrgb(pbrNeutralToneMap(composed)), 1.0);
}
