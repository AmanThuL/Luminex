// P12 composite's address-first frontend (spec section 6), over the same NoApiShared tone map and
// sRGB encode the incumbent's Composite.slang uses.
//
// Binding surface: the vertex stage reads no root data at all (the fullscreen triangle comes from
// SV_VertexID), so `draw` passes `kNullAddress` for it; the pixel root at buffer 2 carries the scene
// colour and bloom slots, the table's address, and the address of this frame's resolved exposure.
import NoApiShared;
import NoApiBindless;

struct CompositeRoot
{
    uint4 slots;   // x scene colour slot, y bloom slot
    float4 params; // x bloom intensity
    ConstBufferPointer<TextureSlot> textures;
    ConstBufferPointer<float> exposure; // R5, this frame's resolve
};

[[vk::binding(2, 0)]]
ConstantBuffer<CompositeRoot> gRoot : register(b2); // → MSL [[buffer(2)]], the pixel root

struct VSOutput
{
    float4 position : SV_Position;
};

[shader("vertex")]
VSOutput vertexMain(uint vid: SV_VertexID)
{
    // Three-vertex fullscreen triangle, no vertex buffer -- Shaders/DisplayTransform.slang's own
    // construction, and the incumbent Composite.slang's.
    float2 uv = float2((vid << 1) & 2, vid & 2);
    VSOutput o;
    o.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    return o;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input): SV_Target
{
    Texture2D<float4> sceneColorTexture = gRoot.textures[gRoot.slots.x].handle;
    Texture2D<float4> bloomTexture = gRoot.textures[gRoot.slots.y].handle;

    float3 sceneColor = sceneColorTexture.Load(int3(int2(input.position.xy), 0)).rgb;
    float3 bloom = bloomTexture.Load(int3(int2(input.position.xy) >> 1, 0)).rgb;
    float exposure = gRoot.exposure[0];
    float3 composed = (sceneColor + bloom * gRoot.params.x) * exposure;
    return float4(LinearToSrgb(PbrNeutralToneMap(composed)), 1.0);
}
