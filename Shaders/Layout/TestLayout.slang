import OracleRoots;

StructuredBuffer<BufferHazardRoot> gRoot_BufferHazardRoot;
StructuredBuffer<ComputeImageRoot> gRoot_ComputeImageRoot;
StructuredBuffer<FullscreenSampleRoot> gRoot_FullscreenSampleRoot;
StructuredBuffer<SamplerRoot> gRoot_SamplerRoot;
StructuredBuffer<VisibilityDepthRoot> gRoot_VisibilityDepthRoot;

// ---- scene ----
import VertexStorage;
StructuredBuffer<VertexStorage> gRecord_VertexStorage;
StructuredBuffer<ShadowSmokeRoot> gRoot_ShadowSmokeRoot;
StructuredBuffer<ShadowSmokeUniforms> gRecord_ShadowSmokeUniforms;
// ---- visibility ----
// ---- lights ----
// ---- post ----
// ---- temporal ----

void probeMain()
{
}
