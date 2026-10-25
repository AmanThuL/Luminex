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
// ---- lights ----
// ---- post ----
// ---- temporal ----

void probeMain()
{
}
