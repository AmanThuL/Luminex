// Shared coverage rule: sRGB texture sampling leaves alpha linear; equality survives the cutoff.
void applyAlphaMask(float baseColorAlpha, float cutoff)
{
    if (baseColorAlpha < cutoff)
        discard;
}
