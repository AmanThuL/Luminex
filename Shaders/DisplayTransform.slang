// The frame's one scene-linear -> display step, run as a fullscreen pass over the HDR scene
// colour: a neutral tone map into [0, 1] followed by the sRGB encode. No other shader in the
// project encodes, so this file is the whole of the image's display side and can be replaced on
// its own. Slots: texture 0.
import Encode;

[[vk::binding(0, 0)]]
Texture2D<float4> gSceneColor : register(t0); // → MSL [[texture(0)]]

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

// Khronos PBR Neutral: a display-referred tone map that leaves in-gamut colour alone and rolls
// only what exceeds the display into range. Below the compression start it is the identity minus
// a small black offset, so mid-tones keep the value the shading produced; past it the peak channel
// is compressed and the colour desaturates toward that peak instead of clipping a channel.
static const float kStartCompression = 0.8 - 0.04;
static const float kDesaturation = 0.15;

float3 pbrNeutralToneMap(float3 color)
{
    float x = min(color.r, min(color.g, color.b));
    float offset = (x < 0.08) ? (x - 6.25 * x * x) : 0.04;
    color -= offset;

    float peak = max(color.r, max(color.g, color.b));
    if (peak < kStartCompression)
    {
        return color;
    }

    const float d = 1.0 - kStartCompression;
    float newPeak = 1.0 - d * d / (peak + d - kStartCompression);
    color *= newPeak / peak;

    float g = 1.0 - 1.0 / (kDesaturation * (peak - newPeak) + 1.0);
    return lerp(color, newPeak.xxx, g);
}

[shader("fragment")]
float4 fragmentMain(VSOutput input): SV_Target
{
    // Load rather than Sample: this pass and the scene target share an extent, so every fragment
    // owns exactly one texel and no filter may blur the transform's input.
    float3 sceneColor = gSceneColor.Load(int3(int2(input.position.xy), 0)).rgb;
    // The display target is opaque -- the scene's alpha is material coverage, not an output.
    return float4(linearToSrgb(pbrNeutralToneMap(sceneColor)), 1.0);
}
