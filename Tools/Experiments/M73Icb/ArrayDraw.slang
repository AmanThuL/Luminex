StructuredBuffer<float4> positions : register(t0);
struct Varying { float4 position : SV_Position; nointerpolation uint material : MATERIAL; };
[shader("vertex")]
Varying vertexMain(uint vertex : SV_VertexID, uint instance : SV_InstanceID) {
    Varying result; result.position=positions[vertex]; result.material=instance; return result;
}
struct Textures { Texture2D<float4> images[10]; SamplerState samp; };
ParameterBlock<Textures> materials : register(b1);
[shader("fragment")]
float4 fragmentMain(Varying input) : SV_Target {
    float4 result = float4(0);
    for (uint i=0; i<5; ++i) result += materials.images[input.material*5+i].SampleLevel(materials.samp, float2(0.5), 0);
    return result / 5.0;
}
