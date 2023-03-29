// Input vertex structure
struct VertexInput
{
    float2 inPosition : POSITION0;
    float3 inColor : COLOR0;
};

// Output vertex structure
struct VertexOutput
{
    float4 outPosition : SV_POSITION;
    float3 fragColor : COLOR0;
};

// Vertex shader function
VertexOutput main(VertexInput input)
{
    VertexOutput output;

    output.outPosition = float4(input.inPosition, 0.0, 1.0);
    output.fragColor = input.inColor;

    return output;
}
