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
import VisibilityRoots;
import OcclusionRoots;
import VisibilityOracleRoots;
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
StructuredBuffer<HzbDepthRoot> gRoot_HzbDepthRoot;
StructuredBuffer<HzbReadbackRoot> gRoot_HzbReadbackRoot;
StructuredBuffer<OcclusionProbeRoot> gRoot_OcclusionProbeRoot;
StructuredBuffer<ReadbackParams> gRecord_ReadbackParams;
StructuredBuffer<ProbeBounds> gRecord_ProbeBounds;
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

StructuredBuffer<SceneTableAbiRoot> gRoot_SceneTableAbiRoot;
