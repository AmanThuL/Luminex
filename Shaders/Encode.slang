// Shared linear-to-sRGB transfer for pipelines writing a non-sRGB scene target.
module Encode;

// Clamp before pow() because negative bases are invalid and the target saturates on write.
public float linearToSrgbChannel(float c)
{
    c = saturate(c);
    return c <= 0.0031308 ? 12.92 * c : 1.055 * pow(c, 1.0 / 2.4) - 0.055;
}

public float3 linearToSrgb(float3 c)
{
    return float3(linearToSrgbChannel(c.r), linearToSrgbChannel(c.g), linearToSrgbChannel(c.b));
}
