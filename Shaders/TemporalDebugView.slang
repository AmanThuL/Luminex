// The temporal debug views (spec 6), drawn over the finished display image as a fullscreen pass.
// Both encodings are meant to be read back and compared, not looked at: values land in the display
// target unencoded, because this pass replaces the display transform's output rather than feeding
// it.
//
// Slots: textures 0 motion, 1 reprojection diagnostic; buffer 0 params.
import Motion;

struct TemporalDebugViewParams
{
    uint view; // kViewMotionVectors or kViewReprojectionError
};

// Mirrors Source/Render/Renderer.h's TemporalDebugView, whose Off never reaches this shader --
// the pass is not declared at all in that mode.
static const uint kViewMotionVectors = 1u;
static const uint kViewReprojectionError = 2u;

// Motion is a fraction of the extent and would be invisible unscaled; 8 puts an eighth of the
// screen at full range and keeps the encoding invertible by a reader.
static const float kMotionScale = 8.0;
// The reprojection error is a scene-linear difference, so a visible one is small; 4 is what makes
// a quarter-unit difference read as white.
static const float kErrorScale = 4.0;
static const float3 kLuminanceWeights = float3(0.2126, 0.7152, 0.0722);

[[vk::binding(0, 0)]]
Texture2D<float2> gMotion : register(t0); // → MSL [[texture(0)]]

[[vk::binding(1, 0)]]
Texture2D<float4> gDiagnostic : register(t1); // → MSL [[texture(1)]]

[[vk::binding(0, 0)]]
ConstantBuffer<TemporalDebugViewParams> gParams : register(b0); // → MSL [[buffer(0)]]

struct VSOutput
{
    float4 position : SV_Position;
};

[shader("vertex")]
VSOutput vertexMain(uint vid: SV_VertexID)
{
    // Three-vertex fullscreen triangle.
    float2 uv = float2((vid << 1) & 2, vid & 2);
    VSOutput o;
    o.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    return o;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input): SV_Target
{
    int3 coord = int3(int2(input.position.xy), 0);
    if (gParams.view == kViewMotionVectors)
    {
        float2 motion = gMotion.Load(coord).xy;
        if (isinf(motion.x) || isinf(motion.y))
        {
            return float4(1.0, 0.0, 1.0, 1.0);
        }
        return float4(0.5 + motion * kMotionScale, 0.5, 1.0);
    }

    float4 diagnostic = gDiagnostic.Load(coord);
    if (diagnostic.a == 0.0)
    {
        // Nothing to compare against: the history fetch left the extent, or motion was undefined.
        return float4(0.0, 0.0, 1.0, 1.0);
    }
    float grey = min(1.0, kErrorScale * dot(diagnostic.rgb, kLuminanceWeights));
    return float4(grey, grey, grey, 1.0);
}
