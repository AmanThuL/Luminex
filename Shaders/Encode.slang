// The linear-to-sRGB transfer, imported by the display transform -- the one pipeline that leaves
// linear space. It stays a module of its own so the transform can be replaced without taking the
// project's definition of the sRGB curve with it.
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
