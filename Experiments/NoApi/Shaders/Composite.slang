// P12 composite's binding frontend (spec section 6): a fullscreen triangle reading the scene colour
// (R2), the bloom result (R7 mip 0), and this frame's resolved exposure (R5) into R8, a
// display-transform-shaped tone map. The exposure read exists to exercise the pass's full declared
// binding surface (spec section 6 lists all three as P12's inputs) rather than to model production's
// manual/auto-exposure split -- ScenePass.slang already pre-exposed the scene colour this pass reads,
// so gExposure here is an independent multiplicative term, applied after the bloom composite and
// before the tone map. Mirrors Shaders/DisplayTransform.slang's bloom composite; the tone map and
// the sRGB encode both live in NoApiShared.slang (see its header comment for why the encode is
// restated there rather than imported from Shaders/Encode.slang directly) so the prototype's
// composite frontend applies the identical curves.
import NoApiShared;

struct CompositeParams
{
    float bloomIntensity;
};

[[vk::binding(0, 0)]]
Texture2D<float4> gSceneColor : register(t0); // → MSL [[texture(0)]]

[[vk::binding(1, 0)]]
Texture2D<float4> gBloom : register(t1); // → MSL [[texture(1)]]

[[vk::binding(0, 0)]]
StructuredBuffer<float> gExposure : register(t0); // → MSL [[buffer(0)]]

[[vk::binding(1, 0)]]
ConstantBuffer<CompositeParams> gParams : register(b1); // → MSL [[buffer(1)]]

struct VSOutput
{
    float4 position : SV_Position;
};

[shader("vertex")]
VSOutput vertexMain(uint vid: SV_VertexID)
{
    // Three-vertex fullscreen triangle, no vertex buffer -- Shaders/DisplayTransform.slang's own
    // construction.
    float2 uv = float2((vid << 1) & 2, vid & 2);
    VSOutput o;
    o.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    return o;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input): SV_Target
{
    float3 sceneColor = gSceneColor.Load(int3(int2(input.position.xy), 0)).rgb;
    float3 bloom = gBloom.Load(int3(int2(input.position.xy) >> 1, 0)).rgb;
    float exposure = gExposure[0];
    float3 composed = (sceneColor + bloom * gParams.bloomIntensity) * exposure;
    return float4(LinearToSrgb(PbrNeutralToneMap(composed)), 1.0);
}
