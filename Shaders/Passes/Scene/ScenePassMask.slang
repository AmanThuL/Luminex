// Alpha-tested variant of ScenePass.slang, with manual exposure: passing masked = true to
// ScenePassShared.slang's shading selects the alpha cutoff and two-sided shading.
import ScenePassShared;

[shader("vertex")]
VSOutput vertexMain(uint vid: SV_VertexID, uint instanceIndex: SV_InstanceID)
{
    return sceneVertex(vid, instanceIndex);
}

[shader("fragment")]
float4 fragmentMain(VSOutput input, bool frontFace: SV_IsFrontFace): SV_Target
{
    return shadeSurfaceDetailed(input, frontFace, gPass.preExposure, true).color;
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
        input, shadeSurfaceDetailed(input.base, frontFace, gPass.preExposure, true));
}
