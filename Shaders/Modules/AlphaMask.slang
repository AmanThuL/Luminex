// Shared coverage rule: sRGB texture sampling leaves alpha linear; equality survives the cutoff.
struct AlphaMaskParams
{
    float cutoff;
};

[[vk::binding(4, 0)]]
ConstantBuffer<AlphaMaskParams> gAlphaMask : register(b4);

void applyAlphaMask(float baseColorAlpha)
{
    if (baseColorAlpha < gAlphaMask.cutoff)
        discard;
}
