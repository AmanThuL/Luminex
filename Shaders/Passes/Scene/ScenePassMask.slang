// Alpha-tested variant of ScenePass.slang, with manual exposure: the masked cutoff and two-sided
// shading come from MaskedCoverage in ScenePassShared.slang, which holds the shared shading.
import ScenePassShared;

[shader("vertex")]
VSOutput vertexMain(uint vid: SV_VertexID, uint instanceIndex: SV_InstanceID)
{
    return sceneVertex(vid, instanceIndex);
}

[shader("fragment")]
float4 fragmentMain(VSOutput input, bool frontFace: SV_IsFrontFace): SV_Target
{
    return shadeSurfaceDetailed<ManualExposure, MaskedCoverage>(input, frontFace).color;
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
        input, shadeSurfaceDetailed<ManualExposure, MaskedCoverage>(input.base, frontFace));
}
