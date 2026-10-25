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
// ---- visibility ----
// ---- lights ----
// ---- post ----
// ---- temporal ----

void probeMain()
{
}
