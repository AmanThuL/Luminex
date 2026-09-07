// The motion convention, shared by every pass that writes or reads lmx.render.motion. It mirrors
// Source/Render/Temporal.h and must stay arithmetically identical to it: motion is a texture-space
// UV delta over the render extent, built from *unjittered* clip positions, and a consumer fetches
// history at uv - motion.
module Motion;

// +inf, spelled as a bit pattern because a literal division by zero is not a foldable constant.
static const float kMotionInvalidChannel = asfloat(0x7F800000u);

// Written when motion is undefined. Consumers test isinf(); zero is a legitimate motion value and
// must never stand in for "unknown".
public static const float2 kMotionInvalid = float2(kMotionInvalidChannel, kMotionInvalidChannel);

// ndc.xy * (0.5, -0.5) + 0.5: +y runs down the image, matching texture space.
float2 clipToMotionUv(float4 clip)
{
    return clip.xy / clip.w * float2(0.5, -0.5) + 0.5;
}

public float2 motionFromClip(float4 current, float4 previous)
{
    return clipToMotionUv(current) - clipToMotionUv(previous);
}
