import DisplayRoots;

StructuredBuffer<DisplayRoot> gRoot_DisplayRoot;

// ---- scene ----
import VertexStorage;
StructuredBuffer<VertexStorage> gRecord_VertexStorage;
import ShadowRoots;
StructuredBuffer<ShadowPassRoot> gRoot_ShadowPassRoot;
StructuredBuffer<ShadowDrawRoot> gRoot_ShadowDrawRoot;
StructuredBuffer<ShadowPassUniforms> gRecord_ShadowPassUniforms;
import SceneRoots;
StructuredBuffer<ScenePassRoot> gRoot_ScenePassRoot;
StructuredBuffer<SceneDrawRoot> gRoot_SceneDrawRoot;
StructuredBuffer<SkyRoot> gRoot_SkyRoot;
import SceneTables;
import Lighting;
StructuredBuffer<PassUniforms> gRecord_PassUniforms;
StructuredBuffer<SkyUniforms> gRecord_SkyUniforms;
StructuredBuffer<DirLight> gRecord_DirLight;
StructuredBuffer<InstanceRow> gRecord_InstanceRow;
StructuredBuffer<MaterialRow> gRecord_MaterialRow;
StructuredBuffer<MeshRow> gRecord_MeshRow;
StructuredBuffer<LightRow> gRecord_LightRow;
// ---- visibility ----
import VisibilityRoots;
import OcclusionRoots;
StructuredBuffer<VisibilityClassifyRoot> gRoot_VisibilityClassifyRoot;
StructuredBuffer<VisibilityOcclusionRoot> gRoot_VisibilityOcclusionRoot;
StructuredBuffer<VisibilityScanRoot> gRoot_VisibilityScanRoot;
StructuredBuffer<VisibilityEmitRoot> gRoot_VisibilityEmitRoot;
StructuredBuffer<HzbReduceRoot> gRoot_HzbReduceRoot;
StructuredBuffer<HzbPublishRoot> gRoot_HzbPublishRoot;
StructuredBuffer<HzbDebugRoot> gRoot_HzbDebugRoot;
StructuredBuffer<OcclusionReferenceRoot> gRoot_OcclusionReferenceRoot;
StructuredBuffer<CandidateRecord> gRecord_CandidateRecord;
StructuredBuffer<RunRecord> gRecord_RunRecord;
StructuredBuffer<ChunkRecord> gRecord_ChunkRecord;
StructuredBuffer<VisibilityViewParams> gRecord_VisibilityViewParams;
StructuredBuffer<VisibilityParams> gRecord_VisibilityParams;
StructuredBuffer<OcclusionParams> gRecord_OcclusionParams;
StructuredBuffer<HzbReduceParams> gRecord_HzbReduceParams;
StructuredBuffer<HzbDebugViewParams> gRecord_HzbDebugViewParams;
StructuredBuffer<ReferenceParams> gRecord_ReferenceParams;
// ---- lights ----
import LightRoots;
import LightCluster;
import LocalLightTypes;
StructuredBuffer<LightClusterRoot> gRoot_LightClusterRoot;
StructuredBuffer<LightDebugRoot> gRoot_LightDebugRoot;
StructuredBuffer<ClusterRecord> gRecord_ClusterRecord;
StructuredBuffer<LocalLightParams> gRecord_LocalLightParams;
StructuredBuffer<LightClusterParams> gRecord_LightClusterParams;
StructuredBuffer<DebugParams> gRecord_DebugParams;
// ---- post ----
import ExposureRoots;
StructuredBuffer<ExposureSeedRoot> gRoot_ExposureSeedRoot;
StructuredBuffer<HistogramRoot> gRoot_HistogramRoot;
StructuredBuffer<ExposureResolveRoot> gRoot_ExposureResolveRoot;
StructuredBuffer<HistogramParams> gRecord_HistogramParams;
StructuredBuffer<ExposureSeedParams> gRecord_ExposureSeedParams;
StructuredBuffer<ExposureResolveParams> gRecord_ExposureResolveParams;
import BloomRoots;
StructuredBuffer<BloomThresholdRoot> gRoot_BloomThresholdRoot;
StructuredBuffer<BloomDownsampleRoot> gRoot_BloomDownsampleRoot;
StructuredBuffer<BloomUpsampleRoot> gRoot_BloomUpsampleRoot;
StructuredBuffer<BloomThresholdParams> gRecord_BloomThresholdParams;
StructuredBuffer<BloomDownsampleParams> gRecord_BloomDownsampleParams;
StructuredBuffer<BloomUpsampleParams> gRecord_BloomUpsampleParams;
import SelectionOutlineRoots;
StructuredBuffer<SelectionMaskRoot> gRoot_SelectionMaskRoot;
StructuredBuffer<SelectionOutlineRoot> gRoot_SelectionOutlineRoot;
StructuredBuffer<SelectionCopyRoot> gRoot_SelectionCopyRoot;
StructuredBuffer<MaskParams> gRecord_MaskParams;
StructuredBuffer<OutlineParams> gRecord_OutlineParams;
// ---- temporal ----

void probeMain()
{
}
