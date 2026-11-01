// The temporal debug views (M6.2 spec 8), drawn over the finished display image as a fullscreen
// pass. Most encodings are meant to be read back and compared, not looked at: values land in the
// display target unencoded, because this pass replaces the display transform's output rather than
// feeding it. The one exception is ReprojectedHistory, which is a colour signal and therefore goes
// through the same tone map and transfer the display pass applied, so it can be held beside the
// picture it came from.
//
// The address root carries the view's parameters and the motion, reprojection diagnostic,
// rejection, reprojected history and resolved colour views; each of the last four that the view
// does not read is a 1x1 fallback view.
import RojoRHI;
import Encode;
import Motion;
import Tonemap;
import TemporalRoots;

// Mirrors Source/Render/TemporalResolve.h's TemporalDebugView, whose Off never reaches this shader
// -- the pass is not declared at all in that mode.
static const uint kViewMotionVectors = 1u;
static const uint kViewReprojectionError = 2u;
static const uint kViewReprojectedHistory = 3u;
static const uint kViewRejectionMask = 4u;
static const uint kViewBlendWeight = 5u;
static const uint kViewHistoryAge = 6u;

// Mirrors Shaders/Passes/Temporal/TemporalResolve.slang's reason codes, which the rejection mask
// stores as R = code / 255 with kClippedBit set alongside.
static const uint kReasonNone = 0u;
static const uint kReasonOffScreen = 1u;
static const uint kReasonInvalid = 2u;
static const uint kReasonDisoccluded = 3u;
static const uint kReasonReactive = 4u;
static const uint kClippedBit = 0x80u;

// One jitter period, mirroring Source/Render/TemporalResolve.h's kTemporalWarmupFrames: the age at
// which the history is considered warm, and so the value HistoryAge shows as white.
static const float kTemporalWarmupFrames = 16.0;

// Motion is a fraction of the extent and would be invisible unscaled; 8 puts an eighth of the
// screen at full range and keeps the encoding invertible by a reader.
static const float kMotionScale = 8.0;
// The reprojection error is a scene-linear difference, so a visible one is small; 4 is what makes
// a quarter-unit difference read as white.
static const float kErrorScale = 4.0;
static const float3 kLuminanceWeights = float3(0.2126, 0.7152, 0.0722);

// The render texel a display pixel's *centre* maps to, clamped so the last row and column of a
// rounded render extent stay addressable. At the output extent it is the pixel itself. No jitter:
// this view has none to apply and motion is an unjittered signal. The reprojection diagnostic and
// the upscale kernel map the same centre and then add the frame's jitter offset, because what they
// address is the jittered render image rather than the motion of a display pixel.
int3 motionCoordFor(TemporalDebugViewParams params, float2 pixel)
{
    float2 render = float2(float(params.renderWidth), float(params.renderHeight));
    float2 output = float2(float(params.outputWidth), float(params.outputHeight));
    return int3(clamp(int2((pixel + 0.5) * render / output), int2(0, 0), int2(render) - 1), 0);
}

struct VSOutput
{
    float4 position : SV_Position;
};

[shader("vertex")]
VSOutput vertexMain(rojoRHI::VertexIds ids)
{
    uint vid = rojoRHI::vertexIndex(ids);
    // Three-vertex fullscreen triangle.
    float2 uv = float2((vid << 1) & 2, vid & 2);
    VSOutput o;
    o.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    return o;
}

// Flat colours rather than a ramp: the mask is a set of named states, and a reader picking a pixel
// out of a screenshot has to be able to say which one it is without a legend of near neighbours.
float3 rejectionColor(uint reason)
{
    if (reason == kReasonOffScreen)
    {
        return float3(0.0, 0.0, 1.0);
    }
    if (reason == kReasonInvalid)
    {
        return float3(1.0, 0.0, 1.0);
    }
    if (reason == kReasonDisoccluded)
    {
        return float3(1.0, 0.0, 0.0);
    }
    if (reason == kReasonReactive)
    {
        return float3(1.0, 1.0, 0.0);
    }
    return float3(0.0, 0.0, 0.0);
}

[shader("fragment")]
float4 fragmentMain(VSOutput input): SV_Target
{
    let root = rojoRHI::root<TemporalDebugViewRoot>();
    let params = rojoRHI::load<TemporalDebugViewParams>(root.paramsAddr, 0);
    let motionTexture = rojoRHI::texture2D<float2>(root.motion);
    let diagnosticTexture = rojoRHI::texture2D<float4>(root.diagnostic);
    let rejectionTexture = rojoRHI::texture2D<float4>(root.rejection);
    let reprojectedTexture = rojoRHI::texture2D<float4>(root.reprojected);
    let resolvedTexture = rojoRHI::texture2D<float4>(root.resolved);
    int3 coord = int3(int2(input.position.xy), 0);
    if (params.view == kViewMotionVectors)
    {
        float2 motion = motionTexture.Load(motionCoordFor(params, float2(coord.xy))).xy;
        if (isinf(motion.x) || isinf(motion.y))
        {
            return float4(1.0, 0.0, 1.0, 1.0);
        }
        return float4(0.5 + motion * kMotionScale, 0.5, 1.0);
    }

    if (params.view == kViewReprojectedHistory)
    {
        float4 reprojected = reprojectedTexture.Load(coord);
        if (reprojected.a == 0.0)
        {
            // Rejected: there is no history to show, which is itself the thing to see.
            return float4(0.0, 0.0, 1.0, 1.0);
        }
        return float4(linearToSrgb(pbrNeutralToneMap(reprojected.rgb)), 1.0);
    }

    if (params.view == kViewRejectionMask)
    {
        float4 rejection = rejectionTexture.Load(coord);
        uint code = uint(round(rejection.r * 255.0));
        float3 color = rejectionColor(code & ~kClippedBit);
        if ((code & kClippedBit) != 0u)
        {
            color.g += 0.5;
        }
        return float4(color, 1.0);
    }

    if (params.view == kViewBlendWeight)
    {
        float weight = rejectionTexture.Load(coord).g;
        return float4(weight, weight, weight, 1.0);
    }

    if (params.view == kViewHistoryAge)
    {
        // The age lives in the resolved colour's alpha, so white means the pixel has accumulated a
        // full jitter period and the picture there is as converged as it is going to get.
        float age = floor(resolvedTexture.Load(coord).a);
        float grey = min(age / kTemporalWarmupFrames, 1.0);
        return float4(grey, grey, grey, 1.0);
    }

    float4 diagnostic = diagnosticTexture.Load(coord);
    if (diagnostic.a == 0.0)
    {
        // Nothing to compare against: the history fetch left the extent, or motion was undefined.
        return float4(0.0, 0.0, 1.0, 1.0);
    }
    float grey = min(1.0, kErrorScale * dot(diagnostic.rgb, kLuminanceWeights));
    return float4(grey, grey, grey, 1.0);
}
