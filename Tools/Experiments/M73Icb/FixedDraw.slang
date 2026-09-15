StructuredBuffer<float4> positions : register(t0);
struct Varying { float4 position : SV_Position; nointerpolation uint material : MATERIAL; };
[shader("vertex")]
Varying vertexMain(uint vertex : SV_VertexID, uint instance : SV_InstanceID) {
    Varying result; result.position=positions[vertex]; result.material=instance; return result;
}
Texture2D<float4> image0 : register(t0);
Texture2D<float4> image1 : register(t1);
Texture2D<float4> image2 : register(t2);
Texture2D<float4> image3 : register(t3);
Texture2D<float4> image4 : register(t4);
SamplerState samp : register(s0);
[shader("fragment")]
float4 fragmentMain(Varying input) : SV_Target {
    float4 result = float4(0);
    result += image0.SampleLevel(samp,float2(0.5),0);
    result += image1.SampleLevel(samp,float2(0.5),0);
    result += image2.SampleLevel(samp,float2(0.5),0);
    result += image3.SampleLevel(samp,float2(0.5),0);
    result += image4.SampleLevel(samp,float2(0.5),0);
    return result / 5.0;
}
