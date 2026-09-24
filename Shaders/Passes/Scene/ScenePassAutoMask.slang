// Alpha-tested variant of ScenePassAuto.slang: MaskedCoverage from ScenePassShared.slang supplies
// the cutoff and two-sided shading. As in ScenePassAuto.slang, the auto-exposure twin is a separate
// pipeline rather than a runtime branch, because a never-taken branch still changes the manual
// fragment's compiled output (an M5 parity check failed by one rounding bit once ScenePass.slang
// carried it). AutoExposure below is this file's one difference from ScenePassMask.slang.
import ScenePassShared;

// The persistent exposure buffer (spec 9's feedback buffer). This pipeline only ever binds while
// auto-exposure is on, so it is always read -- unlike ScenePass.slang, which never binds it at all.
[[vk::binding(3, 0)]]
StructuredBuffer<float> gExposureOverride : register(t3); // → MSL [[buffer(3)]]

struct AutoExposure : ISceneExposure
{
    static float preExposure() { return gExposureOverride[0]; }
}

[shader("vertex")]
VSOutput vertexMain(uint vid: SV_VertexID, uint instanceIndex: SV_InstanceID)
{
    return sceneVertex(vid, instanceIndex);
}

[shader("fragment")]
float4 fragmentMain(VSOutput input, bool frontFace: SV_IsFrontFace): SV_Target
{
    return shadeSurfaceDetailed<AutoExposure, MaskedCoverage>(input, frontFace).color;
}

[shader("vertex")]
MotionVSOutput vertexMainMotion(uint vid: SV_VertexID, uint instanceIndex: SV_InstanceID)
{
    return sceneMotionVertex(vid, instanceIndex);
}

[shader("fragment")]
MotionFSOutput fragmentMainMotion(MotionVSOutput input, bool frontFace: SV_IsFrontFace)
{
    return sceneMotionOutput(
        input, shadeSurfaceDetailed<AutoExposure, MaskedCoverage>(input.base, frontFace));
}
