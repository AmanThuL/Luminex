// Input pixel structure
struct PixelInput
{
    float3 fragColor : COLOR0;
};

// Output pixel structure
struct PixelOutput
{
    float4 outColor : SV_TARGET0;
};

// Fragment shader function
PixelOutput main(PixelInput input)
{
    PixelOutput output;

    output.outColor = float4(input.fragColor, 1.0);

    return output;
}
