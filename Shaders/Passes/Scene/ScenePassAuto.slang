// The auto-exposure twin of ScenePass.slang (spec 9): the same shading from ScenePassShared.slang,
// except the final multiply reads the persistent exposure buffer instead of
// PassUniforms.preExposure. A separate pipeline rather than a runtime branch in one shader, because
// a branch that is never taken still gives the compiler a different fragment to schedule around --
// proven by an M5 parity check that failed by a single rounding bit in one pixel once
// ScenePass.slang carried the branch. Renderer.cpp selects m_scenePipeline or m_scenePipelineAuto
// by SceneView::autoExposureEnabled, so the manual pipeline's compiled output is untouched by
// auto-exposure's existence. Reading gExposureOverride below is this file's one difference from
// ScenePass.slang.
import ScenePassShared;

// The persistent exposure buffer (spec 9's feedback buffer). This pipeline only ever binds while
// auto-exposure is on, so it is always read -- unlike ScenePass.slang, which never binds it at all.
[[vk::binding(3, 0)]]
StructuredBuffer<float> gExposureOverride : register(t3); // → MSL [[buffer(3)]]

[shader("vertex")]
VSOutput vertexMain(uint vid: SV_VertexID, uint instanceIndex: SV_InstanceID)
{
    return sceneVertex(vid, instanceIndex);
}

[shader("fragment")]
float4 fragmentMain(VSOutput input): SV_Target
{
    return shadeSurfaceDetailed(input, true, gExposureOverride[0], false).color;
}

[shader("vertex")]
MotionVSOutput vertexMainMotion(uint vid: SV_VertexID, uint instanceIndex: SV_InstanceID)
{
    return sceneMotionVertex(vid, instanceIndex);
}

[shader("fragment")]
MotionFSOutput fragmentMainMotion(MotionVSOutput input)
{
    return sceneMotionOutput(
        input, shadeSurfaceDetailed(input.base, true, gExposureOverride[0], false));
}
