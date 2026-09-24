// Forward scene pass, opaque with manual exposure: scene-linear radiance pre-exposed by
// PassUniforms.preExposure. The shading, slot map and vertex outputs live in ScenePassShared.slang.
import ScenePassShared;

[shader("vertex")]
VSOutput vertexMain(uint vid: SV_VertexID, uint instanceIndex: SV_InstanceID)
{
    return sceneVertex(vid, instanceIndex);
}

[shader("fragment")]
float4 fragmentMain(VSOutput input): SV_Target
{
    return shadeSurfaceDetailed(input, true, gPass.preExposure, false).color;
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
        input, shadeSurfaceDetailed(input.base, true, gPass.preExposure, false));
}
