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
import LightRoots;
import LightCluster;
import LocalLightTypes;
import LocalLightOracleRoots;
import SceneTables;
StructuredBuffer<LightClusterRoot> gRoot_LightClusterRoot;
StructuredBuffer<LightDebugRoot> gRoot_LightDebugRoot;
StructuredBuffer<PunctualLightRoot> gRoot_PunctualLightRoot;
StructuredBuffer<LightRow> gRecord_LightRow;
StructuredBuffer<ClusterRecord> gRecord_ClusterRecord;
StructuredBuffer<LocalLightParams> gRecord_LocalLightParams;
StructuredBuffer<LightClusterParams> gRecord_LightClusterParams;
StructuredBuffer<DebugParams> gRecord_DebugParams;
StructuredBuffer<PunctualSample> gRecord_PunctualSample;
// ---- post ----
// ---- temporal ----

void probeMain()
{
}

StructuredBuffer<SceneTableAbiRoot> gRoot_SceneTableAbiRoot;
